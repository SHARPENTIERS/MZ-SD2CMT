#include "record_autoname.h"

#include <Arduino.h>
#include <avr/pgmspace.h>
#include <string.h>

#include "record_path_buffer.h"
#include "../formats/mzi_sidecar.h"
#include "../formats/mz_loader_profiles.h"
#include "../formats/mz_tape_decoder.h"
#include "../formats/mz_title.h"
#include "../drivers/flash_text.h"
#include "../drivers/sdcard.h"
#include "../streams/wav_sample_stream.h"

#define AUTONAME_NAME_BYTES 17U
#define AUTONAME_MAX_SUFFIX 99U

/*
   The CM/CMT + Sinclair recognizer owns no separate header buffer. It probes
   a short run of intervals and then reuses one union state for exactly one
   candidate protocol. Small states and counters stay byte-sized where their
   ranges allow it, and candidate titles are written directly to autoname_name[]
   only becoming visible after checksum validation. This bounds persistent SRAM
   while multiple recognition formats share one decoder workspace.
*/
#define AUTONAME_AUX_PROBE_INTERVALS 32U
#define AUTONAME_AUX_MAX_INTERVAL_X8 640U
#define AUTONAME_SINCLAIR_MIN_PILOT 256U
#define AUTONAME_CMT_MIN_LEADER_SYMBOLS 256U
#define AUTONAME_AUX_NONE 0U
#define AUTONAME_AUX_PROBE 1U
#define AUTONAME_AUX_SIN_PILOT 2U
#define AUTONAME_AUX_SIN_SYNC2 3U
#define AUTONAME_AUX_SIN_DATA 4U
#define AUTONAME_AUX_CMT_LEADER 5U
#define AUTONAME_AUX_CMT_DATA 6U
#define AUTONAME_AUX_LOCKED 7U
#define AUTONAME_SIN_FIRST_PULSE_NONE 0xFFU

/*
   A fast native Sharp leader is distinctive enough to stop the parallel AUX
   Sinclair/CM/CMT search early.  Like the main Sharp decoder, this probe uses
   only physical WRITE LOW (logical PC1 HIGH after the external inverter).
   It never pairs both polarities or tries the opposite level.  256 intervals
   are still only a few tens of ms at Normal 1:2..1:4, but reject short
   accidental tones very strongly.
*/
#define AUTONAME_MZ_FAST_LOCK_PULSES 256U
#define AUTONAME_MZ_FAST_MIN_X8 32U
#define AUTONAME_MZ_FAST_MAX_X8 80U

/* WAV AutoName converts sample-run lengths to the decoder's L16*8 time
   reference. Cache the fixed-point conversion once at begin() so the hot
   interval path needs only a multiply, rounding add and shift. */
#define AUTONAME_WAV_X8_Q_SHIFT 11U
#define AUTONAME_WAV_X8_Q_ROUND (1UL << (AUTONAME_WAV_X8_Q_SHIFT - 1U))

static bool autoname_enabled = false;
static bool autoname_found = false;
static char autoname_name[AUTONAME_NAME_BYTES + 1U];

/* WAV-only run accumulator. Packed source bytes are bit 0 first. */
static bool autoname_sample_active = false;
static uint8_t autoname_sample_level = 0U;
static uint16_t autoname_sample_run = 0U;

/* Metadata tracking supplies the optional AUTONAME record display. */
static bool metadata_active = false;
static file_format_t metadata_format = FILE_FORMAT_UNKNOWN;
static uint16_t metadata_wav_sample_rate = 0U;
static uint16_t metadata_wav_x8_q11 = 0U;
static bool metadata_loader_valid = false;
static loader_mode_t metadata_loader_mode = LOADER_MODE_NORMAL_1_1;
static bool metadata_payload_known = false;
static uint32_t metadata_payload_bytes = 0UL;
static uint32_t metadata_payload_received = 0UL;
static bool metadata_tc_pending = false;
static record_autoname_aux_profile_t metadata_aux_profile =
    RECORD_AUTONAME_AUX_PROFILE_NONE;

/* Lightweight single-polarity native-MZ leader probe used until AUX locks. */
static uint16_t mz_fast_reference_x8 = 0U;
static uint16_t mz_fast_stable_pulses = 0U;
static bool mz_fast_reference_checked = false;

static_assert(MZ_TC_LOADER_BYTES <= MZ_TAPE_HEADER_BYTES,
              "TC metadata scratch must fit an idle decoder header buffer");

/* One compact shared state: only the active branch occupies SRAM. */
typedef struct
{
    uint8_t count;
    uint16_t minimum_x8;
    uint16_t maximum_x8;
    uint16_t sum_x8;
} autoname_aux_probe_t;

typedef struct
{
    uint16_t pilot_x8;
    uint16_t pilot_count;
    uint8_t first_pulse_class;
    uint8_t bit_count;
    uint8_t byte_value;
    uint8_t byte_index;
    uint8_t xor_value;
} autoname_aux_sinclair_t;

typedef struct
{
    uint16_t short_x8;
    uint16_t leader_run;
    uint16_t checksum;
    uint8_t history;
    uint8_t history_count;
    uint8_t short_pending;
    uint8_t level;
    uint8_t bit_count;
    uint8_t byte_value;
    uint8_t byte_index;
    uint8_t checksum_low;
} autoname_aux_cmt_t;

typedef struct
{
    uint8_t state;
    union
    {
        autoname_aux_probe_t probe;
        autoname_aux_sinclair_t sinclair;
        autoname_aux_cmt_t cmt;
    } data;
} autoname_aux_decoder_t;

#ifdef __AVR__
static_assert(sizeof(autoname_aux_decoder_t) <= 16U,
              "AUX AutoName state exceeded 16 bytes SRAM");
#endif

static autoname_aux_decoder_t autoname_aux;

/* Title conversion is shared with the MZT selector so both paths interpret
   Sharp character codes, copier ASCII and CR termination identically. */
static bool autoname_sanitize_best(uint8_t raw_count)
{
    return mz_title_sanitize_filename(autoname_name, raw_count);
}

