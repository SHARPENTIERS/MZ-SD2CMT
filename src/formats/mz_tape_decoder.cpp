#include "mz_tape_decoder.h"

#include <stddef.h>

#define MIN_LEADER_PULSES 16U
#define MIN_MARK_PULSES 12U
#define MAX_MARK_PULSES 48U
#define FINAL_MARK_PULSES 2U
#define MAX_HALF_UNITS 1024U

/* Common-path limits for the exact 16-bit short/long threshold.  Real Sharp
   tape pulses are far below these values; the wide fallback only protects
   malformed/extreme input without changing the original decision boundary. */
#define CLASSIFY_16_SCALED_MAX 3276U
#define CLASSIFY_16_AVERAGE_MAX 2259U

/* Exact 8-bit hot path for real MZ header leaders.  With average <= 204,
   average + floor(average/4) is <= 255.  A duration > 31 has scaled=x8 >=256
   and therefore cannot satisfy either the +/-25% window or difference<=8. */
#define LEADER_8_AVERAGE_MAX 204U
#define LEADER_8_DURATION_MAX 31U
#define LEADER_LOCK_PULSES 256U

/* A valid Sharp mark starts with at least MIN_MARK_PULSES LONG pulses.  Sample
   only the first few while the decoder is already outside the leader hot path.
   This makes LONG timing available for profile detection without adding work
   to the locked 256-pulse leader path or to the data stream. */
#define HEADER_LONG_SAMPLE_LIMIT 16U

typedef enum
{
    DECODE_SEARCH_LEADER = 0,
    DECODE_MARK_LONG,
    DECODE_MARK_SHORT,
    DECODE_MARK_FINAL,
    DECODE_DATA,
    DECODE_DUPLICATE_GAP
} decode_state_t;

typedef enum
{
    DECODER_MODE_STOPPED = 0,
    DECODER_MODE_HEADER,
    DECODER_MODE_DATA
} decoder_mode_t;

typedef struct
{
    decode_state_t state;
    uint16_t short_x8;
    uint16_t leader_pulses;
    /* In DECODE_SEARCH_LEADER these two otherwise-idle bytes are reused after
       LEADER_LOCK_PULSES as frozen min/max duration bounds.  Once state leaves
       SEARCH_LEADER they resume their original mark/final counter roles. */
    uint8_t mark_pulses;
    uint8_t final_pulses;
    uint8_t bit_count;
    uint8_t byte_value;
    uint32_t byte_index;
    uint32_t expected_bytes;
    uint16_t checksum;
    uint16_t recorded_checksum;
    uint8_t copy_index;
} pulse_decoder_t;

static decoder_mode_t decoder_mode = DECODER_MODE_STOPPED;
static pulse_decoder_t decoder;
static uint8_t header_buffer[MZ_TAPE_HEADER_BYTES];
static uint8_t data_scratch[MZ_TAPE_HEADER_BYTES];
static const uint8_t *validated_header = NULL;
static uint16_t validated_short_x8 = 0U;
static uint16_t validated_long_x8 = 0U;
static mz_tape_decoder_event_t pending_event;

static uint8_t popcount8(uint8_t value)
{
    uint8_t count = 0U;
    while (value != 0U)
    {
        count = (uint8_t)(count + (value & 1U));
        value >>= 1U;
    }
    return count;
}

static void reset_decoder(pulse_decoder_t *decoder, uint16_t seed_units)
{
    if (decoder == NULL) return;
    if (decoder_mode == DECODER_MODE_HEADER)
        validated_long_x8 = 0U;
    decoder->state = DECODE_SEARCH_LEADER;
    decoder->short_x8 = (seed_units <= MAX_HALF_UNITS) ?
        (uint16_t)(seed_units * 8U) : 0U;
    decoder->leader_pulses = (decoder->short_x8 != 0U) ? 1U : 0U;
    decoder->mark_pulses = 0U;
    decoder->final_pulses = 0U;
    decoder->bit_count = 0U;
    decoder->byte_value = 0U;
    decoder->byte_index = 0UL;
    decoder->checksum = 0U;
    decoder->recorded_checksum = 0U;
    decoder->copy_index = 0U;
}

