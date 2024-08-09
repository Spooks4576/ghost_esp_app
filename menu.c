#include "menu.h"
#include <stdlib.h>
#include <string.h>
#include "uart_utils.h"

// Helper function to send commands
void send_uart_command(const char* command, AppState* state) {
    uart_send(state->uart_context, (uint8_t*)command, strlen(command));
}

void send_uart_command_with_text(const char* command, const char* text, AppState* state) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s %s", command, text);
    uart_send(state->uart_context, (uint8_t*)buffer, strlen(buffer));
}

void send_uart_command_with_bytes(
    const char* command,
    const uint8_t* bytes,
    size_t length,
    AppState* state) {
    send_uart_command(command, state);
    uart_send(state->uart_context, bytes, length);
}

bool back_event_callback(void* context) {
    AppState* state = (AppState*)context;

    if(state->current_view == 5) {
        FURI_LOG_D("Ghost ESP", "Stopping Thread");
        if(state->textBoxBuffer) {
            free(state->textBoxBuffer);
            state->textBoxBuffer = malloc(1);
            if(state->textBoxBuffer) {
                state->textBoxBuffer[0] = '\0';
            }
            state->buffer_length = 0;
        }
        send_uart_command("stopscan", state);

        furi_delay_ms(
            700); // Delaying Here due to us stopping the buffer before recieving pcap data

        if(state->uart_context->storageContext->current_file &&
           storage_file_is_open(state->uart_context->storageContext->current_file)) {
            state->uart_context->storageContext->HasOpenedFile = false;
            FURI_LOG_D("DEBUG", "Storage File Closed");
        }

        // Return to the previous view
        switch(state->previous_view) {
        case 1: {
            show_wifi_menu(state);
            break;
        }
        case 2: {
            show_ble_menu(state);
            break;
        }
        case 3: {
            show_gps_menu(state);
            break;
        }
        }
    } else if(state->current_view != 0) {
        // If current_view is not 5 and not the main menu, go back to the main menu
        show_main_menu(state);
    } else if(state->current_view == 0) {
        // Stop the view dispatcher if already in the main menu
        view_dispatcher_stop(state->view_dispatcher);
    }

    return true;
}

void show_wifi_menu(AppState* state) {
    const char* wifi_labels[] = {
        "Scan WiFi APs",
        "Scan WiFi Stations",
        "List APs",
        "List Stations",
        "Select AP",
        "Select Station",
        "Add Random SSID",
        "Add SSID",
        "Beacon Spam (List)",
        "Beacon Spam (Random)",
        "Beacon Spam (Rickroll)",
        "Cast V2 Connect",
        "Dial Connect",
        "Deauth Detector",
        "Deauth Stations",
        "Sniff Raw Packets",
        "Sniff PMKID",
        "Sniff Probes",
        "Sniff PWN",
        "Sniff Deauth",
        "Calibrate",
        "Attack WPS"};

    submenu_reset(state->wifi_menu);
    submenu_set_header(state->wifi_menu, "WiFi Utilities:");
    for(unsigned int i = 0; i < sizeof(wifi_labels) / sizeof(wifi_labels[0]); i++) {
        submenu_add_item(state->wifi_menu, wifi_labels[i], i, submenu_callback, state);
    }
    view_dispatcher_switch_to_view(state->view_dispatcher, 1);
    state->current_view = 1;
    state->previous_view = 1;
}

void show_ble_menu(AppState* state) {
    const char* ble_labels[] = {
        "BLE Spam (Samsung)",
        "BLE Spam (Apple)",
        "BLE Spam (Google)",
        "BLE Spam (Windows)",
        "BLE Spam (All)",
        "Find the Flippers",
        "BLE Spam Detector",
        "AirTag Scanner",
        "Sniff Bluetooth"};

    submenu_reset(state->ble_menu);
    submenu_set_header(state->ble_menu, "BLE Utilities:");
    for(unsigned int i = 0; i < sizeof(ble_labels) / sizeof(ble_labels[0]); i++) {
        submenu_add_item(state->ble_menu, ble_labels[i], i, submenu_callback, state);
    }
    view_dispatcher_switch_to_view(state->view_dispatcher, 2);
    state->current_view = 2;
    state->previous_view = 2;
}

void show_gps_menu(AppState* state) {
    const char* gps_labels[] = {"Street Detector", "WarDrive"};
    submenu_reset(state->gps_menu);
    submenu_set_header(state->gps_menu, "GPS Utilities:");
    for(unsigned int i = 0; i < sizeof(gps_labels) / sizeof(gps_labels[0]); i++) {
        submenu_add_item(state->gps_menu, gps_labels[i], i, submenu_callback, state);
    }
    view_dispatcher_switch_to_view(state->view_dispatcher, 3);
    state->current_view = 3;
    state->previous_view = 3;
}

void handle_wifi_menu(AppState* state, uint32_t index) {
    const char* wifi_commands[] = {
        "scanap",
        "scansta",
        "list -a",
        "list -c",
        "select -a",
        "select -s",
        "ssid -a -g",
        "ssid -a -n",
        "attack -t beacon -l",
        "attack -t beacon -r",
        "attack -t rickroll",
        "castv2connect -s SSID -p PASSWORD -v Y7uhkyameuk",
        "dialconnect -s GSQ1 -p 7802378253 -t youtube -v Y7uhkyameuk",
        "deauthdetector -s SSID -p PASSWORD -w WebHookUrl",
        "attack -t deauth",
        "sniffraw",
        "sniffpmkid",
        "sniffprobe",
        "sniffpwn",
        "sniffdeauth",
        "calibrate",
        "attack -t wps_pwn"};

    handle_wifi_commands(state, index, wifi_commands);
}

