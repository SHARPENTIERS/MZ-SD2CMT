#include "mz_tape_profiles.h"

#include <avr/pgmspace.h>
#include <string.h>

#define CAPS_NATIVE (MZ_TAPE_CAP_PLAY | MZ_TAPE_CAP_RECORD | \
                     MZ_TAPE_CAP_AUTONAME)
#define CAPS_LOADER_RECORD (MZ_TAPE_CAP_PLAY | MZ_TAPE_CAP_RECORD | \
                            MZ_TAPE_CAP_AUTONAME | MZ_TAPE_CAP_LOADER)

static const mz_tape_profile_t mz_tape_profiles_P[MZ_TAPE_PROFILE_COUNT]
    PROGMEM = {
    { MZ_TAPE_PROFILE_MZ800_NORMAL_1X, MZ_TAPE_FRAMING_MZ800_NATIVE,
      CAPS_NATIVE, 250U, 250U, 500U, 500U, 6344UL, 6344UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false },
    { MZ_TAPE_PROFILE_MZ800_NORMAL_2X, MZ_TAPE_FRAMING_MZ800_NATIVE,
      CAPS_NATIVE, 136U, 136U, 277U, 277U, 11239UL, 11239UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false },
    { MZ_TAPE_PROFILE_MZ800_NORMAL_3X, MZ_TAPE_FRAMING_MZ800_NATIVE,
      CAPS_NATIVE, 113U, 113U, 204U, 204U, 15130UL, 15130UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false },
    { MZ_TAPE_PROFILE_MZ700_NORMAL_1X, MZ_TAPE_FRAMING_MZ700_NATIVE,
      CAPS_NATIVE, 240U, 264U, 464U, 494U, 22000UL, 11000UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false },
    { MZ_TAPE_PROFILE_MZ700_FAST3_3X, MZ_TAPE_FRAMING_MZ700_FAST3,
      (MZ_TAPE_CAP_PLAY | MZ_TAPE_CAP_LOADER),
      80U, 80U, 160U, 160U, 6344UL, 5500UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false },
    { MZ_TAPE_PROFILE_IC_1_4, MZ_TAPE_FRAMING_IC_TURBO,
      CAPS_LOADER_RECORD, 112U, 80U, 176U, 160U, 6344UL, 5500UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false },
    { MZ_TAPE_PROFILE_IC_1_3, MZ_TAPE_FRAMING_IC_TURBO,
      CAPS_LOADER_RECORD, 112U, 96U, 224U, 192U, 6344UL, 5500UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false },
    { MZ_TAPE_PROFILE_IC_1_2, MZ_TAPE_FRAMING_IC_TURBO,
      CAPS_LOADER_RECORD, 144U, 112U, 256U, 224U, 6344UL, 5500UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false },
    { MZ_TAPE_PROFILE_TC_1_3, MZ_TAPE_FRAMING_TC_TURBO,
      CAPS_LOADER_RECORD, 112U, 112U, 204U, 204U, 6344UL, 15130UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false },
    { MZ_TAPE_PROFILE_TC_1_2, MZ_TAPE_FRAMING_TC_TURBO,
      CAPS_LOADER_RECORD, 144U, 144U, 288U, 288U, 6344UL, 11239UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false },
    { MZ_TAPE_PROFILE_MZ800_NORMAL_4X, MZ_TAPE_FRAMING_MZ800_NATIVE,
      CAPS_NATIVE, 112U, 80U, 176U, 160U, 11000UL, 5500UL,
      40U, 40U, 20U, 20U, 2U, 0U, false, false }
};

static_assert((sizeof(mz_tape_profiles_P) /
               sizeof(mz_tape_profiles_P[0])) == MZ_TAPE_PROFILE_COUNT,
              "MZ tape profile table is incomplete");

bool mz_tape_profile_read(mz_tape_profile_id_t id,
                          mz_tape_profile_t *profile)
{
    if ((profile == NULL) || ((uint8_t)id >= MZ_TAPE_PROFILE_COUNT))
    {
        return false;
    }
    memcpy_P(profile, &mz_tape_profiles_P[(uint8_t)id], sizeof(*profile));
    return true;
}

bool mz_tape_profile_has_capability(mz_tape_profile_id_t id, uint8_t cap)
{
    mz_tape_profile_t profile;
    return mz_tape_profile_read(id, &profile) &&
           ((profile.caps & cap) == cap);
}
