#ifndef CLEAN_RX_FIRMWARE_PROFILE_H
#define CLEAN_RX_FIRMWARE_PROFILE_H

#ifndef CLEAN_RX_DIAGNOSTIC
#define CLEAN_RX_DIAGNOSTIC 0
#endif
_Static_assert(CLEAN_RX_DIAGNOSTIC == 0 || CLEAN_RX_DIAGNOSTIC == 1,
               "Diagnostic firmware selection must be zero or one");

#endif
