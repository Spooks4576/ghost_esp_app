#ifndef UART_UTILS_H
#define UART_UTILS_H

#include <furi_hal_serial.h>
#include <furi_hal_serial_types.h>
#include <furi_hal_serial_control.h>
#include <furi/core/thread.h>
#include <furi/core/stream_buffer.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/text_box.h>
#include "menu.h"
#include "uart_storage.h"
#define TEXT_BOX_STORE_SIZE (4096)  // 4KB text box buffer size
#define RX_BUF_SIZE 1024
#define PCAP_BUF_SIZE 4096
#define STORAGE_BUF_SIZE 4096
#define GHOST_ESP_APP_FOLDER          "/ext/apps_data/ghost_esp"
#define GHOST_ESP_APP_FOLDER_PCAPS    "/ext/apps_data/ghost_esp/pcaps"
#define GHOST_ESP_APP_FOLDER_WARDRIVE "/ext/apps_data/ghost_esp/wardrive"
#define GHOST_ESP_APP_FOLDER_LOGS     "/ext/apps_data/ghost_esp/logs"
#define GHOST_ESP_APP_SETTINGS_FILE   "/ext/apps_data/ghost_esp/settings.ini"
#define ESP_CHECK_TIMEOUT_MS 100
void update_text_box_view(AppState* state);
void handle_uart_rx_data(uint8_t *buf, size_t len, void *context);
// First define base structures

typedef struct {
    bool in_ap_list;
    int ap_count;
    int expected_ap_count;
} APListState;

// 2. Then define LineProcessingState as it uses APListState
typedef struct {
    // Buffer for incomplete lines
    char partial_buffer[RX_BUF_SIZE];
    size_t partial_len;
    // State tracking
    bool in_escape;
    bool in_timestamp;
    char last_char;
    // AP list state
    APListState ap_state;
} LineProcessingState;

// 3. Then UartLineState as it uses LineProcessingState
typedef struct {
    LineProcessingState line_state;
    bool initialized;
} UartLineState;

typedef enum {
    WorkerEvtStop = (1 << 0),
    WorkerEvtRxDone = (1 << 1),
    WorkerEvtPcapDone = (1 << 2),
    WorkerEvtStorage = (1 << 3),
} WorkerEvtFlags;

// Then define the main context structure
typedef struct UartContext {
    FuriHalSerialHandle* serial_handle;
    FuriThread* rx_thread;
    FuriStreamBuffer* rx_stream;
    FuriStreamBuffer* pcap_stream;
    FuriStreamBuffer* storage_stream;
    bool pcap;
    uint8_t mark_test_buf[11];
    uint8_t mark_test_idx;
    uint8_t rx_buf[RX_BUF_SIZE + 1];
    void (*handle_rx_data_cb)(uint8_t* buf, size_t len, void* context);
    void (*handle_rx_pcap_cb)(uint8_t* buf, size_t len, void* context);
    void (*handle_storage_cb)(uint8_t* buf, size_t len, void* context);
    AppState* state;
    UartStorageContext* storageContext;
    bool is_serial_active;
    bool streams_active;
    bool storage_active;
    UartLineState* line_state;
} UartContext;

// Function prototypes
UartContext* uart_init(AppState* state);
void uart_free(UartContext* uart);
void uart_stop_thread(UartContext* uart);
void uart_send(UartContext* uart, const uint8_t* data, size_t len);
void uart_receive_data(
    UartContext* uart,
    ViewDispatcher* view_dispatcher,
    AppState* current_view,
    const char* prefix,
    const char* extension,
    const char* TargetFolder);
bool uart_is_esp_connected(UartContext* uart);
void uart_storage_reset_logs(UartStorageContext *ctx);

#endif