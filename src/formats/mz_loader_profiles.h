#ifndef SD2CMT2_MZ_LOADER_PROFILES_H
#define SD2CMT2_MZ_LOADER_PROFILES_H

#include <stdbool.h>
#include <stdint.h>

#define MZ_IC_LOADER_BYTES 96U
#define MZ_TC_LOADER_BYTES 90U

typedef enum
{
    MZ_COPIER_NONE = 0,
    MZ_COPIER_IC_1_4,
    MZ_COPIER_IC_1_3,
    MZ_COPIER_IC_1_2,
    MZ_COPIER_TC_1_3,
    MZ_COPIER_TC_1_2,
    MZ_COPIER_IC_1_1,
    MZ_COPIER_TC_1_1
} mz_copier_profile_t;

void mz_loader_profile_copy_ic_loader(uint8_t *destination);
void mz_loader_profile_copy_tc_loader(uint8_t *destination);
void mz_loader_profile_copy_tc_tag(uint8_t *destination);

/* Detect the IC layout from its header fields and encoded speed byte. Loader
   bodies may differ between compatible FASTIPL revisions. */
bool mz_loader_profile_detect_ic(const uint8_t *header,
                                 mz_copier_profile_t *profile);
bool mz_loader_profile_recognize_ic(const uint8_t *header,
                                    uint8_t *canonical_header,
                                    mz_copier_profile_t *profile);
bool mz_loader_profile_recognize_tc_header(const uint8_t *header);
/* Validate the complete TC loader and extract its speed without normalizing
   the logical header into a second 128-byte buffer. */
bool mz_loader_profile_detect_tc(const uint8_t *loader_header,
                                 const uint8_t *loader_data,
                                 mz_copier_profile_t *profile);
bool mz_loader_profile_normalize_tc(const uint8_t *loader_header,
                                    const uint8_t *loader_data,
                                    uint8_t *canonical_header,
                                    mz_copier_profile_t *profile);

#endif
