#pragma once
typedef struct {
    uint8_t rgb_mode_index;
    uint8_t channel_hop_delay_index;
    uint8_t enable_channel_hopping_index;
    uint8_t enable_random_ble_mac_index;
} Settings;

// Create settings string from the struct
char* create_settings_string(Settings* settings) {
    char* settings_string = (char*)malloc(5); // Allocate memory for 4 settings + null terminator
    if(settings_string == NULL) {
        return NULL;
    }

    snprintf(settings_string, 5, "%hhu%hhu%hhu%hhu",
             settings->rgb_mode_index,
             settings->channel_hop_delay_index,
             settings->enable_channel_hopping_index,
             settings->enable_random_ble_mac_index);

    return settings_string;
}

// Read and parse the settings from a file into the struct
bool read_and_parse_settings_file(File* file, const char* file_path, Settings* settings) {
    storage_file_seek(file, 0, true);

    uint64_t file_size = storage_file_size(file);
    FURI_LOG_I("Ghost ESP", "File size: %llu", file_size);

    if(file_size < 4) { // Ensure there are 4 bytes
        FURI_LOG_I("Ghost ESP", "File is invalid or empty: %s\n", file_path);
        return false;
    }

    char buffer[5]; // 4 bytes for settings, 1 for null terminator
    int bytes_read = storage_file_read(file, buffer, 4);
    FURI_LOG_I("Ghost ESP", "Bytes read: %d", bytes_read);

    buffer[4] = '\0';

    FURI_LOG_I("Ghost ESP", "Buffer contents (hex): %02X %02X %02X %02X",
               buffer[0], buffer[1], buffer[2], buffer[3]);

    if(buffer[0] >= '0' && buffer[0] <= '2') {
        settings->rgb_mode_index = buffer[0] - '0';
    } else {
        FURI_LOG_I("Ghost ESP", "Invalid RGB Mode Index: %c", buffer[0]);
        return false;
    }

    if(buffer[1] >= '0' && buffer[1] <= '9') {
        settings->channel_hop_delay_index = buffer[1] - '0';
    } else {
        FURI_LOG_I("Ghost ESP", "Invalid Channel Hop Delay Index: %c", buffer[1]);
        return false;
    }

    if(buffer[2] >= '0' && buffer[2] <= '1') {
        settings->enable_channel_hopping_index = buffer[2] - '0';
    } else {
        FURI_LOG_I("Ghost ESP", "Invalid Enable Channel Hopping Index: %c", buffer[2]);
        return false;
    }

    if(buffer[3] >= '0' && buffer[3] <= '1') {
        settings->enable_random_ble_mac_index = buffer[3] - '0';
    } else {
        FURI_LOG_I("Ghost ESP", "Invalid Random BLE MAC Index: %c", buffer[3]);
        return false;
    }

    return true;
}

// Write settings to the file
bool write_settings_to_file(File* file, Settings* settings) {
    if(!storage_file_seek(file, 0, true)) {
        FURI_LOG_I("Ghost ESP", "Failed to seek to the beginning of the file.\n");
        return false;
    }

    if(!storage_file_truncate(file)) {
        FURI_LOG_I("Ghost ESP", "Failed to truncate the file.\n");
        return false;
    }

    char* settings_string = create_settings_string(settings);
    if(settings_string == NULL) {
        return false;
    }

    size_t data_length = strlen(settings_string);
    size_t bytes_written = storage_file_write(file, settings_string, data_length);
    free(settings_string);

    if(bytes_written != data_length) {
        FURI_LOG_I("Ghost ESP", "Failed to write all data to the file.\n");
        return false;
    }

    return true;
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
    if(!read_and_parse_settings_file(app->uart_context->storageContext->settings_file,
                                     GHOST_ESP_APP_SETTINGS_FILE, &settings)) {
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
    if(!read_and_parse_settings_file(app->uart_context->storageContext->settings_file,
                                     GHOST_ESP_APP_SETTINGS_FILE, &settings)) {
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
    if(!read_and_parse_settings_file(app->uart_context->storageContext->settings_file,
                                     GHOST_ESP_APP_SETTINGS_FILE, &settings)) {
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
    if(!read_and_parse_settings_file(app->uart_context->storageContext->settings_file,
                                     GHOST_ESP_APP_SETTINGS_FILE, &settings)) {
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