/* Test and adapt one leader pulse in a single pass.  The old acceptance rule
   was:

       scaled inside average +/- floor(average/4), OR abs(scaled-average) <= 8

   which is exactly the same as:

       abs(scaled-average) <= max(floor(average/4), 8)

   Using one difference and one tolerance removes low/high arithmetic and extra
   comparisons without changing a single decision or the exact 1/8 IIR update.

   Real Normal leaders use the 8-bit branch: short_x8 <= 204 and decisive
   logical-HIGH duration <= 31 units. Malformed, slow or otherwise wider input
   falls back to the exact 16-bit path. */
static inline __attribute__((always_inline))
bool accept_leader_pulse(pulse_decoder_t *decoder, uint16_t duration_units)
{
    uint16_t average16;

    if ((decoder == NULL) || (decoder->short_x8 == 0U)) return false;
    average16 = decoder->short_x8;

    if (average16 <= LEADER_8_AVERAGE_MAX)
    {
        uint8_t average;
        uint8_t scaled;
        uint8_t difference;
        uint8_t tolerance;

        /* With average<=204 the largest accepted scaled value is <=255.
           duration>31 therefore cannot satisfy the exact acceptance rule. */
        if (duration_units > LEADER_8_DURATION_MAX) return false;

        average = (uint8_t)average16;
        scaled = (uint8_t)((uint8_t)duration_units << 3U);
        difference = (scaled >= average) ?
            (uint8_t)(scaled - average) : (uint8_t)(average - scaled);
        tolerance = (uint8_t)(average >> 2U);
        if (tolerance < 8U) tolerance = 8U;
        if (difference > tolerance) return false;

        if (scaled >= average)
        {
            decoder->short_x8 = (uint16_t)
                (uint8_t)(average + (uint8_t)((difference + 4U) >> 3U));
        }
        else
        {
            decoder->short_x8 = (uint16_t)
                (uint8_t)(average - (uint8_t)((difference + 3U) >> 3U));
        }
        return true;
    }

    {
        uint16_t scaled = (uint16_t)(duration_units << 3U);
        uint16_t difference = (scaled >= average16) ?
            (uint16_t)(scaled - average16) :
            (uint16_t)(average16 - scaled);
        uint16_t tolerance = (uint16_t)(average16 >> 2U);

        if (tolerance < 8U) tolerance = 8U;
        if (difference > tolerance) return false;

        if (scaled >= average16)
        {
            decoder->short_x8 = (uint16_t)
                (average16 + ((difference + 4U) >> 3U));
        }
        else
        {
            decoder->short_x8 = (uint16_t)
                (average16 - ((difference + 3U) >> 3U));
        }
        return true;
    }
}

/* Freeze the already-calibrated 8-bit leader reference after 256 accepted
   pulses.  The exact frozen acceptance condition is converted once from x8
   units to integer duration_units:

       ceil((average-tolerance)/8) <= duration <=
       floor((average+tolerance)/8)

   Because duration*8 is the only value the adaptive path ever tests, these
   integer bounds are exactly equivalent for a frozen average.  The bounds reuse
   mark_pulses/final_pulses while the state is SEARCH_LEADER, so this optimization
   costs 0 bytes of additional SRAM.  No lookup table or pulse dictionary is used. */