static bool autoname_sanitize_sinclair(uint8_t raw_count)
{
    return mz_title_sanitize_filename(autoname_name, raw_count);
}

static bool autoname_sanitize(void)
{
    return mz_title_sanitize_filename(autoname_name, AUTONAME_NAME_BYTES);
}

static uint16_t metadata_read_le16(const uint8_t *source)
{
    return (uint16_t)source[0] | ((uint16_t)source[1] << 8U);
}

static uint16_t metadata_difference_u16(uint16_t left, uint16_t right)
{
    return (left >= right) ? (uint16_t)(left - right) :
                             (uint16_t)(right - left);
}

static record_autoname_aux_profile_t
autoname_sinclair_profile_from_pilot(uint16_t pilot_x8)
{
    uint16_t d1 = metadata_difference_u16(pilot_x8, 306U);
    uint16_t d2 = metadata_difference_u16(pilot_x8, 233U);
    uint16_t d3 = metadata_difference_u16(pilot_x8, 200U);
    uint16_t d4 = metadata_difference_u16(pilot_x8, 168U);
    if ((d4 < d3) && (d4 < d2) && (d4 < d1))
        return RECORD_AUTONAME_AUX_PROFILE_SIN_1_4;
    if ((d3 < d2) && (d3 < d1))
        return RECORD_AUTONAME_AUX_PROFILE_SIN_1_3;
    if (d2 < d1)
        return RECORD_AUTONAME_AUX_PROFILE_SIN_1_2;
    return RECORD_AUTONAME_AUX_PROFILE_SIN_1_1;
}

static record_autoname_aux_profile_t
autoname_cpm_profile_from_short(uint16_t short_x8)
{
    uint16_t d1 = metadata_difference_u16(short_x8, 104U);
    uint16_t d2 = metadata_difference_u16(short_x8, 90U);
    uint16_t d3 = metadata_difference_u16(short_x8, 79U);
    uint16_t d4 = metadata_difference_u16(short_x8, 70U);
    if ((d4 < d3) && (d4 < d2) && (d4 < d1))
        return RECORD_AUTONAME_AUX_PROFILE_CPM_1_4;
    if ((d3 < d2) && (d3 < d1))
        return RECORD_AUTONAME_AUX_PROFILE_CPM_1_3;
    if (d2 < d1)
        return RECORD_AUTONAME_AUX_PROFILE_CPM_1_2;
    return RECORD_AUTONAME_AUX_PROFILE_CPM_1_1;
}

static bool autoname_aux_profile_is_sinclair(void)
{
    return (metadata_aux_profile >= RECORD_AUTONAME_AUX_PROFILE_SIN_1_1) &&
           (metadata_aux_profile <= RECORD_AUTONAME_AUX_PROFILE_SIN_1_4);
}

static bool autoname_aux_profile_is_cpm(void)
{
    return (metadata_aux_profile >= RECORD_AUTONAME_AUX_PROFILE_CPM_1_1) &&
           (metadata_aux_profile <= RECORD_AUTONAME_AUX_PROFILE_CPM_1_4);
}

static bool autoname_aux_payload_mode(void)
{
    return autoname_found && metadata_payload_known &&
           (metadata_aux_profile != RECORD_AUTONAME_AUX_PROFILE_NONE);
}

/* Convert a calibrated logical-HIGH/physical-LOW short interval to the common
   16 us * 8 reference.  This is shared by final speed classification and by
   the cheap fast-leader AUX lock test; the latter calls it once per run. */
static uint16_t metadata_normalize_mz_short_high_x8(uint16_t short_high_x8)
{
    uint32_t normalized = short_high_x8;

    if ((metadata_format == FILE_FORMAT_WAV) &&
        (metadata_wav_sample_rate != 0U))
    {
        normalized = ((uint32_t)short_high_x8 * 62500UL +
                      (metadata_wav_sample_rate / 2UL)) /
                     metadata_wav_sample_rate;
    }
    else if (metadata_format == FILE_FORMAT_LEP)
    {
        normalized = ((uint32_t)short_high_x8 * 50UL + 8UL) / 16UL;
    }

    return (normalized > 0xFFFFUL) ? 0xFFFFU : (uint16_t)normalized;
}

/* Convert the decoder's format-specific units to the same 16 us reference
   used by direct MZF recording, then classify the header pilot. */
static loader_mode_t metadata_loader_mode_from_tone(uint16_t short_high_x8,
                                                    uint16_t leader_pulses)
{
    uint16_t normalized =
        metadata_normalize_mz_short_high_x8(short_high_x8);
    uint16_t d1;
    uint16_t d2;
    uint16_t d3;
    uint16_t d4;

    /* x8 L16 reference for logical HIGH: Normal 1:1 ~=119..120,
       1:2 ~=57, 1:3 ~=44 and 1:4 ~=39. */
    d1 = metadata_difference_u16(normalized, 120U);
    d2 = metadata_difference_u16(normalized, 57U);
    d3 = metadata_difference_u16(normalized, 44U);
    d4 = metadata_difference_u16(normalized, 39U);
    if ((leader_pulses >= 8000U) && (leader_pulses <= 13000U) &&
        (d4 < d3) && (d4 < d2) && (d4 < d1))
    {
        return LOADER_MODE_NORMAL_1_4;
    }
    if ((d2 < d1) && (d2 <= d3)) return LOADER_MODE_NORMAL_1_2;
    if ((d3 < d1) && (d3 < d2)) return LOADER_MODE_NORMAL_1_3;
    return LOADER_MODE_NORMAL_1_1;
}

static bool metadata_loader_from_copier(mz_copier_profile_t profile,
                                        loader_mode_t *loader_mode)
{
    if (loader_mode == NULL) return false;
    switch (profile)
    {
        case MZ_COPIER_IC_1_4: *loader_mode = LOADER_MODE_IC_1_4; return true;
        case MZ_COPIER_IC_1_3: *loader_mode = LOADER_MODE_IC_1_3; return true;
        case MZ_COPIER_IC_1_2: *loader_mode = LOADER_MODE_IC_1_2; return true;
        case MZ_COPIER_TC_1_3: *loader_mode = LOADER_MODE_TC_1_3; return true;
        case MZ_COPIER_TC_1_2: *loader_mode = LOADER_MODE_TC_1_2; return true;
        case MZ_COPIER_NONE:
        default: return false;
    }
}

