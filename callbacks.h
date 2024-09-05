#pragma once
typedef struct {
    uint8_t rgb_mode_index;
    uint8_t channel_hop_delay_index;
    uint8_t enable_channel_hopping_index;
    uint8_t enable_random_ble_mac_index;
} Settings;

void set_default_settings(Settings* settings) {
    settings->rgb_mode_index = 0; // Default: Stealth
    settings->channel_hop_delay_index = 0; // Default: 500ms
    settings->enable_channel_hopping_index = 0; // Default: Disabled
    settings->enable_random_ble_mac_index = 0; // Default: Disabled
}

// Create settings string from the struct
char* create_settings_string(Settings* settings) {
    char* settings_string = (char*)malloc(5); // Allocate memory for 4 settings + null terminator
    if(settings_string == NULL) {
        return NULL;
    }
    snprintf(
        settings_string,
        5,
        "%hhu%hhu%hhu%hhu",
        settings->rgb_mode_index,
        settings->channel_hop_delay_index,
        settings->enable_channel_hopping_index,
        settings->enable_random_ble_mac_index);
    return settings_string;
}

bool write_settings_to_file(File* file, Settings* settings) {
    // Seek to the start of the file and truncate it to start fresh
    if(!storage_file_seek(file, 0, true)) {
        FURI_LOG_I("Ghost ESP", "Failed to seek to the beginning of the file.\n");
        return false;
    }

    if(!storage_file_truncate(file)) {
        FURI_LOG_I("Ghost ESP", "Failed to truncate the file.\n");
        return false;
    }

    // Create the settings string from the structure
    char* settings_string = create_settings_string(settings);
    if(settings_string == NULL) {
        FURI_LOG_I("Ghost ESP", "Failed to allocate memory for settings string.\n");
        return false;
    }

    // Write the settings string to the file
    size_t data_length = strlen(settings_string);
    size_t bytes_written = storage_file_write(file, settings_string, data_length);
    free(settings_string);

    if(bytes_written != data_length) {
        FURI_LOG_I("Ghost ESP", "Failed to write all data to the file.\n");
        return false;
    }

    FURI_LOG_I("Ghost ESP", "Settings successfully written to the file.");
    return true;
}

bool read_and_parse_settings_file(File* file, const char* file_path, Settings* settings) {
    // Seek to the start of the file
    storage_file_seek(file, 0, true);

    // Check the size of the file
    uint64_t file_size = storage_file_size(file);
    if(file_size < 4) { // Ensure there are 4 bytes to read
        FURI_LOG_I("Ghost ESP", "File is invalid or empty: %s\n", file_path);
        set_default_settings(settings);
        write_settings_to_file(file, settings); // Reset to default settings if file is too short
        return false;
    }

    // Read the settings from the file
    char buffer[5]; // Buffer to hold the settings data
    int bytes_read = storage_file_read(file, buffer, 4);
    if(bytes_read != 4) {
        FURI_LOG_I("Ghost ESP", "Failed to read the required number of bytes.");
        set_default_settings(settings);
        write_settings_to_file(file, settings); // Reset to default settings if read fails
        return false;
    }
    buffer[4] = '\0'; // Ensure null termination for proper string operations

    // Parse each setting from the buffer
    bool invalid = false;
    if(buffer[0] >= '0' && buffer[0] <= '2') {
        settings->rgb_mode_index = buffer[0] - '0';
    } else {
        FURI_LOG_I("Ghost ESP", "Invalid RGB Mode Index: %c", buffer[0]);
        invalid = true;
    }

    if(buffer[1] >= '0' && buffer[1] <= '9') {
        settings->channel_hop_delay_index = buffer[1] - '0';
    } else {
        FURI_LOG_I("Ghost ESP", "Invalid Channel Hop Delay Index: %c", buffer[1]);
        invalid = true;
    }

    if(buffer[2] >= '0' && buffer[2] <= '1') {
        settings->enable_channel_hopping_index = buffer[2] - '0';
    } else {
        FURI_LOG_I("Ghost ESP", "Invalid Enable Channel Hopping Index: %c", buffer[2]);
        invalid = true;
    }

    if(buffer[3] >= '0' && buffer[3] <= '1') {
        settings->enable_random_ble_mac_index = buffer[3] - '0';
    } else {
        FURI_LOG_I("Ghost ESP", "Invalid Random BLE MAC Index: %c", buffer[3]);
        invalid = true;
    }

    // If any setting is invalid, reset to default settings and rewrite the file
    if(invalid) {
        FURI_LOG_I("Ghost ESP", "Invalid data detected, resetting to default settings.");
        set_default_settings(settings);
        write_settings_to_file(file, settings);
    }

    return !invalid; // Return true if settings are valid, false otherwise
}