static inline __attribute__((always_inline))
void lock_leader_window(pulse_decoder_t *decoder)
{
    uint8_t average;
    uint8_t tolerance;
    uint8_t low_scaled;
    uint8_t high_scaled;

    if ((decoder == NULL) ||
        (decoder->final_pulses != 0U) ||
        (decoder->short_x8 == 0U) ||
        (decoder->short_x8 > LEADER_8_AVERAGE_MAX))
    {
        return;
    }

    average = (uint8_t)decoder->short_x8;
    tolerance = (uint8_t)(average >> 2U);
    if (tolerance < 8U) tolerance = 8U;

    low_scaled = (tolerance >= average) ? 0U :
        (uint8_t)(average - tolerance);
    high_scaled = (uint8_t)(average + tolerance);

    decoder->mark_pulses = (uint8_t)((low_scaled + 7U) >> 3U);
    decoder->final_pulses = (uint8_t)(high_scaled >> 3U);
}

/* During DECODE_MARK_LONG checksum is still unused, so reuse it as the
   running LONG x8 average. final_pulses is likewise free in that state and is
   reused as the sample count.  No pulse_decoder_t SRAM growth is required. */
static inline void sample_header_mark_long(pulse_decoder_t *decoder,
                                           uint16_t duration_units)
{
    uint16_t scaled;
    uint16_t average;
    uint16_t difference;

    if ((decoder == NULL) ||
        (decoder_mode != DECODER_MODE_HEADER) ||
        (decoder->final_pulses >= HEADER_LONG_SAMPLE_LIMIT))
    {
        return;
    }

    scaled = (uint16_t)(duration_units << 3U);
    if (decoder->final_pulses == 0U)
    {
        decoder->checksum = scaled;
        decoder->final_pulses = 1U;
        return;
    }

    average = decoder->checksum;
    if (scaled >= average)
    {
        difference = (uint16_t)(scaled - average);
        decoder->checksum = (uint16_t)
            (average + ((difference + 4U) >> 3U));
    }
    else
    {
        difference = (uint16_t)(average - scaled);
        decoder->checksum = (uint16_t)
            (average - ((difference + 3U) >> 3U));
    }
    decoder->final_pulses++;
}

static int8_t classify_pulse(const pulse_decoder_t *decoder,
                             uint16_t duration_units)
{
    uint16_t scaled;
    uint16_t average;

    if ((decoder == NULL) || (decoder->short_x8 == 0U)) return -1;
    scaled = (uint16_t)(duration_units << 3U);
    average = decoder->short_x8;

    /* These range tests are safe in 16 bits for the complete allowed
       half-wave domain: scaled <= 8192 and average <= 8192. */
    if (((uint16_t)(scaled << 1U) < average) ||
        (scaled > (uint16_t)(average + average + average)))
    {
        return -1;
    }

    /* Keep the original 20*scaled < 29*average boundary exactly.  Normal MZ
       pulses always take the 16-bit branch; only extreme malformed values use
       the old wide expression as a correctness fallback. */
    if ((scaled <= CLASSIFY_16_SCALED_MAX) &&
        (average <= CLASSIFY_16_AVERAGE_MAX))
    {
        uint16_t left = (uint16_t)(scaled * 20U);
        uint16_t right = (uint16_t)(average * 29U);
        return (left < right) ? 0 : 1;
    }

    return (((uint32_t)scaled * 20UL) < ((uint32_t)average * 29UL)) ? 0 : 1;
}

static void publish_event(mz_tape_decoder_event_type_t type,
                          uint8_t value,
                          uint32_t byte_index,
                          const pulse_decoder_t *decoder)
{
    /* A caller consumes after every foreground interval. Preserve the first
       event if malformed input somehow tries to publish two at once. */
    if (pending_event.type != MZ_TAPE_DECODER_EVENT_NONE) return;
    pending_event.type = type;
    pending_event.value = value;
    pending_event.byte_index = byte_index;
    pending_event.calculated_checksum = decoder->checksum;
    pending_event.recorded_checksum = decoder->recorded_checksum;
    pending_event.leader_pulses = decoder->leader_pulses;
    pending_event.copy_index = decoder->copy_index;
}