static void metadata_start_payload(uint32_t payload_bytes)
{
    metadata_payload_bytes = payload_bytes;
    metadata_payload_received = 0UL;
    metadata_payload_known = true;
    mz_tape_decoder_start_data(payload_bytes);
}

/* Normalize one physical level interval to L16-unit * 8.  Limiting the source
   value first avoids slow/wide arithmetic for silence while keeping all tape
   pulses of interest far inside the accepted range. */
static uint16_t autoname_interval_x8(uint16_t duration_units)
{
    if (duration_units == 0U) return 0U;
    if (duration_units > 1024U) return 0xFFFFU;

    if (metadata_format == FILE_FORMAT_WAV)
    {
        uint32_t value;
        if (metadata_wav_x8_q11 == 0U) return 0xFFFFU;
        value = (((uint32_t)duration_units * metadata_wav_x8_q11) +
                 AUTONAME_WAV_X8_Q_ROUND) >> AUTONAME_WAV_X8_Q_SHIFT;
        return (value > 0xFFFFUL) ? 0xFFFFU : (uint16_t)value;
    }
    if (metadata_format == FILE_FORMAT_LEP)
        return (uint16_t)(duration_units * 25U);
    return (uint16_t)(duration_units << 3U);
}

/* All AUX ratio limits are multiples of 5 percent and every AUX interval is
   capped at 640.  Compare value*20 against reference*(percent/5): the largest
   product is 640*54 = 34560, so the complete hot path stays 16-bit. */
static bool autoname_ratio_between_q5(uint16_t value, uint16_t reference,
                                      uint8_t low_q5, uint8_t high_q5)
{
    uint16_t scaled;
    uint16_t low;
    uint16_t high;

    if (reference == 0U) return false;
    scaled = (uint16_t)(value * 20U);
    low = (uint16_t)(reference * (uint16_t)low_q5);
    high = (uint16_t)(reference * (uint16_t)high_q5);
    return (scaled >= low) && (scaled <= high);
}

/* Integer-equivalent form of (7*old + sample + 4)/8 without a wide multiply. */
static uint16_t autoname_iir_eighth_u16(uint16_t old_value,
                                        uint16_t sample)
{
    if (sample >= old_value)
    {
        return (uint16_t)(old_value +
            (((uint16_t)(sample - old_value) + 4U) >> 3U));
    }
    return (uint16_t)(old_value -
        (((uint16_t)(old_value - sample) + 3U) >> 3U));
}

static void autoname_aux_reset_probe(void)
{
    autoname_aux.state = AUTONAME_AUX_PROBE;
    memset(&autoname_aux.data, 0, sizeof(autoname_aux.data));
    if (!autoname_found) autoname_name[0] = '\0';
}

static void autoname_aux_begin_sinclair(void)
{
    uint8_t count = autoname_aux.data.probe.count;
    uint16_t sum = autoname_aux.data.probe.sum_x8;
    uint16_t average = (count == 0U) ? 0U :
        (uint16_t)((sum + (count / 2U)) / count);

    memset(&autoname_aux.data, 0, sizeof(autoname_aux.data));
    autoname_aux.state = AUTONAME_AUX_SIN_PILOT;
    autoname_aux.data.sinclair.pilot_x8 = average;
    autoname_aux.data.sinclair.pilot_count = count;
    autoname_aux.data.sinclair.first_pulse_class =
        AUTONAME_SIN_FIRST_PULSE_NONE;
}

static void autoname_aux_begin_cmt(void)
{
    uint8_t count = autoname_aux.data.probe.count;
    uint16_t sum = autoname_aux.data.probe.sum_x8;
    uint16_t average = (count == 0U) ? 0U :
        (uint16_t)((sum + (count / 2U)) / count);
    uint16_t short_x8 = (uint16_t)((average * 2U + 1U) / 3U);

    memset(&autoname_aux.data, 0, sizeof(autoname_aux.data));
    autoname_aux.state = AUTONAME_AUX_CMT_LEADER;
    autoname_aux.data.cmt.short_x8 = short_x8;
}

static void autoname_aux_publish_header(record_autoname_aux_profile_t profile,
                                        uint16_t payload_bytes,
                                        bool sinclair_title)
{
    uint8_t raw_count = sinclair_title ? 10U : AUTONAME_NAME_BYTES;
    bool valid;

    for (uint8_t i = raw_count; i < AUTONAME_NAME_BYTES; ++i)
        autoname_name[i] = '\0';
    autoname_name[AUTONAME_NAME_BYTES] = '\0';

    valid = sinclair_title ?
        autoname_sanitize_sinclair(raw_count) :
        autoname_sanitize_best(raw_count);
    if (!valid)
    {
        autoname_aux_reset_probe();
        return;
    }

    autoname_found = true;
    metadata_loader_valid = false;
    metadata_tc_pending = false;
    metadata_aux_profile = profile;
    metadata_payload_bytes = payload_bytes;
    metadata_payload_received = 0UL;
    metadata_payload_known = true;

    mz_tape_decoder_stop();
    autoname_aux_reset_probe();
}

static void autoname_aux_sinclair_accept_byte(uint8_t value)
{
    autoname_aux_sinclair_t *state = &autoname_aux.data.sinclair;

    if (autoname_aux_payload_mode())
    {
        if (state->byte_index == 0U)
        {
            if (value != 0xFFU)
            {
                autoname_aux_reset_probe();
                return;
            }
            state->xor_value = value;
            state->byte_index = 1U;
            return;
        }
        if (metadata_payload_received < metadata_payload_bytes)
        {
            state->xor_value ^= value;
            metadata_payload_received++;
            return;
        }
        autoname_aux.state = AUTONAME_AUX_LOCKED;
        return;
    }

    {
        uint8_t index = state->byte_index;
        if ((index == 0U) && (value != 0x00U))
        {
            autoname_aux_reset_probe();
            return;
        }
        if ((index == 1U) && (value > 0x03U))
        {
            autoname_aux_reset_probe();
            return;
        }
        if ((index >= 2U) && (index <= 11U))
            autoname_name[index - 2U] = (char)value;

        if (index == 12U)
            state->pilot_count = value;
        else if (index == 13U)
            state->pilot_count |= (uint16_t)((uint16_t)value << 8U);

        if (index < 18U)
        {
            state->xor_value ^= value;
        }
        else
        {
            if (value == state->xor_value)
                autoname_aux_publish_header(
                    autoname_sinclair_profile_from_pilot(state->pilot_x8),
                    state->pilot_count, true);
            else
                autoname_aux_reset_probe();
            return;
        }
        state->byte_index++;
    }
}