void handle_ble_menu(AppState* state, uint32_t index) {
    const char* ble_commands[] = {
        "blespam -t samsung",
        "blespam -t apple",
        "blespam -t google",
        "blespam -t windows",
        "blespam -t all",
        "findtheflippers",
        "detectblespam",
        "airtagscan",
        "sniffbt"};

    if(index == 8) {
        send_uart_command(ble_commands[index], state);
        uart_receive_data(
            state->uart_context,
            state->view_dispatcher,
            state,
            "btscan",
            "pcap",
            GHOST_ESP_APP_FOLDER_PCAPS);
        return;
    }

    send_uart_command(ble_commands[index], state);
    uart_receive_data(state->uart_context, state->view_dispatcher, state, "", "", "");
}

void handle_gps_menu(AppState* state, uint32_t index) {
    const char* gps_commands[] = {"streetdetector", "wardrive"};
    send_uart_command(gps_commands[index], state);

    uart_receive_data(
        state->uart_context,
        state->view_dispatcher,
        state,
        "wardrive_scan",
        "csv",
        GHOST_ESP_APP_FOLDER_WARDRIVE);
}

void submenu_callback(void* context, uint32_t index) {
    AppState* state = (AppState*)context;

    switch(state->current_view) {
    case 0: // Main Menu
        switch(index) {
        case 0: // WiFi Utils
            show_wifi_menu(state);
            break;
        case 1: // BLE Utils
            show_ble_menu(state);
            break;
        case 2: // LED Utils
            show_gps_menu(state);
            break;
        case 3:
            view_dispatcher_switch_to_view(state->view_dispatcher, 4);
            state->current_view = 4;
            state->previous_view = 4;
            break;
        default:
            break;
        }
        break;

    case 1: // WiFi Menu
        handle_wifi_menu(state, index);
        break;
    case 2: // BLE Menu
        handle_ble_menu(state, index);
        break;
    case 3:
        handle_gps_menu(state, index);
        break;
    case 4:

        break;
    default:
        break;
    }
}

static void text_input_result_callback(void* context) {
    AppState* input_state = (AppState*)context;

    // Send the UART command if accepted

    send_uart_command_with_text(
        input_state->uart_command, input_state->input_buffer, (AppState*)input_state);

    uart_receive_data(
        input_state->uart_context, input_state->view_dispatcher, input_state, "", "", "");
}

// Function to handle WiFi commands
void handle_wifi_commands(AppState* state, uint32_t index, const char** wifi_commands) {
    switch(index) {
    case 4: // Select AP
    case 5: // Select Station
    case 7: // Add SSID
    case 6:
        state->uart_command = wifi_commands[index];
        text_input_set_result_callback(
            state->text_input, text_input_result_callback, state, state->input_buffer, 32, true);
        view_dispatcher_switch_to_view(state->view_dispatcher, 6);
        break;
    case 15:
        send_uart_command(wifi_commands[index], state);
        uart_receive_data(
            state->uart_context,
            state->view_dispatcher,
            state,
            "raw_capture",
            "pcap",
            GHOST_ESP_APP_FOLDER_PCAPS);
        break;
    case 16:
        send_uart_command(wifi_commands[index], state);
        uart_receive_data(
            state->uart_context,
            state->view_dispatcher,
            state,
            "pmkid_capture",
            "pcap",
            GHOST_ESP_APP_FOLDER_PCAPS);
        break;
    case 17:
        send_uart_command(wifi_commands[index], state);
        uart_receive_data(
            state->uart_context,
            state->view_dispatcher,
            state,
            "probes_capture",
            "pcap",
            GHOST_ESP_APP_FOLDER_PCAPS);
        break;
    case 18:
        send_uart_command(wifi_commands[index], state);
        uart_receive_data(
            state->uart_context,
            state->view_dispatcher,
            state,
            "pwn_capture",
            "pcap",
            GHOST_ESP_APP_FOLDER_PCAPS);
        break;
    case 19:
        send_uart_command(wifi_commands[index], state);
        uart_receive_data(
            state->uart_context,
            state->view_dispatcher,
            state,
            "deauth_capture",
            "pcap",
            GHOST_ESP_APP_FOLDER_PCAPS);
        break;
    default:
        send_uart_command(wifi_commands[index], state);
        uart_receive_data(state->uart_context, state->view_dispatcher, state, "", "", "");
        break;
    }
}

// Function to show the main menu
void show_main_menu(AppState* state) {
    main_menu_reset(state->main_menu);
    main_menu_set_header(state->main_menu, "Select a Utility:");
    main_menu_add_item(state->main_menu, "WiFi", 0, submenu_callback, state);
    main_menu_add_item(state->main_menu, "BLE", 1, submenu_callback, state);
    main_menu_add_item(state->main_menu, "GPS", 2, submenu_callback, state);
    main_menu_add_item(state->main_menu, "CONF", 3, submenu_callback, state);
    view_dispatcher_switch_to_view(state->view_dispatcher, 0);
    state->current_view = 0;
}
