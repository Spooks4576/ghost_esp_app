#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <stdio.h>
#include <stdlib.h>
#include <music_worker.h>
#include <expansion/expansion.h>
#include "menu.h"
#include "uart_utils.h"
#include "callbacks.h"

#define DEFAULT_BAUD_RATE 115200

/* generated by fbt from .png files in images folder */
#include <ghost_esp_icons.h>

int32_t ghost_esp_app(void* p) {
    UNUSED(p);

    uint8_t attempts = 0;
    bool otg_was_enabled = furi_hal_power_is_otg_enabled();
    while(!furi_hal_power_is_otg_enabled() && attempts++ < 5) {
        furi_hal_power_enable_otg();
        furi_delay_ms(10);
    }
    furi_delay_ms(200);

    // Set up UI
    AppState* state = malloc(sizeof(AppState));
    state->view_dispatcher = view_dispatcher_alloc();
    state->main_menu = main_menu_alloc();
    state->wifi_menu = submenu_alloc();
    state->ble_menu = submenu_alloc();
    state->gps_menu = submenu_alloc();
    state->text_box = text_box_alloc();
    state->settings_menu = variable_item_list_alloc();
    state->text_input = text_input_alloc();
    state->input_buffer = malloc(32);

    main_menu_set_header(state->main_menu, "Select a Utility");
    submenu_set_header(state->wifi_menu, "Select a Wifi Utility");
    submenu_set_header(state->ble_menu, "Select a Bluetooth Utility");
    submenu_set_header(state->gps_menu, "Select a GPS Utility");
    text_input_set_header_text(state->text_input, "Enter Your Text");

    // Initialize UART
    state->uart_context = uart_init(state);

    // Settings Menu
    VariableItem* item =
        variable_item_list_add(state->settings_menu, "RGB Mode", 3, on_rgb_mode_changed, state);
    VariableItem* item2 = variable_item_list_add(
        state->settings_menu, "Channel Switch Delay", 5, on_channelswitchdelay_changed, state);
    VariableItem* item3 = variable_item_list_add(
        state->settings_menu, "Enable Channel Hopping", 2, on_togglechannelhopping_changed, state);
    VariableItem* item4 = variable_item_list_add(
        state->settings_menu, "Enable Random BLE Mac", 2, on_ble_mac_changed, state);

    Settings settings;

    if(read_and_parse_settings_file(
           state->uart_context->storageContext->settings_file,
           GHOST_ESP_APP_SETTINGS_FILE,
           &settings)) {
        // Update RGBMode
        switch(settings.rgb_mode_index) {
        case 0:
            variable_item_set_current_value_index(item, 0);
            variable_item_set_current_value_text(item, "Stealth");
            break;
        case 1:
            variable_item_set_current_value_index(item, 1);
            variable_item_set_current_value_text(item, "Normal");
            break;
        case 2:
            variable_item_set_current_value_index(item, 2);
            variable_item_set_current_value_text(item, "Rainbow");
            break;
        }

        // Update ChannelHopDelay
        switch(settings.channel_hop_delay_index) {
        case 0:
            variable_item_set_current_value_index(item2, 0);
            variable_item_set_current_value_text(item2, "500ms");
            break;
        case 1:
            variable_item_set_current_value_index(item2, 1);
            variable_item_set_current_value_text(item2, "1000ms");
            break;
        case 2:
            variable_item_set_current_value_index(item2, 2);
            variable_item_set_current_value_text(item2, "2000ms");
            break;
        case 3:
            variable_item_set_current_value_index(item2, 3);
            variable_item_set_current_value_text(item2, "3000ms");
            break;
        case 4:
            variable_item_set_current_value_index(item2, 4);
            variable_item_set_current_value_text(item2, "4000ms");
            break;
        }

        // Update EnableChannelHopping
        switch(settings.enable_channel_hopping_index) {
        case 0:
            variable_item_set_current_value_index(item3, 0);
            variable_item_set_current_value_text(item3, "False");
            break;
        case 1:
            variable_item_set_current_value_index(item3, 1);
            variable_item_set_current_value_text(item3, "True");
            break;
        }
        switch(settings.enable_random_ble_mac_index) {
        case 0:
            variable_item_set_current_value_index(item4, 0);
            variable_item_set_current_value_text(item4, "False");
            break;
        case 1:
            variable_item_set_current_value_index(item4, 1);
            variable_item_set_current_value_text(item4, "True");
            break;
        }
    } else { // Fallback
        variable_item_set_current_value_index(item, 0);
        variable_item_set_current_value_text(item, "Stealth");
        variable_item_set_current_value_index(item2, 0);
        variable_item_set_current_value_text(item2, "500ms");
        variable_item_set_current_value_index(item3, 1);
        variable_item_set_current_value_text(item3, "True");
        variable_item_set_current_value_index(item4, 0);
        variable_item_set_current_value_text(item4, "False");
    }

    view_dispatcher_add_view(state->view_dispatcher, 0, main_menu_get_view(state->main_menu));
    view_dispatcher_add_view(state->view_dispatcher, 1, submenu_get_view(state->wifi_menu));
    view_dispatcher_add_view(state->view_dispatcher, 2, submenu_get_view(state->ble_menu));
    view_dispatcher_add_view(state->view_dispatcher, 3, submenu_get_view(state->gps_menu));
    view_dispatcher_add_view(
        state->view_dispatcher, 4, variable_item_list_get_view(state->settings_menu));
    view_dispatcher_add_view(state->view_dispatcher, 5, text_box_get_view(state->text_box));
    view_dispatcher_add_view(state->view_dispatcher, 6, text_input_get_view(state->text_input));

    // Show the main menu
    show_main_menu(state);

    // Open GUI
    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(state->view_dispatcher, gui, ViewDispatcherTypeFullscreen);

    view_dispatcher_set_navigation_event_callback(state->view_dispatcher, back_event_callback);
    view_dispatcher_set_event_callback_context(state->view_dispatcher, state);

    view_dispatcher_run(state->view_dispatcher);

    furi_record_close(RECORD_GUI);

    view_dispatcher_remove_view(state->view_dispatcher, 0);
    view_dispatcher_remove_view(state->view_dispatcher, 1);
    view_dispatcher_remove_view(state->view_dispatcher, 2);
    view_dispatcher_remove_view(state->view_dispatcher, 3);
    view_dispatcher_remove_view(state->view_dispatcher, 4);
    view_dispatcher_remove_view(state->view_dispatcher, 5);
    view_dispatcher_remove_view(state->view_dispatcher, 6);

    uart_free(state->uart_context);

    // Clean up
    text_input_free(state->text_input);
    text_box_free(state->text_box);
    main_menu_free(state->main_menu);
    submenu_free(state->wifi_menu);
    variable_item_list_free(state->settings_menu);
    submenu_free(state->ble_menu);
    submenu_free(state->gps_menu);
    view_dispatcher_free(state->view_dispatcher);
    free(state);

    // Have to put at bottom otherwise nullptr dereference
    if(furi_hal_power_is_otg_enabled() && !otg_was_enabled) {
        furi_hal_power_disable_otg();
    }

    return 0;
}