static void autoname_aux_feed_sinclair_data(uint16_t interval_x8)
{
    autoname_aux_sinclair_t *state = &autoname_aux.data.sinclair;
    uint8_t pulse_class;

    if (!autoname_ratio_between_q5(interval_x8, state->pilot_x8, 4U, 22U))
    {
        autoname_aux_reset_probe();
        return;
    }
    pulse_class = ((uint16_t)(interval_x8 * 5U) <
                   (uint16_t)(state->pilot_x8 * 3U)) ? 0U : 1U;

    if (state->first_pulse_class == AUTONAME_SIN_FIRST_PULSE_NONE)
    {
        state->first_pulse_class = pulse_class;
        return;
    }
    if (pulse_class != state->first_pulse_class)
    {
        autoname_aux_reset_probe();
        return;
    }

    state->first_pulse_class = AUTONAME_SIN_FIRST_PULSE_NONE;
    state->byte_value = (uint8_t)((state->byte_value << 1U) | pulse_class);
    state->bit_count++;
    if (state->bit_count == 8U)
    {
        uint8_t decoded = state->byte_value;
        state->byte_value = 0U;
        state->bit_count = 0U;
        autoname_aux_sinclair_accept_byte(decoded);
    }
}

static void autoname_aux_cmt_accept_byte(uint8_t value)
{
    autoname_aux_cmt_t *state = &autoname_aux.data.cmt;

    if (autoname_aux_payload_mode())
    {
        if (state->byte_index == 0U)
        {
            if (value != 0x01U)
            {
                autoname_aux_reset_probe();
                return;
            }
            state->byte_index = 1U;
            return;
        }
        if (metadata_payload_received < metadata_payload_bytes)
        {
            state->checksum = (uint16_t)(state->checksum + value);
            metadata_payload_received++;
            return;
        }
        if (state->byte_index == 1U)
        {
            state->checksum_low = value;
            state->byte_index = 2U;
            return;
        }
        autoname_aux.state = AUTONAME_AUX_LOCKED;
        return;
    }

    {
        uint8_t index = state->byte_index;
        if ((index == 0U) && (value != 0x00U))
        {
            autoname_aux_reset_probe();
            return;
        }
        if ((index == 1U) && (value != 0x00U) &&
            (value != 0x01U) && (value != 0x76U))
        {
            autoname_aux_reset_probe();
            return;
        }
        if (index == 1U)
        {
            /* InterCopy CPM/CMT can wrap a Sinclair-style header with subtype
               $00.  history is no longer needed after CMT leader sync, so keep
               the subtype there without increasing the AVR state size. */
            state->history = value;
        }
        if ((index >= 2U) &&
            (((state->history == 0x00U) && (index <= 11U)) ||
             ((state->history != 0x00U) && (index <= 18U))))
        {
            autoname_name[index - 2U] = (char)value;
        }

        if (index == 19U)
            state->leader_run = value;
        else if (index == 20U)
            state->leader_run |= (uint16_t)((uint16_t)value << 8U);

        if ((index >= 1U) && (index <= 128U))
            state->checksum = (uint16_t)(state->checksum + value);
        else if (index == 129U)
            state->checksum_low = value;
        else if (index == 130U)
        {
            uint16_t recorded = (uint16_t)state->checksum_low |
                                ((uint16_t)value << 8U);
            if (recorded == state->checksum)
                autoname_aux_publish_header(
                    autoname_cpm_profile_from_short(state->short_x8),
                    state->leader_run, state->history == 0x00U);
            else
                autoname_aux_reset_probe();
            return;
        }
        state->byte_index++;
    }
}

static void autoname_aux_cmt_accept_symbol(uint8_t symbol)
{
    autoname_aux_cmt_t *state = &autoname_aux.data.cmt;

    if (autoname_aux.state == AUTONAME_AUX_CMT_LEADER)
    {
        if (state->history_count < 3U)
        {
            state->history = (uint8_t)(((state->history << 1U) | symbol) & 0x07U);
            state->history_count++;
            if (state->history_count == 3U) state->leader_run = 3U;
            return;
        }

        {
            uint8_t oldest = (uint8_t)((state->history >> 2U) & 1U);
            state->history = (uint8_t)(((state->history << 1U) | symbol) & 0x07U);
            if (symbol == oldest)
            {
                if (state->leader_run != 0xFFFFU) state->leader_run++;
                return;
            }
        }

        if (state->leader_run >= AUTONAME_CMT_MIN_LEADER_SYMBOLS)
        {
            /* The first period-3 mismatch is the CM/CMT sync symbol.  It is
               intentionally discarded; the following symbol is bit 0. */
            state->level = 0U;
            state->bit_count = 0U;
            state->byte_value = 0U;
            state->byte_index = 0U;
            state->leader_run = 0U;
            state->checksum = 0U;
            state->checksum_low = 0U;
            autoname_aux.state = AUTONAME_AUX_CMT_DATA;
            return;
        }

        /* Keep searching without allocating/replaying a leader window. */
        state->leader_run = 3U;
        return;
    }

    /* CM/CMT symbol is one LONG interval or a pair of SHORT intervals.  LONG
       toggles the NRZI level, SHORT-pair keeps it.  The resulting level is the
       data bit inverted; bytes are LSB first. */
    if (symbol != 0U) state->level ^= 1U;
    if ((state->level ^ 1U) != 0U)
        state->byte_value |= (uint8_t)(1U << state->bit_count);
    state->bit_count++;
    if (state->bit_count == 8U)
    {
        uint8_t value = state->byte_value;
        state->byte_value = 0U;
        state->bit_count = 0U;
        autoname_aux_cmt_accept_byte(value);
    }
}

