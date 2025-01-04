#pragma once
#include <furi.h>
#include <furi_hal_version.h>

// Only try to include momentum if we're building for it
#if defined(FAP_MOMENTUM) && defined(__has_include)
#if __has_include(<momentum/momentum.h>)
#include <momentum/momentum.h>
#define HAS_MOMENTUM_SUPPORT 1
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