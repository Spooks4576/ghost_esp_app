#pragma once
#include <furi.h>
#include <furi_hal_version.h>

// Check if we're building for Momentum firmware
#if defined(FAP_MOMENTUM) && defined(__has_include)
#if __has_include(<momentum/momentum.h>)
#include <momentum/momentum.h>
#define HAS_MOMENTUM_SUPPORT 1
#endif
#endif

// Check for version.h and custom name function
#if defined(__has_include)
#if __has_include(<lib/toolbox/version.h>)
#include <lib/toolbox/version.h>
// Only define if the function prototype exists
#if defined(version_get_custom_name)
#define HAS_VERSION_CUSTOM_NAME 1
#endif
#endif
#endif

/**
 * @brief Check if currently running on Momentum firmware
 * @return true if running on Momentum firmware
 */
bool is_momentum_firmware(void);

/**
 * @brief Check if Momentum features are available
 * @return true if Momentum features can be used
 */
bool has_momentum_features(void);