static void autoname_aux_feed_cmt_interval(uint16_t interval_x8)
{
    autoname_aux_cmt_t *state = &autoname_aux.data.cmt;
    uint8_t symbol;

    if (autoname_ratio_between_q5(interval_x8, state->short_x8, 12U, 29U))
    {
        state->short_x8 =
            autoname_iir_eighth_u16(state->short_x8, interval_x8);
        if (state->short_pending == 0U)
        {
            state->short_pending = 1U;
            return;
        }
        state->short_pending = 0U;
        symbol = 0U; /* two shorts */
    }
    else if (autoname_ratio_between_q5(interval_x8, state->short_x8,
                                       29U, 54U))
    {
        uint16_t half = (uint16_t)((interval_x8 + 1U) / 2U);
        state->short_x8 =
            autoname_iir_eighth_u16(state->short_x8, half);
        /* If probing entered halfway through a short pair, drop that orphan
           short and use this long interval to regain symbol alignment. */
        state->short_pending = 0U;
        symbol = 1U; /* one long */
    }
    else
    {
        autoname_aux_reset_probe();
        return;
    }

    autoname_aux_cmt_accept_symbol(symbol);
}

static void autoname_aux_feed(uint16_t duration_units)
{
    uint16_t interval_x8;

    if (!metadata_active ||
        (autoname_aux.state == AUTONAME_AUX_NONE) ||
        (autoname_aux.state == AUTONAME_AUX_LOCKED))
        return;

    interval_x8 = autoname_interval_x8(duration_units);
    if ((interval_x8 == 0U) ||
        (interval_x8 > AUTONAME_AUX_MAX_INTERVAL_X8))
    {
        autoname_aux_reset_probe();
        return;
    }

    if (autoname_aux.state == AUTONAME_AUX_PROBE)
    {
        autoname_aux_probe_t *probe = &autoname_aux.data.probe;
        if (probe->count == 0U)
        {
            probe->minimum_x8 = interval_x8;
            probe->maximum_x8 = interval_x8;
        }
        else
        {
            if (interval_x8 < probe->minimum_x8) probe->minimum_x8 = interval_x8;
            if (interval_x8 > probe->maximum_x8) probe->maximum_x8 = interval_x8;
        }
        probe->sum_x8 += interval_x8;
        probe->count++;

        if (probe->count >= AUTONAME_AUX_PROBE_INTERVALS)
        {
            uint16_t average = (uint16_t)
                ((probe->sum_x8 + (probe->count / 2U)) / probe->count);
            bool looks_sinclair =
                autoname_ratio_between_q5(probe->minimum_x8, average, 15U, 22U) &&
                autoname_ratio_between_q5(probe->maximum_x8, average, 18U, 26U);
            bool looks_cmt =
                autoname_ratio_between_q5(probe->minimum_x8, average, 9U, 17U) &&
                autoname_ratio_between_q5(probe->maximum_x8, average, 24U, 36U);

            if (autoname_aux_profile_is_sinclair())
            {
                if (looks_sinclair) autoname_aux_begin_sinclair();
                else autoname_aux_reset_probe();
            }
            else if (autoname_aux_profile_is_cpm())
            {
                if (looks_cmt) autoname_aux_begin_cmt();
                else autoname_aux_reset_probe();
            }
            else if (looks_sinclair)
                autoname_aux_begin_sinclair();
            else if (looks_cmt)
                autoname_aux_begin_cmt();
            else
                autoname_aux_reset_probe();
        }
        return;
    }

    if (autoname_aux.state == AUTONAME_AUX_SIN_PILOT)
    {
        autoname_aux_sinclair_t *state = &autoname_aux.data.sinclair;
        if (autoname_ratio_between_q5(interval_x8, state->pilot_x8, 15U, 25U))
        {
            state->pilot_x8 =
                autoname_iir_eighth_u16(state->pilot_x8, interval_x8);
            if (state->pilot_count != 0xFFFFU) state->pilot_count++;
            return;
        }
        if ((state->pilot_count >= AUTONAME_SINCLAIR_MIN_PILOT) &&
            autoname_ratio_between_q5(interval_x8, state->pilot_x8, 2U, 12U))
        {
            autoname_aux.state = AUTONAME_AUX_SIN_SYNC2;
            return;
        }
        autoname_aux_reset_probe();
        return;
    }

    if (autoname_aux.state == AUTONAME_AUX_SIN_SYNC2)
    {
        autoname_aux_sinclair_t *state = &autoname_aux.data.sinclair;
        if (!autoname_ratio_between_q5(interval_x8, state->pilot_x8, 2U, 13U))
        {
            autoname_aux_reset_probe();
            return;
        }
        state->first_pulse_class = AUTONAME_SIN_FIRST_PULSE_NONE;
        state->bit_count = 0U;
        state->byte_value = 0U;
        state->byte_index = 0U;
        state->xor_value = 0U;
        state->pilot_count = 0U;
        autoname_aux.state = AUTONAME_AUX_SIN_DATA;
        return;
    }

    if (autoname_aux.state == AUTONAME_AUX_SIN_DATA)
    {
        autoname_aux_feed_sinclair_data(interval_x8);
        return;
    }

    if ((autoname_aux.state == AUTONAME_AUX_CMT_LEADER) ||
        (autoname_aux.state == AUTONAME_AUX_CMT_DATA))
    {
        autoname_aux_feed_cmt_interval(interval_x8);
        return;
    }

    autoname_aux_reset_probe();
}

void record_autoname_accept_header(const uint8_t *header)
{
    if (!autoname_enabled || autoname_found || (header == NULL)) return;
    memcpy(autoname_name, &header[1], AUTONAME_NAME_BYTES);
    autoname_name[AUTONAME_NAME_BYTES] = '\0';
    autoname_found = autoname_sanitize();
    if (autoname_found) autoname_aux.state = AUTONAME_AUX_LOCKED;
}

