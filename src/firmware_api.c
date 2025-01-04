#include "firmware_api.h"
#include <string.h>

bool is_momentum_firmware(void) {
    const Version* version = furi_hal_version_get_firmware_version();
    const char* firmware_origin = version_get_custom_name(version);
    return (firmware_origin != NULL) && (strcmp(firmware_origin, "Momentum") == 0);
}

bool has_momentum_features(void) {
#ifdef HAS_MOMENTUM_SUPPORT
    return is_momentum_firmware();
#else
    return false;
#endif
} 