// Helper function to update and write settings
void update_settings_and_write(AppState* app, Settings* settings) {
    write_settings_to_file(app->uart_context->storageContext->settings_file, settings);
}

// Event handler for changing RGB mode
void on_rgb_mode_changed(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    AppState* app = variable_item_get_context(item);
    Settings settings;

    // Read the current settings
    if(!read_and_parse_settings_file(
           app->uart_context->storageContext->settings_file,
           GHOST_ESP_APP_SETTINGS_FILE,
           &settings)) {
        return;
    }

    settings.rgb_mode_index = index;

    switch(index) {
    case 0:
        variable_item_set_current_value_text(item, "Stealth");
        send_uart_command("setsetting -i 1 -v 1\n", app);
        break;
    case 1:
        variable_item_set_current_value_text(item, "Normal");
        send_uart_command("setsetting -i 1 -v 2\n", app);
        break;
    case 2:
        variable_item_set_current_value_text(item, "Rainbow");
        send_uart_command("setsetting -i 1 -v 3\n", app);
        break;
    }

    // Write the updated settings
    update_settings_and_write(app, &settings);
}

// Event handler for changing Channel Switch Delay
void on_channelswitchdelay_changed(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    AppState* app = variable_item_get_context(item);
    Settings settings;

    // Read the current settings
    if(!read_and_parse_settings_file(
           app->uart_context->storageContext->settings_file,
           GHOST_ESP_APP_SETTINGS_FILE,
           &settings)) {
        return;
    }

    settings.channel_hop_delay_index = index;

    switch(index) {
    case 0:
        variable_item_set_current_value_text(item, "500ms");
        send_uart_command("setsetting -i 2 -v 1\n", app);
        break;
    case 1:
        variable_item_set_current_value_text(item, "1000ms");
        send_uart_command("setsetting -i 2 -v 2\n", app);
        break;
    case 2:
        variable_item_set_current_value_text(item, "2000ms");
        send_uart_command("setsetting -i 2 -v 3\n", app);
        break;
    case 3:
        variable_item_set_current_value_text(item, "3000ms");
        send_uart_command("setsetting -i 2 -v 4\n", app);
        break;
    case 4:
        variable_item_set_current_value_text(item, "4000ms");
        send_uart_command("setsetting -i 2 -v 5\n", app);
        break;
    }

    // Write the updated settings
    update_settings_and_write(app, &settings);
}

// Event handler for toggling channel hopping
void on_togglechannelhopping_changed(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    AppState* app = variable_item_get_context(item);
    Settings settings;

    // Read the current settings
    if(!read_and_parse_settings_file(
           app->uart_context->storageContext->settings_file,
           GHOST_ESP_APP_SETTINGS_FILE,
           &settings)) {
        return;
    }

    settings.enable_channel_hopping_index = index;

    switch(index) {
    case 0:
        variable_item_set_current_value_text(item, "False");
        send_uart_command("setsetting -i 3 -v 1\n", app);
        break;
    case 1:
        variable_item_set_current_value_text(item, "True");
        send_uart_command("setsetting -i 3 -v 2\n", app);
        break;
    }

    // Write the updated settings
    update_settings_and_write(app, &settings);
}

// Event handler for toggling BLE MAC randomization
void on_ble_mac_changed(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    AppState* app = variable_item_get_context(item);
    Settings settings;

    // Read the current settings
    if(!read_and_parse_settings_file(
           app->uart_context->storageContext->settings_file,
           GHOST_ESP_APP_SETTINGS_FILE,
           &settings)) {
        return;
    }

    settings.enable_random_ble_mac_index = index;

    switch(index) {
    case 0:
        variable_item_set_current_value_text(item, "False");
        send_uart_command("setsetting -i 4 -v 1\n", app);
        break;
    case 1:
        variable_item_set_current_value_text(item, "True");
        send_uart_command("setsetting -i 4 -v 2\n", app);
        break;
    }

    // Write the updated settings
    update_settings_and_write(app, &settings);
}
