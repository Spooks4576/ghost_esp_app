// settings_def.c
#include "settings_def.h"
#include <stddef.h>

// Define the constant arrays
const char* const SETTING_VALUE_NAMES_RGB_MODE[] = {"Stealth", "Normal", "Rainbow"};
const char* const SETTING_VALUE_NAMES_CHANNEL_HOP[] = {"500ms", "1000ms", "2000ms", "3000ms", "4000ms"};
const char* const SETTING_VALUE_NAMES_BOOL[] = {"False", "True"};
const char* const SETTING_VALUE_NAMES_ACTION[] = {"Press OK", "Press OK"};

#include "settings_ui.h"  // Add this include at the top

const SettingMetadata SETTING_METADATA[SETTINGS_COUNT] = {
    [SETTING_RGB_MODE] = {
        .name = "RGB Mode",
        .data.setting = {
            .max_value = RGB_MODE_COUNT - 1,
            .value_names = SETTING_VALUE_NAMES_RGB_MODE,
            .uart_command = "setsetting -i 1 -v"
        },
        .is_action = false
    },
    [SETTING_CHANNEL_HOP_DELAY] = {
        .name = "Channel Switch Delay",
        .data.setting = {
            .max_value = CHANNEL_HOP_COUNT - 1,
            .value_names = SETTING_VALUE_NAMES_CHANNEL_HOP,
            .uart_command = "setsetting -i 2 -v"
        },
        .is_action = false
    },
    [SETTING_ENABLE_CHANNEL_HOPPING] = {
        .name = "Enable Channel Hopping",
        .data.setting = {
            .max_value = 1,
            .value_names = SETTING_VALUE_NAMES_BOOL,
            .uart_command = "setsetting -i 3 -v"
        },
        .is_action = false
    },
    [SETTING_ENABLE_RANDOM_BLE_MAC] = {
        .name = "Enable Random BLE Mac",
        .data.setting = {
            .max_value = 1,
            .value_names = SETTING_VALUE_NAMES_BOOL,
            .uart_command = "setsetting -i 4 -v"
        },
        .is_action = false
    },
    [SETTING_STOP_ON_BACK] = {
        .name = "Stop On Back",
        .data.setting = {
            .max_value = 1,
            .value_names = SETTING_VALUE_NAMES_BOOL,
            .uart_command = NULL
        },
        .is_action = false
    },
    [SETTING_REBOOT_ESP] = {
        .name = "Reboot ESP",
        .data.action = {
            .name = "Reboot ESP",
            .command = "handle_reboot",
            .callback = NULL
        },
        .is_action = true
    },
    [SETTING_CLEAR_LOGS] = {
        .name = "Clear Log Files",
        .data.action = {
            .name = "Clear Log Files",
            .command = NULL,
            .callback = &clear_log_files  // Now this should compile
        },
        .is_action = true
    },
    [SETTING_CLEAR_NVS] = {
        .name = "Clear NVS",
        .data.action = {
            .name = "Clear NVS",
            .command = "handle_clearnvs",
            .callback = NULL
        },
        .is_action = true
    }
};
// Update the function signature to include the is_action flag
const SettingMetadata* settings_get_metadata(SettingKey key) {
    if(key >= SETTINGS_COUNT) {
        return NULL;
    }
    return &SETTING_METADATA[key];
}