static void begin_duplicate_gap(pulse_decoder_t *decoder,
                                uint32_t expected_bytes)
{
    decoder->state = DECODE_DUPLICATE_GAP;
    decoder->leader_pulses = 0U;
    decoder->mark_pulses = 0U;
    decoder->final_pulses = 0U;
    decoder->bit_count = 0U;
    decoder->byte_value = 0U;
    decoder->byte_index = 0UL;
    decoder->expected_bytes = expected_bytes;
    decoder->checksum = 0U;
    decoder->recorded_checksum = 0U;
    decoder->copy_index = 1U;
}

static void accept_byte(pulse_decoder_t *decoder, uint8_t value)
{
    uint32_t index = decoder->byte_index;

    if (index < decoder->expected_bytes)
    {
        decoder->checksum = (uint16_t)(decoder->checksum + popcount8(value));
        if (decoder_mode == DECODER_MODE_HEADER)
        {
            header_buffer[(uint8_t)index] = value;
        }
        else
        {
            publish_event(MZ_TAPE_DECODER_EVENT_DATA_BYTE, value, index,
                          decoder);
        }
    }
    else
    {
        decoder->recorded_checksum =
            (uint16_t)((decoder->recorded_checksum << 8U) | value);
    }

    decoder->byte_index++;
    if (decoder->byte_index == (decoder->expected_bytes + 2UL))
    {
        bool valid = decoder->recorded_checksum == decoder->checksum;
        if (decoder_mode == DECODER_MODE_HEADER)
        {
            if (valid)
            {
                validated_header = header_buffer;
                validated_short_x8 = decoder->short_x8;
                decoder_mode = DECODER_MODE_STOPPED;
                publish_event(MZ_TAPE_DECODER_EVENT_HEADER_VALID, 0U, 0UL,
                              decoder);
                return;
            }
            /* Native MZ700 repeats the block without a new leader/mark.  Keep
               the calibrated short interval and wait for the
               documented 256-short separator. */
            begin_duplicate_gap(decoder, MZ_TAPE_HEADER_BYTES);
            return;
        }

        publish_event(valid ? MZ_TAPE_DECODER_EVENT_BLOCK_VALID :
                              MZ_TAPE_DECODER_EVENT_BLOCK_INVALID,
                      0U, decoder->expected_bytes, decoder);
        decoder_mode = DECODER_MODE_STOPPED;
    }
}

static void accept_data_pulse(pulse_decoder_t *decoder,
                              uint8_t pulse_class)
{
    if (decoder->bit_count < 8U)
    {
        decoder->byte_value =
            (uint8_t)((decoder->byte_value << 1U) | pulse_class);
        decoder->bit_count++;
        return;
    }
    if (pulse_class != 1U)
    {
        reset_decoder(decoder, 0U);
        return;
    }
    accept_byte(decoder, decoder->byte_value);
    decoder->byte_value = 0U;
    decoder->bit_count = 0U;
}