static void autoname_take_decoder_event(void)
{
    mz_tape_decoder_event_t event;
    while (mz_tape_decoder_take_event(&event))
    {
        if (event.type == MZ_TAPE_DECODER_EVENT_HEADER_VALID)
        {
            const uint8_t *header = mz_tape_decoder_get_header();
            mz_copier_profile_t copier_profile;

            record_autoname_accept_header(header);
            metadata_loader_valid = false;
            metadata_payload_known = false;
            metadata_tc_pending = false;
            metadata_aux_profile = RECORD_AUTONAME_AUX_PROFILE_NONE;

            if (mz_loader_profile_detect_ic(header, &copier_profile))
            {
                metadata_loader_valid = metadata_loader_from_copier(
                    copier_profile, &metadata_loader_mode);
                metadata_start_payload(metadata_read_le16(header + 0x1AU));
            }
            else if (mz_loader_profile_recognize_tc_header(header))
            {
                metadata_tc_pending = true;
                mz_tape_decoder_start_data(MZ_TC_LOADER_BYTES);
            }
            else
            {
                metadata_loader_mode = metadata_loader_mode_from_tone(
                    mz_tape_decoder_get_header_short_high_x8(),
                    event.leader_pulses);
                metadata_loader_valid = true;
                metadata_start_payload(metadata_read_le16(header + 0x12U));
            }
        }
        else if (event.type == MZ_TAPE_DECODER_EVENT_DATA_BYTE)
        {
            if (metadata_tc_pending)
            {
                uint8_t *metadata_tc_loader =
                    mz_tape_decoder_get_data_scratch();
                if (metadata_tc_loader == NULL)
                {
                    metadata_tc_pending = false;
                    metadata_loader_valid = false;
                    metadata_payload_known = false;
                    continue;
                }
                if (event.byte_index < MZ_TC_LOADER_BYTES)
                    metadata_tc_loader[event.byte_index] = event.value;
            }
            else if (metadata_payload_known)
            {
                uint32_t received = event.byte_index + 1UL;
                metadata_payload_received =
                    (received > metadata_payload_bytes) ?
                        metadata_payload_bytes : received;
            }
        }
        else if (event.type == MZ_TAPE_DECODER_EVENT_BLOCK_VALID)
        {
            if (metadata_tc_pending)
            {
                mz_copier_profile_t copier_profile;
                const uint8_t *header = mz_tape_decoder_get_header();
                uint8_t *metadata_tc_loader =
                    mz_tape_decoder_get_data_scratch();

                metadata_tc_pending = false;
                if ((metadata_tc_loader != NULL) &&
                    mz_loader_profile_detect_tc(header, metadata_tc_loader,
                                                &copier_profile))
                {
                    metadata_loader_valid = metadata_loader_from_copier(
                        copier_profile, &metadata_loader_mode);
                    metadata_start_payload(metadata_read_le16(
                        metadata_tc_loader + 0x4DU));
                }
                else
                {
                    metadata_loader_valid = false;
                    metadata_payload_known = false;
                }
            }
            else if (metadata_payload_known)
            {
                metadata_payload_received = metadata_payload_bytes;
            }
        }
        else if (event.type == MZ_TAPE_DECODER_EVENT_BLOCK_INVALID)
        {
            if (metadata_tc_pending)
            {
                metadata_tc_pending = false;
                metadata_loader_valid = false;
                metadata_payload_known = false;
            }
            else if (metadata_payload_known)
            {
                /* The physical payload reached its declared end even when
                   its checksum did not validate. */
                metadata_payload_received = metadata_payload_bytes;
            }
        }
    }
}

static void autoname_mz_fast_probe_reset(void)
{
    mz_fast_reference_x8 = 0U;
    mz_fast_stable_pulses = 0U;
    mz_fast_reference_checked = false;
}

static void autoname_mz_fast_probe_feed(uint16_t duration_units, uint8_t level)
{
    uint16_t pulse_x8;
    uint16_t difference;
    uint16_t tolerance;

    if (autoname_found ||
        (autoname_aux.state == AUTONAME_AUX_NONE) ||
        (autoname_aux.state == AUTONAME_AUX_LOCKED))
    {
        return;
    }

    level = level ? 1U : 0U;
    if ((duration_units == 0U) || (duration_units > 1024U))
    {
        autoname_mz_fast_probe_reset();
        return;
    }
    /* Physical WRITE LOW is the only receiver-relevant Sharp interval. */
    if (level != 0U) return;
    pulse_x8 = (uint16_t)(duration_units * 8U);

    if (mz_fast_reference_x8 == 0U)
    {
        mz_fast_reference_x8 = pulse_x8;
        mz_fast_stable_pulses = 1U;
        mz_fast_reference_checked = false;
        return;
    }

    difference = metadata_difference_u16(pulse_x8, mz_fast_reference_x8);
    tolerance = (uint16_t)(mz_fast_reference_x8 / 4U); /* +/-25 % */
    if (tolerance < 8U) tolerance = 8U;

    if (difference > tolerance)
    {
        mz_fast_reference_x8 = pulse_x8;
        mz_fast_stable_pulses = 1U;
        mz_fast_reference_checked = false;
        return;
    }

    /* 1/8 IIR without a wide multiply: reference += (sample-reference)/8. */
    if (pulse_x8 >= mz_fast_reference_x8)
    {
        mz_fast_reference_x8 = (uint16_t)
            (mz_fast_reference_x8 +
             ((uint16_t)(pulse_x8 - mz_fast_reference_x8) + 4U) / 8U);
    }
    else
    {
        mz_fast_reference_x8 = (uint16_t)
            (mz_fast_reference_x8 -
             ((uint16_t)(mz_fast_reference_x8 - pulse_x8) + 4U) / 8U);
    }

    if (mz_fast_stable_pulses != 0xFFFFU) mz_fast_stable_pulses++;
    if ((mz_fast_stable_pulses >= AUTONAME_MZ_FAST_LOCK_PULSES) &&
        !mz_fast_reference_checked)
    {
        uint16_t normalized =
            metadata_normalize_mz_short_high_x8(mz_fast_reference_x8);

        mz_fast_reference_checked = true;

        /* CM/CMT (including InterCopy CP/M subtype $00) can form a periodic
           leader whose paired intervals resemble a fast native MZ leader.
           Once AUX has already positively entered CMT leader/data decoding,
           do not let the generic MZ fast probe kill that decoder. Native fast
           MZ does not enter the CMT state, so its CPU-saving lock is unchanged. */
        if ((autoname_aux.state == AUTONAME_AUX_CMT_LEADER) ||
            (autoname_aux.state == AUTONAME_AUX_CMT_DATA))
        {
            return;
        }

        if ((normalized >= AUTONAME_MZ_FAST_MIN_X8) &&
            (normalized <= AUTONAME_MZ_FAST_MAX_X8))
        {
            /* A stable Sharp-MZ leader makes AUX protocols irrelevant for
               the rest of this recording, so keep AUX locked across later gaps. */
            autoname_aux.state = AUTONAME_AUX_LOCKED;
        }
    }
}

