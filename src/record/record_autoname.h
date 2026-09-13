#ifndef SD2CMT2_RECORD_AUTONAME_H
#define SD2CMT2_RECORD_AUTONAME_H

#include <stdbool.h>
#include <stdint.h>

#include "../formats/file_format.h"
#include "../play/loader_mode.h"

typedef uint8_t record_autoname_aux_profile_t;

#define RECORD_AUTONAME_AUX_PROFILE_NONE    ((record_autoname_aux_profile_t)0U)
#define RECORD_AUTONAME_AUX_PROFILE_SIN_1_1 ((record_autoname_aux_profile_t)1U)
#define RECORD_AUTONAME_AUX_PROFILE_SIN_1_2 ((record_autoname_aux_profile_t)2U)
#define RECORD_AUTONAME_AUX_PROFILE_SIN_1_3 ((record_autoname_aux_profile_t)3U)
#define RECORD_AUTONAME_AUX_PROFILE_SIN_1_4 ((record_autoname_aux_profile_t)4U)
#define RECORD_AUTONAME_AUX_PROFILE_CPM_1_1 ((record_autoname_aux_profile_t)5U)
#define RECORD_AUTONAME_AUX_PROFILE_CPM_1_2 ((record_autoname_aux_profile_t)6U)
#define RECORD_AUTONAME_AUX_PROFILE_CPM_1_3 ((record_autoname_aux_profile_t)7U)
#define RECORD_AUTONAME_AUX_PROFILE_CPM_1_4 ((record_autoname_aux_profile_t)8U)

/*
   Foreground-only tape metadata detector. WAV feeds chronological packed
   WRITE samples; LEP/L16 feeds the same quantized edge intervals which are
   written to the recording. Detection is enabled together with AUTONAME.

   Supported naming paths:
     - native/IC/TC Sharp MZ through the shared mz_tape_decoder,
     - InterCopy CP/M stream (1:1 .. 1:4) through one small adaptive parser,
     - Sinclair-style stream (1:1 .. 1:4) through the same adaptive parser.

   The auxiliary parser is deliberately RAM-light: it has one union state and
   writes a candidate title directly into the existing AUTONAME title buffer;
   no second header/payload buffer is allocated. No decoder work runs in ISR.
*/
void record_autoname_begin(bool enabled, file_format_t format,
                           uint32_t wav_sample_rate);
/* Reset on a real discontinuity/overlong signal interval, not on a MOTOR or
   user pause: captured FIFOs may still contain the end of a tape header. */
void record_autoname_break_signal(void);
void record_autoname_feed_packed_samples(uint8_t packed, uint8_t valid_bits);
void record_autoname_feed_interval(uint16_t duration_units);
void record_autoname_feed_level_interval(uint16_t duration_units,
                                         uint8_t level);

/* Reuse a header already validated by the shared MZF recorder decoder. */
void record_autoname_accept_header(const uint8_t *header);

bool record_autoname_has_name(void);
const char *record_autoname_get_name(void);
bool record_autoname_get_detected_loader_mode(loader_mode_t *loader_mode);
bool record_autoname_get_detected_aux_profile(record_autoname_aux_profile_t *profile);
bool record_autoname_get_payload_progress_percent(uint8_t *percent);

/* Rename the already closed RECxxxx file. Failure keeps the original file. */
bool record_autoname_apply(const char *directory_path, file_format_t format);

#endif
