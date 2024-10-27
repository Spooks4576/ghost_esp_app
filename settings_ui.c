#include "settings_ui.h"
#include "settings_ui_types.h"
#include "settings_def.h"
#include "app_state.h"
#include "sequential_file.h"
#include "uart_utils.h"
#include <furi.h>
#include <gui/modules/variable_item_list.h>
#include <storage/storage.h>
#include "settings_storage.h"
#include "utils.h"
#include "callbacks.h"

typedef struct {
    SettingsUIContext* settings_ui_context;
    SettingKey key;
} VariableItemContext;


// Function to clear log files
void clear_log_files(void* context) {
    AppState* app = (AppState*)context;

    FURI_LOG_I("ClearLogs", "Entering clear_log_files function");

    FURI_LOG_I("ClearLogs", "Starting log cleanup");

    // Close current log file if it exists
    if(app && app->uart_context && app->uart_context->storageContext &&
       app->uart_context->storageContext->log_file) {
        FURI_LOG_D("ClearLogs", "Closing current log file");
        storage_file_close(app->uart_context->storageContext->log_file);
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* dir = storage_file_alloc(storage);
    FileInfo file_info;
    char* filename = malloc(256);
    int deleted_count = 0;

    FURI_LOG_I("ClearLogs", "Opening logs directory: %s", GHOST_ESP_APP_FOLDER_LOGS);
    if(storage_dir_open(dir, GHOST_ESP_APP_FOLDER_LOGS)) {
        FURI_LOG_D("ClearLogs", "Successfully opened logs directory");
        while(storage_dir_read(dir, &file_info, filename, 256)) {
            if(!(file_info.flags & FSF_DIRECTORY)) {
                FURI_LOG_D("ClearLogs", "Processing file: %s", filename);
                char full_path[512];
                snprintf(full_path, sizeof(full_path), "%s/%s", GHOST_ESP_APP_FOLDER_LOGS, filename);
                FURI_LOG_I("ClearLogs", "Attempting to delete: %s", full_path);

                if(storage_file_exists(storage, full_path)) {
                    FURI_LOG_D("ClearLogs", "File exists, attempting to delete: %s", full_path);
                    if(storage_simply_remove(storage, full_path)) {
                        FURI_LOG_I("ClearLogs", "Successfully deleted: %s", filename);
                        deleted_count++;
                    } else {
                        FURI_LOG_E("ClearLogs", "Failed to delete: %s", filename);
                    }
                }
            }
        }
    }

    FURI_LOG_D("ClearLogs", "Freeing filename buffer");
    free(filename);
    storage_dir_close(dir);
    storage_file_free(dir);

    FURI_LOG_I("ClearLogs", "Deleted %d files", deleted_count);

    // Create new log file
    if(app && app->uart_context && app->uart_context->storageContext) {
        FURI_LOG_I("ClearLogs", "Creating new log file");
        sequential_file_open(
            app->uart_context->storageContext->storage_api,
            app->uart_context->storageContext->log_file,
            GHOST_ESP_APP_FOLDER_LOGS,
            "ghost_logs",
            "txt");
    }

    furi_record_close(RECORD_STORAGE);
    FURI_LOG_I("ClearLogs", "Log cleanup complete");
}

bool settings_set(Settings* settings, SettingKey key, uint8_t value, void* context) {
    FURI_LOG_D("SettingsSet", "Entering settings_set function for key: %d, value: %d", key, value);

    if(key >= SETTINGS_COUNT) return false;
    bool changed = false;

    switch(key) {
        case SETTING_RGB_MODE:
            if(settings->rgb_mode_index != value) {
                settings->rgb_mode_index = value;
                changed = true;
            }
            break;

        case SETTING_CHANNEL_HOP_DELAY:
            if(settings->channel_hop_delay_index != value) {
                settings->channel_hop_delay_index = value;
                changed = true;
            }
            break;

        case SETTING_ENABLE_CHANNEL_HOPPING:
            if(settings->enable_channel_hopping_index != value) {
                settings->enable_channel_hopping_index = value;
                changed = true;
            }
            break;

        case SETTING_ENABLE_RANDOM_BLE_MAC:
            if(settings->enable_random_ble_mac_index != value) {
                settings->enable_random_ble_mac_index = value;
                changed = true;
            }
            break;

        case SETTING_STOP_ON_BACK:
            if(settings->stop_on_back_index != value) {
                settings->stop_on_back_index = value;
                changed = true;
            }
            break;

        case SETTING_REBOOT_ESP:
            if(value == 0) { // Execute on press
                SettingsUIContext* settings_context = (SettingsUIContext*)context;
                if(settings_context && settings_context->send_uart_command) {
                    FURI_LOG_I("SettingsSet", "Executing reboot command");
                    settings_context->send_uart_command("handle_reboot\n", settings_context->context);
                }
            }
            break;

        case SETTING_CLEAR_LOGS:
        case SETTING_CLEAR_NVS:
            if(value == 0) { // Execute on press
                SettingsUIContext* settings_context = (SettingsUIContext*)context;
                if(settings_context && settings_context->context) {
                    FURI_LOG_I("SettingsSet", "Posting custom event to show confirmation dialog");
                    AppState* app_state = (AppState*)settings_context->context;
                    // Post a custom event with the key
                    view_dispatcher_send_custom_event(app_state->view_dispatcher, key);
                }
            }
            break;

        default:
            return false;
    }

    if(changed) {
        FURI_LOG_I("SettingsSet", "Setting changed, saving to storage");
        // Save settings to storage
        settings_storage_save(settings, GHOST_ESP_APP_SETTINGS_FILE);
    }

    return true;
}

uint8_t settings_get(const Settings* settings, SettingKey key) {
    FURI_LOG_D("SettingsGet", "Getting setting for key: %d", key);

    switch(key) {
        case SETTING_RGB_MODE:
            return settings->rgb_mode_index;

        case SETTING_CHANNEL_HOP_DELAY:
            return settings->channel_hop_delay_index;

        case SETTING_ENABLE_CHANNEL_HOPPING:
            return settings->enable_channel_hopping_index;

        case SETTING_ENABLE_RANDOM_BLE_MAC:
            return settings->enable_random_ble_mac_index;

        case SETTING_STOP_ON_BACK:
            return settings->stop_on_back_index;

        case SETTING_REBOOT_ESP:
        case SETTING_CLEAR_LOGS:
        case SETTING_CLEAR_NVS:
            return 0;

        default:
            return 0;
    }
}

static void settings_item_change_callback(VariableItem* item) {
    FURI_LOG_D("SettingsChange", "Settings item change callback triggered");

    VariableItemContext* item_context = variable_item_get_context(item);
    if(!item_context) {
        FURI_LOG_E("SettingsChange", "Invalid item context");
        return;
    }

    SettingsUIContext* context = item_context->settings_ui_context;
    SettingKey key = item_context->key;

    FURI_LOG_D("SettingsChange", "Handling key: %d", key);

    const SettingMetadata* metadata = settings_get_metadata(key);
    if(!metadata) {
        FURI_LOG_E("SettingsChange", "Invalid metadata for key: %d", key);
        return;
    }

    if(metadata->is_action) {
        // Action button pressed
        FURI_LOG_D("SettingsChange", "Action button detected for key: %d", key);

        // Reset the action button's value to allow future presses
        variable_item_set_current_value_index(item, 0);
        variable_item_set_current_value_text(item, metadata->data.action.name);
        FURI_LOG_D("SettingsChange", "Action button state reset for key: %d", key);

        // Execute the action
        if(metadata->data.action.callback) {
            FURI_LOG_D("SettingsChange", "Executing callback for action key: %d", key);
            metadata->data.action.callback(context);
            FURI_LOG_I("SettingsChange", "Action callback executed for key: %d", key);
        } else if(metadata->data.action.command && context->send_uart_command) {
            FURI_LOG_D("SettingsChange", "Executing command for action key: %d", key);
            context->send_uart_command(metadata->data.action.command, context->context);
            FURI_LOG_I("SettingsChange", "Action command executed for key: %d", key);
        } else {
            FURI_LOG_I("SettingsChange", "No action handler found for key: %d", key);
        }

        return;
    }

    // Handle regular settings
    uint8_t value = variable_item_get_current_value_index(item);
    FURI_LOG_D("SettingsChange", "Attempting to set setting: key=%d, value=%d", key, value);

    if(settings_set(context->settings, key, value, context)) {
        variable_item_set_current_value_text(item, metadata->data.setting.value_names[value]);
        if(metadata->data.setting.uart_command && context->send_uart_command) {
            char command[64];
            snprintf(command, sizeof(command), "%s %d\n", metadata->data.setting.uart_command, value + 1);
            FURI_LOG_D("SettingsChange", "Sending UART command: %s", command);
            context->send_uart_command(command, context->context);
        }
    }
}

static void settings_menu_callback(void* context, uint32_t index) {
    UNUSED(index);
    AppState* app_state = context;
    view_dispatcher_switch_to_view(app_state->view_dispatcher, 4); // Switch to settings view
    app_state->current_view = 4;
}

static void settings_action_callback(void* context, uint32_t index) {
    SettingsUIContext* settings_context = context;
    // Execute the action directly without value simulation
    settings_set(settings_context->settings, index, 0, settings_context);
}

void settings_setup_gui(VariableItemList* list, SettingsUIContext* context) {
    FURI_LOG_D("SettingsSetup", "Entering settings_setup_gui");
    AppState* app_state = (AppState*)context->context;

    // Add "Configuration" submenu item
    submenu_add_item(
        app_state->settings_actions_menu,
        "Configuration  >",
        SETTINGS_COUNT,
        settings_menu_callback,
        app_state);

    // Iterate over all settings
    for(SettingKey key = 0; key < SETTINGS_COUNT; key++) {
        const SettingMetadata* metadata = settings_get_metadata(key);
        if(!metadata) continue;

        if(metadata->is_action) {
            // Add all action items to the actions submenu
            submenu_add_item(
                app_state->settings_actions_menu,
                metadata->name,
                key,
                settings_action_callback,
                context);
            FURI_LOG_D("SettingsSetup", "Added action button: %s", metadata->name);
        } else {
            // Handle regular settings
            VariableItemContext* item_context = malloc(sizeof(VariableItemContext));
            if(!item_context) {
                FURI_LOG_E("SettingsSetup", "Failed to allocate memory for item context");
                continue;
            }
            item_context->settings_ui_context = context;
            item_context->key = key;

            VariableItem* item = variable_item_list_add(
                list,
                metadata->name,
                metadata->data.setting.max_value + 1,
                settings_item_change_callback,
                item_context);

            if(item) {
                uint8_t current_value = settings_get(context->settings, key);
                variable_item_set_current_value_index(item, current_value);
                variable_item_set_current_value_text(item, metadata->data.setting.value_names[current_value]);
                FURI_LOG_D("SettingsSetup", "Added setting item: %s", metadata->name);
            }
        }
    }
}

bool settings_custom_event_callback(void* context, uint32_t event_id) {
    AppState* app_state = (AppState*)context;
    SettingKey key = (SettingKey)event_id;

    switch(key) {
        case SETTING_CLEAR_LOGS:
            show_confirmation_dialog_ex(
                app_state,
                "Clear Logs",
                "Are you sure you want to clear the logs?\nThis action cannot be undone.",
                logs_clear_confirmed_callback,
                logs_clear_cancelled_callback);
            break;

        case SETTING_CLEAR_NVS:
            show_confirmation_dialog_ex(
                app_state,
                "Clear NVS",
                "Are you sure you want to clear NVS?\nThis will reset all ESP settings.",
                nvs_clear_confirmed_callback,
                nvs_clear_cancelled_callback);
            break;

        default:
            return false;
    }

    return true;
}