void record_autoname_feed_level_interval(uint16_t duration_units,
                                         uint8_t level)
{
    if (!metadata_active) return;

    /* Feed the Sharp-MZ decoder first. During a long leader there is no event
       to consume, so avoid an empty take_event() call on every interval. */
    if (mz_tape_decoder_feed_interval(duration_units, level))
        autoname_take_decoder_event();

    /* Once a fast Sharp-MZ leader locks AUX out, skip both AUX probe paths.
       While AUX remains a candidate, run the fast-MZ probe first so a lock on
       the current interval prevents unnecessary secondary decoding. */
    if ((autoname_aux.state != AUTONAME_AUX_NONE) &&
        (autoname_aux.state != AUTONAME_AUX_LOCKED))
    {
        autoname_mz_fast_probe_feed(duration_units, level);
        if (autoname_aux.state != AUTONAME_AUX_LOCKED)
            autoname_aux_feed(duration_units);
    }
}

/* WAV packed-sample hot path after the Sharp-MZ probe locks AUX out.
   At that point the generic wrapper would only feed the native decoder and
   recheck a known LOCKED state. Bypass that branch while preserving decoder
   events; direct interval callers continue through the public path. */
static inline __attribute__((always_inline))
void autoname_feed_packed_interval(uint16_t duration_units, uint8_t level)
{
    if (autoname_aux.state == AUTONAME_AUX_LOCKED)
    {
        if (mz_tape_decoder_feed_interval(duration_units, level))
            autoname_take_decoder_event();
        return;
    }

    record_autoname_feed_level_interval(duration_units, level);
}


void record_autoname_begin(bool enabled, file_format_t format,
                           uint32_t wav_sample_rate)
{
    autoname_enabled = enabled;
    autoname_found = false;
    autoname_sample_active = false;
    autoname_sample_level = 0U;
    autoname_sample_run = 0U;
    autoname_name[0] = '\0';
    metadata_format = format;
    metadata_wav_sample_rate =
        (wav_sample_rate <= 0xFFFFUL) ? (uint16_t)wav_sample_rate : 0U;
    metadata_wav_x8_q11 = 0U;
    if ((format == FILE_FORMAT_WAV) && (metadata_wav_sample_rate != 0U))
    {
        uint32_t coefficient =
            (((500000UL << AUTONAME_WAV_X8_Q_SHIFT) +
              ((uint32_t)metadata_wav_sample_rate / 2UL)) /
             (uint32_t)metadata_wav_sample_rate);
        if (coefficient <= 0xFFFFUL)
            metadata_wav_x8_q11 = (uint16_t)coefficient;
    }
    metadata_active = enabled &&
                      ((format == FILE_FORMAT_WAV) ||
                       (format == FILE_FORMAT_LEP) ||
                       (format == FILE_FORMAT_L16));
    metadata_loader_valid = false;
    metadata_payload_known = false;
    metadata_payload_bytes = 0UL;
    metadata_payload_received = 0UL;
    metadata_tc_pending = false;
    metadata_aux_profile = RECORD_AUTONAME_AUX_PROFILE_NONE;
    autoname_aux_reset_probe();
    autoname_mz_fast_probe_reset();
    if (metadata_active) mz_tape_decoder_begin_header();
    else
    {
        autoname_aux.state = AUTONAME_AUX_NONE;
        mz_tape_decoder_stop();
    }
}

void record_autoname_break_signal(void)
{
    if (!metadata_active) return;
    autoname_sample_active = false;
    autoname_sample_run = 0U;
    mz_tape_decoder_break_signal();
    if (autoname_aux.state != AUTONAME_AUX_LOCKED)
    {
        autoname_aux_reset_probe();
        autoname_mz_fast_probe_reset();
    }
}

static inline void autoname_sample_run_add(uint8_t count)
{
    uint16_t room = (uint16_t)(0xFFFFU - autoname_sample_run);
    if ((uint16_t)count > room) autoname_sample_run = 0xFFFFU;
    else autoname_sample_run = (uint16_t)(autoname_sample_run + count);
}

/* Return the index (0..7) of the lowest set bit.  The caller guarantees that
   value is nonzero.  This is a generic bit operation, not a pulse-length
   lookup: it only locates transition positions inside one packed WAV byte. */
static inline uint8_t autoname_lowest_set_bit_index(uint8_t value)
{
    uint8_t index = 0U;

    if ((value & 0x0FU) == 0U)
    {
        value >>= 4U;
        index = 4U;
    }
    if ((value & 0x03U) == 0U)
    {
        value >>= 2U;
        index = (uint8_t)(index + 2U);
    }
    if ((value & 0x01U) == 0U) index++;
    return index;
}