static void feed_pulse(pulse_decoder_t *decoder, uint16_t duration_units)
{
    int8_t pulse_class;

    if ((decoder == NULL) || (decoder_mode == DECODER_MODE_STOPPED)) return;
    if (decoder->state == DECODE_DUPLICATE_GAP)
    {
        pulse_class = classify_pulse(decoder, duration_units);
        if (pulse_class == 0)
        {
            if (decoder->leader_pulses < 256U) decoder->leader_pulses++;
            if (decoder->leader_pulses == 256U)
            {
                decoder->state = DECODE_DATA;
                decoder->leader_pulses = 0U;
            }
        }
        else if (decoder->leader_pulses < 128U)
        {
            /* Ignore checksum trailing longs before the separator. */
            decoder->leader_pulses = 0U;
        }
        else
        {
            reset_decoder(decoder, duration_units);
        }
        return;
    }
    if (decoder->state == DECODE_SEARCH_LEADER)
    {
        if (decoder->short_x8 == 0U)
        {
            reset_decoder(decoder, duration_units);
            return;
        }

        /* After 256 stable pulses the calibrated 8-bit leader window is
           frozen.  This is the sustained Normal-leader hot path: two byte
           compares and the existing 16-bit leader counter increment. */
        if (decoder->final_pulses != 0U)
        {
            if ((duration_units >= decoder->mark_pulses) &&
                (duration_units <= decoder->final_pulses))
            {
                if (decoder->leader_pulses != 0xFFFFU) decoder->leader_pulses++;
                return;
            }
        }
        else if (accept_leader_pulse(decoder, duration_units))
        {
            if (decoder->leader_pulses != 0xFFFFU) decoder->leader_pulses++;
            if (decoder->leader_pulses == LEADER_LOCK_PULSES)
                lock_leader_window(decoder);
            return;
        }
        pulse_class = classify_pulse(decoder, duration_units);
        if ((decoder->leader_pulses >= MIN_LEADER_PULSES) &&
            (pulse_class == 1))
        {
            decoder->state = DECODE_MARK_LONG;
            decoder->mark_pulses = 1U;
            decoder->final_pulses = 0U;
            decoder->checksum = 0U;
            sample_header_mark_long(decoder, duration_units);
            return;
        }
        reset_decoder(decoder, duration_units);
        return;
    }

    pulse_class = classify_pulse(decoder, duration_units);
    if (pulse_class < 0)
    {
        reset_decoder(decoder, duration_units);
        return;
    }
    if (decoder->state == DECODE_MARK_LONG)
    {
        if (pulse_class == 1)
        {
            if (decoder->mark_pulses != 0xFFU) decoder->mark_pulses++;
            sample_header_mark_long(decoder, duration_units);
            return;
        }
        if ((decoder->mark_pulses >= MIN_MARK_PULSES) &&
            (decoder->mark_pulses <= MAX_MARK_PULSES))
        {
            validated_long_x8 = decoder->checksum;
            decoder->checksum = 0U;
            decoder->final_pulses = 0U;
            decoder->state = DECODE_MARK_SHORT;
            decoder->mark_pulses = 1U;
            return;
        }
        reset_decoder(decoder, duration_units);
        return;
    }
    if (decoder->state == DECODE_MARK_SHORT)
    {
        if (pulse_class == 0)
        {
            if (decoder->mark_pulses != 0xFFU) decoder->mark_pulses++;
            return;
        }
        if ((decoder->mark_pulses >= MIN_MARK_PULSES) &&
            (decoder->mark_pulses <= MAX_MARK_PULSES))
        {
            decoder->state = DECODE_MARK_FINAL;
            decoder->final_pulses = 1U;
            return;
        }
        reset_decoder(decoder, duration_units);
        return;
    }
    if (decoder->state == DECODE_MARK_FINAL)
    {
        if (pulse_class != 1)
        {
            reset_decoder(decoder, duration_units);
            return;
        }
        decoder->final_pulses++;
        if (decoder->final_pulses == FINAL_MARK_PULSES)
        {
            decoder->state = DECODE_DATA;
        }
        return;
    }
    accept_data_pulse(decoder, (uint8_t)pulse_class);
}

void mz_tape_decoder_begin_header(void)
{
    decoder_mode = DECODER_MODE_HEADER;
    validated_header = NULL;
    validated_short_x8 = 0U;
    validated_long_x8 = 0U;
    pending_event.type = MZ_TAPE_DECODER_EVENT_NONE;
    decoder.expected_bytes = MZ_TAPE_HEADER_BYTES;
    reset_decoder(&decoder, 0U);
}

void mz_tape_decoder_start_data(uint32_t byte_count)
{
    if ((validated_header == NULL) || (byte_count > 65535UL))
    {
        decoder_mode = DECODER_MODE_STOPPED;
        return;
    }
    decoder_mode = DECODER_MODE_DATA;
    pending_event.type = MZ_TAPE_DECODER_EVENT_NONE;
    decoder.expected_bytes = byte_count;
    reset_decoder(&decoder, 0U);
}

