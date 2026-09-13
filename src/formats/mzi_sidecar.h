#ifndef SD2CMT2_MZI_SIDECAR_H
#define SD2CMT2_MZI_SIDECAR_H

#include <stdbool.h>
#include <stdint.h>

#include "../play/loader_mode.h"

typedef enum
{
    MZI_RECORD_PROFILE_NORMAL_1_1 = 0,
    MZI_RECORD_PROFILE_NORMAL_1_2,
    MZI_RECORD_PROFILE_NORMAL_1_3,
    MZI_RECORD_PROFILE_IC_1_4,
    MZI_RECORD_PROFILE_IC_1_3,
    MZI_RECORD_PROFILE_IC_1_2,
    MZI_RECORD_PROFILE_TC_1_3,
    MZI_RECORD_PROFILE_TC_1_2,
    MZI_RECORD_PROFILE_NORMAL_1_4,
    MZI_RECORD_PROFILE_TC_1_4,
    MZI_RECORD_PROFILE_COUNT
} mzi_record_profile_t;

bool mzi_record_profile_get_loader_mode(mzi_record_profile_t profile,
                                        loader_mode_t *loader_mode);

/* Writes/truncates the same-basename .MFI companion for a completed MZF. */
bool mzi_sidecar_write_for_mzf(const char *mzf_path,
                               mzi_record_profile_t profile);

/* Writes any explicit PLAY loader mode, including MZ700 and UL variants.
   LOADER_MODE_AUTO is not a valid sidecar payload. */
bool mzi_sidecar_write_loader_for_mzf(const char *mzf_path,
                                      loader_mode_t loader_mode);

/* Reads the same-basename .MFI and returns its explicit PLAY loader mode. */
bool mzi_sidecar_read_loader_for_mzf(const char *mzf_path,
                                     loader_mode_t *loader_mode);

/* True when the corresponding same-basename metadata file exists. */
bool mzi_sidecar_exists_for_mzf(const char *mzf_path);
bool mzi_sidecar_exists_for_mzt(const char *mzt_path);

/* Reads RECORD=n from the same-basename .MTI companion of an MZT.
   Record numbering starts at 1. Missing/invalid records return false. */
bool mzi_sidecar_read_loader_for_mzt_record(const char *mzt_path,
                                            uint16_t record_index,
                                            loader_mode_t *loader_mode);

/* Moves a companion after AUTONAME. Existing destination .MFI is replaced. */
bool mzi_sidecar_relocate_for_mzf(const char *old_mzf_path,
                                  const char *new_mzf_path);

#endif