void record_autoname_feed_packed_samples(uint8_t packed, uint8_t valid_bits)
{
    uint8_t first_level;
    uint8_t transitions;
    uint8_t consumed = 0U;

    if (!metadata_active) return;
    if (valid_bits > 8U) valid_bits = 8U;
    if (valid_bits == 0U) return;

    first_level = (packed & 0x01U) ? 1U : 0U;

    /* A level change between packed bytes closes the accumulated run before
       bit 0 of the new byte is processed. */
    if (!autoname_sample_active)
    {
        autoname_sample_active = true;
        autoname_sample_level = first_level;
        autoname_sample_run = 0U;
    }
    else if (first_level != autoname_sample_level)
    {
        autoname_feed_packed_interval(autoname_sample_run,
                                      autoname_sample_level);
        autoname_sample_level = first_level;
        autoname_sample_run = 0U;
    }

    /* Bit i is set when sample i differs from sample i+1. Packed WAV bytes
       are therefore processed once per physical edge rather than once per
       sample; long constant runs need no extra buffer and very little CPU. */
    if (valid_bits > 1U)
    {
        uint8_t transition_mask =
            (uint8_t)((1U << (valid_bits - 1U)) - 1U);
        transitions =
            (uint8_t)((packed ^ (uint8_t)(packed >> 1U)) & transition_mask);
    }
    else
    {
        transitions = 0U;
    }

    while (transitions != 0U)
    {
        uint8_t transition_bit = autoname_lowest_set_bit_index(transitions);
        uint8_t run_end = (uint8_t)(transition_bit + 1U);

        autoname_sample_run_add((uint8_t)(run_end - consumed));
        autoname_feed_packed_interval(autoname_sample_run,
                                      autoname_sample_level);
        autoname_sample_level ^= 1U;
        autoname_sample_run = 0U;
        consumed = run_end;
        transitions &= (uint8_t)(transitions - 1U);
    }

    autoname_sample_run_add((uint8_t)(valid_bits - consumed));
}

bool record_autoname_has_name(void)
{
    return autoname_enabled && autoname_found;
}

const char *record_autoname_get_name(void)
{
    return record_autoname_has_name() ? autoname_name : NULL;
}

bool record_autoname_get_detected_loader_mode(loader_mode_t *loader_mode)
{
    if (!metadata_loader_valid || (loader_mode == NULL)) return false;
    *loader_mode = metadata_loader_mode;
    return true;
}

bool record_autoname_get_detected_aux_profile(
    record_autoname_aux_profile_t *profile)
{
    if ((profile == NULL) ||
        (metadata_aux_profile == RECORD_AUTONAME_AUX_PROFILE_NONE))
        return false;
    *profile = metadata_aux_profile;
    return true;
}

bool record_autoname_get_payload_progress_percent(uint8_t *percent)
{
    uint32_t value;

    if (!metadata_payload_known || (percent == NULL)) return false;
    if (metadata_payload_bytes == 0UL)
    {
        *percent = 100U;
        return true;
    }
    value = (metadata_payload_received * 100UL) / metadata_payload_bytes;
    if (value > 100UL) value = 100UL;
    *percent = (uint8_t)value;
    return true;
}

static int autoname_make_path(char *destination,
                              const char *directory_path,
                              file_format_t format,
                              uint8_t suffix)
{
    if (suffix == 0U)
    {
        if (format == FILE_FORMAT_WAV)
            return flash_text_snprintf(destination, RECORD_PATH_BUFFER_MAX,
                                       PSTR("%s/%s.WAV"), directory_path,
                                       autoname_name);
        if (format == FILE_FORMAT_L16)
            return flash_text_snprintf(destination, RECORD_PATH_BUFFER_MAX,
                                       PSTR("%s/%s.L16"), directory_path,
                                       autoname_name);
        if (format == FILE_FORMAT_MZF)
            return flash_text_snprintf(destination, RECORD_PATH_BUFFER_MAX,
                                       PSTR("%s/%s.MZF"), directory_path,
                                       autoname_name);
        return flash_text_snprintf(destination, RECORD_PATH_BUFFER_MAX,
                                   PSTR("%s/%s.LEP"), directory_path,
                                   autoname_name);
    }

    if (format == FILE_FORMAT_WAV)
        return flash_text_snprintf(destination, RECORD_PATH_BUFFER_MAX,
                                   PSTR("%s/%s_%02u.WAV"), directory_path,
                                   autoname_name, (unsigned int)suffix);
    if (format == FILE_FORMAT_L16)
        return flash_text_snprintf(destination, RECORD_PATH_BUFFER_MAX,
                                   PSTR("%s/%s_%02u.L16"), directory_path,
                                   autoname_name, (unsigned int)suffix);
    if (format == FILE_FORMAT_MZF)
        return flash_text_snprintf(destination, RECORD_PATH_BUFFER_MAX,
                                   PSTR("%s/%s_%02u.MZF"), directory_path,
                                   autoname_name, (unsigned int)suffix);
    return flash_text_snprintf(destination, RECORD_PATH_BUFFER_MAX,
                               PSTR("%s/%s_%02u.LEP"), directory_path,
                               autoname_name, (unsigned int)suffix);
}

bool record_autoname_apply(const char *directory_path, file_format_t format)
{
    char *target = (char *)wav_sample_stream_get_shared_work_buffer();
    uint8_t suffix;

    if (!record_autoname_has_name() || (directory_path == NULL) ||
        ((format != FILE_FORMAT_WAV) && (format != FILE_FORMAT_L16) &&
         (format != FILE_FORMAT_LEP) && (format != FILE_FORMAT_MZF)) ||
        sdcard_file_is_open())
    {
        return false;
    }

    for (suffix = 0U; suffix <= AUTONAME_MAX_SUFFIX; ++suffix)
    {
        int length = autoname_make_path(target, directory_path, format, suffix);

        if ((length <= 0) || (length >= (int)RECORD_PATH_BUFFER_MAX))
            return false;
        if (strcmp(target, record_path_buffer) == 0) return true;
        if (sdcard_file_exists(target)) continue;
        if (!sdcard_file_rename(record_path_buffer, target)) return false;

        /* MZF sidecar is metadata only. Failure to relocate it never rolls
           back or invalidates the already successful canonical MZF rename. */
        if (format == FILE_FORMAT_MZF)
            (void)mzi_sidecar_relocate_for_mzf(record_path_buffer, target);

        strncpy(record_path_buffer, target, RECORD_PATH_BUFFER_MAX - 1U);
        record_path_buffer[RECORD_PATH_BUFFER_MAX - 1U] = '\0';
        return true;
    }

    return false;
}
