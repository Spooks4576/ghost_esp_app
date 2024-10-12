#ifndef MENU_H
#define MENU_H

#include <furi_hal.h> // Or any necessary Flipper Zero headers
#include <gui/view_dispatcher.h>
#include <gui/modules/text_box.h>
#include <gui/modules/submenu.h>
#include "gui_modules/mainmenu.h"
#include <gui/modules/text_input.h>
#include <gui/modules/variable_item_list.h>

// Forward declare UartContext since it might be defined elsewhere
// (but already included by including uart_utils.h)
typedef struct UartContext UartContext;

typedef struct {
    ViewDispatcher* view_dispatcher;
    MainMenu* main_menu;
    Submenu* wifi_menu;
    Submenu* ble_menu;
    VariableItemList* settings_menu;
    Submenu* gps_menu;
    TextBox* text_box;
    TextInput* text_input;
    uint32_t current_view;
    uint32_t previous_view;
    UartContext* uart_context;
    char* textBoxBuffer;
    size_t buffer_length;
    char* input_buffer;
    const char* uart_command;
} AppState;

// Function declarations
void send_uart_command(const char* command, AppState* state);
void send_uart_command_with_text(const char* command, char* text, AppState* state);
void send_uart_command_with_bytes(
    const char* command,
    const uint8_t* bytes,
    size_t length,
    AppState* state);

bool back_event_callback(void* context);
void submenu_callback(void* context, uint32_t index);
void handle_wifi_commands(AppState* state, uint32_t index, const char** wifi_commands);
void show_main_menu(AppState* state);
void handle_wifi_menu(AppState* state, uint32_t index);
void handle_ble_menu(AppState* state, uint32_t index);
void handle_gps_menu(AppState* state, uint32_t index);

void show_wifi_menu(AppState* state);
void show_ble_menu(AppState* state);
void show_gps_menu(AppState* state);
#endif // MENU_H