void mz_tape_decoder_start_recovery_data(uint32_t byte_count)
{
    if ((validated_header == NULL) || (byte_count > 65535UL))
    {
        decoder_mode = DECODER_MODE_STOPPED;
        return;
    }
    decoder_mode = DECODER_MODE_DATA;
    pending_event.type = MZ_TAPE_DECODER_EVENT_NONE;
    begin_duplicate_gap(&decoder, byte_count);
}

void mz_tape_decoder_break_signal(void)
{
    if (decoder_mode == DECODER_MODE_HEADER)
    {
        decoder.expected_bytes = MZ_TAPE_HEADER_BYTES;
        reset_decoder(&decoder, 0U);
    }
    else if ((decoder_mode == DECODER_MODE_DATA) &&
             (validated_header != NULL))
    {
        reset_decoder(&decoder, 0U);
    }
}

void mz_tape_decoder_stop(void)
{
    decoder_mode = DECODER_MODE_STOPPED;
    pending_event.type = MZ_TAPE_DECODER_EVENT_NONE;
}

bool mz_tape_decoder_feed_interval(uint16_t duration_units, uint8_t level)
{
    if (decoder_mode == DECODER_MODE_STOPPED) return false;
    level = level ? 1U : 0U;
    if ((duration_units == 0U) || (duration_units > MAX_HALF_UNITS))
    {
        mz_tape_decoder_break_signal();
        return false;
    }

    /*
       Input level is the physical external-CMT/MCU WRITE level.  Sharp's
       interface inverts that signal, so physical WRITE LOW is logical PC1/PC5
       HIGH: the receiver-relevant half-wave measured from the detected PC5
       rising edge to its later decision sample.  Decode that one level only.

       The opposite polarity is deliberately not tried.  External WAV polarity
       is corrected explicitly by INVERT SIG. before physical playback; live
       RECORD has the fixed hardware mapping above.  A symmetric leader cannot
       reliably determine polarity, so checksum fallback or dual candidates
       here would silently defeat that explicit contract.
    */
    if (level != 0U) return false;

    /* Sustained frozen-leader direct path.  feed_pulse() would do exactly
       these state/bound checks before returning, so perform them here and
       avoid the function call plus its generic state dispatch on every
       locked Normal leader pulse.  A pulse outside the frozen window falls
       through to feed_pulse() on the same interval so MARK detection and all
       recovery behaviour remain unchanged. */
    if ((decoder.state == DECODE_SEARCH_LEADER) &&
        (decoder.final_pulses != 0U) &&
        (duration_units >= decoder.mark_pulses) &&
        (duration_units <= decoder.final_pulses))
    {
        if (decoder.leader_pulses != 0xFFFFU) decoder.leader_pulses++;
    }
    else
    {
        feed_pulse(&decoder, duration_units);
    }
    return pending_event.type != MZ_TAPE_DECODER_EVENT_NONE;
}

bool mz_tape_decoder_take_event(mz_tape_decoder_event_t *event)
{
    if ((event == NULL) ||
        (pending_event.type == MZ_TAPE_DECODER_EVENT_NONE)) return false;
    *event = pending_event;
    pending_event.type = MZ_TAPE_DECODER_EVENT_NONE;
    return true;
}

const uint8_t *mz_tape_decoder_get_header(void)
{
    return validated_header;
}

uint8_t *mz_tape_decoder_get_data_scratch(void)
{
    return (validated_header != NULL) ? data_scratch : NULL;
}

uint16_t mz_tape_decoder_get_header_short_high_x8(void)
{
    return validated_short_x8;
}

uint16_t mz_tape_decoder_get_header_long_high_x8(void)
{
    return validated_long_x8;
}
