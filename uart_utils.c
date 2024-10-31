#include "uart_utils.h"
#include <furi.h>
#include <stdlib.h>
#include <string.h>
#include "sequential_file.h"
#include <gui/modules/text_box.h>
#include <furi_hal_serial.h>

#define WORKER_ALL_RX_EVENTS (WorkerEvtStop | WorkerEvtRxDone | WorkerEvtPcapDone)

#define AP_LIST_TIMEOUT_MS 5000

typedef struct {
    bool in_ap_list;
    int ap_count;
    int expected_ap_count;
} APListState;

static APListState ap_state = {
    .in_ap_list = false,
    .ap_count = 0,
    .expected_ap_count = 0
};

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

static bool process_chunk(LineProcessingState* state, const char* input, 
                         char* output, size_t output_size,
                         size_t* output_len);

static bool format_line(const char* input, char* output, FilterConfig* config);


static void uart_rx_callback(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context) {
    UartContext* uart = (UartContext*)context;
    
    if(event == FuriHalSerialRxEventData) {
        uint8_t data = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(uart->rx_stream, &data, 1, 0);
        furi_thread_flags_set(furi_thread_get_id(uart->rx_thread), WorkerEvtRxDone);
    }
}

static bool process_chunk(LineProcessingState* state, const char* input, 
                         char* output, size_t output_size,
                         size_t* output_len) {
    UNUSED(output_size);  // Properly mark as unused
    
    // First, append new input to partial buffer if we have any
    size_t available = RX_BUF_SIZE - state->partial_len;
    size_t input_len = strlen(input);
    
    if (input_len > available) {
        // Buffer would overflow - process what we can
        size_t processable = available;
        while (processable > 0 && 
               !isspace((unsigned char)input[processable-1]) && 
               processable < input_len) {
            processable--;
        }
        
        memcpy(state->partial_buffer + state->partial_len, 
               input, processable);
        state->partial_len += processable;
        
        // Process complete line
        char* newline = strchr(state->partial_buffer, '\n');
        if (newline) {
            size_t line_len = newline - state->partial_buffer;
            memcpy(output, state->partial_buffer, line_len);
            output[line_len] = '\0';
            *output_len = line_len;
            
            // Move remaining data to start of buffer
            memmove(state->partial_buffer, newline + 1,
                   state->partial_len - (line_len + 1));
            state->partial_len -= (line_len + 1);
            
            return true;
        }
    } else {
        // Append all input
        memcpy(state->partial_buffer + state->partial_len,
               input, input_len);
        state->partial_len += input_len;
    }
    
    return false;
}



static bool is_ap_list_start(const char* line) {
    if (!line) return false;
    
    if (strstr(line, "Found") && strstr(line, "access points:")) {
        // Extract expected AP count
        const char* count_start = strstr(line, "Found") + 6;
        char* end;
        int count = strtol(count_start, &end, 10);
        if (count > 0) {
            ap_state.expected_ap_count = count;
        }
        return true;
    }
    return false;
}

static bool is_ap_list_entry(const char* line) {
    if (!line) return false;
    return strstr(line, "SSID:") != NULL && 
           strstr(line, "BSSID:") != NULL && 
           strstr(line, "RSSI:") != NULL && 
           strstr(line, "Company:") != NULL;
}

static bool should_filter_line(const char* line) {
    if (!line || strlen(line) == 0) return true;
    
    // Check for AP list completion based on count instead of timeout
    if (ap_state.in_ap_list && ap_state.ap_count >= ap_state.expected_ap_count) {
        ap_state.in_ap_list = false;
        ap_state.ap_count = 0;
    }
    
    // Corruption check with improved null handling
    bool has_corruption = false;
    size_t line_len = strlen(line);
    for (size_t i = 0; i < line_len; i++) {
        if (line[i] == '\0' && i < line_len - 1) {
            has_corruption = true;
            break;
        }
    }
    if (has_corruption) return true;
    
    // Improved AP list state management
    if (is_ap_list_start(line)) {
        ap_state.in_ap_list = true;
        ap_state.ap_count = 0;
        return false;
    }
    
    if (ap_state.in_ap_list) {
        if (is_ap_list_entry(line)) {
            ap_state.ap_count++;
            return false;
        } else if (strstr(line, "WiFiManager:") == NULL) {
            ap_state.in_ap_list = false;
        }
    }

    // Enhanced pattern matching with context awareness
    const char* keep_patterns[] = {
        "WiFiManager:",
        "BLE_MANAGER:",
        "ESP32",
        "WiFi scan",
        "scan started",
        "scan stopped",
        "monitor mode",
        "AP count",
        "Added station",
        "HTTP server",
        "DHCP server",
        "IP Address:",
        "AP IP Address:",
        "ready to scan",
        "mode : sta",      // Added to keep important mode changes
        "mode : softAP",   // Added to keep important mode changes
        NULL
    };

    const char* filter_patterns[] = {
        "No deauth transmission",
        "wifi:flush txq",
        "wifi:stop sw txq",
        "wifi:lmac stop hw",
        "wifi:enable tsf",
        "wifi:Total power save buffer",
        "wifi:Init max length of beacon",
        "wifi:new:",
        "wifi:station:",
        "wifi:<ba-",
        "own_addr_type=",
        "duration=forever",
        NULL
    };

    // Check keep patterns first
    for (int i = 0; keep_patterns[i]; i++) {
        if (strstr(line, keep_patterns[i])) return false;
    }

    // Then check filter patterns
    for (int i = 0; filter_patterns[i]; i++) {
        if (strstr(line, filter_patterns[i])) return true;
    }

    return false;
}

static void clean_text(char* str) {
    if (!str) return;
    
    char* write = str;
    const char* read = str;
    bool in_ansi = false;
    bool last_was_space = true;
    char last_char = 0;
    
    while (*read) {
        unsigned char c = (unsigned char)*read;
        
        // ANSI escape sequence handling with improved state tracking
        if (c == '\x1b' || (c == '[' && last_char == '\x1b')) {
            in_ansi = true;
            read++;
            last_char = c;
            continue;
        }
        
        if (in_ansi) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == 'm') {
                in_ansi = false;
            }
            read++;
            last_char = c;
            continue;
        }
        
        // Space normalization with improved boundary checks
        if (isspace((unsigned char)*read)) {
            if (!last_was_space && write > str) {
                *write++ = ' ';
                last_was_space = true;
            }
        } else {
            *write++ = *read;
            last_was_space = false;
        }
        
        read++;
        last_char = c;
    }
    
    // Null terminate and handle trailing space
    if (write > str && *(write - 1) == ' ') {
        write--;
    }
    *write = '\0';
}

static void strip_ansi_codes(const char* input, char* output) {
    if (!input || !output) return;
   
    size_t j = 0;
    size_t input_len = strlen(input);
    bool in_escape = false;
    bool in_timestamp = false;
    char* temp = malloc(RX_BUF_SIZE);
    if (!temp) {
        FURI_LOG_E("UART", "Failed to allocate temp buffer");
        return;
    }
    memset(temp, 0, RX_BUF_SIZE);
    
    size_t temp_idx = 0;
    char last_char = 0;
    char next_char = 0;
   
    for (size_t i = 0; i < input_len; i++) {
        unsigned char c = (unsigned char)input[i];
        next_char = (i + 1 < input_len) ? input[i + 1] : 0;
       
        // Improved escape sequence handling
        if (c == '\x1b' || (c == '[' && last_char == '\x1b')) {
            in_escape = true;
            last_char = c;
            continue;
        }
       
        if (in_escape) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == 'm') {
                in_escape = false;
            }
            last_char = c;
            continue;
        }
       
        // Enhanced timestamp handling with boundary protection
        if (c == '(' && next_char && isdigit((unsigned char)next_char)) {
            in_timestamp = true;
            continue;
        }
        if (in_timestamp) {
            if (c == ')') {
                in_timestamp = false;
                if (next_char && !isspace((unsigned char)next_char)) {
                    if (temp_idx < RX_BUF_SIZE - 1) {
                        temp[temp_idx++] = ' ';
                    }
                }
            }
            continue;
        }
       
        // Improved color code fragment handling with boundary check
        if (c == ';' && i + 2 < input_len &&
            isdigit((unsigned char)next_char) && input[i + 2] == 'm') {
            i += 2;
            continue;
        }
       
        // Enhanced buffer management with word boundary protection
        if (temp_idx >= RX_BUF_SIZE - 2) {
            char* last_space = strrchr(temp, ' ');
            if (last_space) {
                size_t keep_len = last_space - temp;
                if (j + keep_len < RX_BUF_SIZE) {
                    memcpy(output + j, temp, keep_len);
                    j += keep_len;
                   
                    // Move remaining content with overlap protection
                    size_t remaining = temp_idx - (keep_len + 1);
                    if (remaining > 0) {
                        memmove(temp, last_space + 1, remaining);
                        temp_idx = remaining;
                    } else {
                        temp_idx = 0;
                    }
                }
            } else {
                if (j + temp_idx < RX_BUF_SIZE) {
                    memcpy(output + j, temp, temp_idx);
                    j += temp_idx;
                    temp_idx = 0;
                }
            }
        }
       
        // Add character with boundary check
        if (temp_idx < RX_BUF_SIZE - 1) {
            temp[temp_idx++] = c;
        }
        last_char = c;
    }
   
    // Handle remaining characters with improved cleanup
    if (temp_idx > 0) {
        temp[temp_idx] = '\0';
        clean_text(temp);
        size_t remaining_len = strlen(temp);
        if (j + remaining_len < RX_BUF_SIZE) {
            memcpy(output + j, temp, remaining_len);
            j += remaining_len;
        }
    }
   
    output[j] = '\0';
    free(temp);
    clean_text(output);
}

static bool format_line(const char* input, char* output, FilterConfig* config) {
    if (!input || !output || !config) return false;
    
    if (!config->enabled) {
        strncpy(output, input, RX_BUF_SIZE - 1);
        output[RX_BUF_SIZE - 1] = '\0';
        return true;
    }

    char* temp = malloc(RX_BUF_SIZE);
    if (!temp) return false;
    
    // Initial cleanup with improved buffer management
    strncpy(temp, input, RX_BUF_SIZE - 1);
    temp[RX_BUF_SIZE - 1] = '\0';

    // Enhanced ANSI code handling
    if (config->strip_ansi_codes) {
        char* stripped = malloc(RX_BUF_SIZE);
        if (stripped) {
            strip_ansi_codes(temp, stripped);
            strncpy(temp, stripped, RX_BUF_SIZE - 1);
            free(stripped);
        }
    }

    clean_text(temp);

    // Improved pattern fixing
    char* fixed = temp;
    while (*fixed) {
        if (strncmp(fixed, "Wi Fi", 5) == 0) {
            memmove(fixed + 4, fixed + 5, strlen(fixed + 5) + 1);
            memcpy(fixed, "WiFi", 4);
        }
        // Add additional pattern fixes here if needed
        fixed++;
    }

    // Enhanced timestamp and ID cleanup
    char* start = temp;
    while (*start) {
        if (isdigit((unsigned char)*start) && strstr(start, "]")) {
            char* end = strstr(start, "]");
            if (end) {
                memmove(start, end + 1, strlen(end + 1) + 1);
                while (*start == ' ') start++; // Remove leading spaces
                continue;
            }
        }
        break;
    }

    bool keep_line = !should_filter_line(start);
    
    if (keep_line) {
        const char* prefix = "";
        if (config->add_prefixes) {
            if (strstr(start, "WiFi") || strstr(start, "AP_MANAGER") || 
                strstr(start, "SSID:") || strstr(start, "BSSID:")) {
                prefix = "[WIFI] ";
            } else if (strstr(start, "BLE")) {
                prefix = "[BLE] ";
            } else if (strstr(start, "Found Flipper")) {
                prefix = "[FLIPPER] ";
            }
        }

        // More robust prefix handling
        if (strlen(prefix) > 0 && strncmp(start, prefix, strlen(prefix)) != 0) {
            snprintf(output, RX_BUF_SIZE - 1, "%s%s", prefix, start);
        } else {
            strncpy(output, start, RX_BUF_SIZE - 1);
        }
        output[RX_BUF_SIZE - 1] = '\0';
        clean_text(output);
    }
    
    free(temp);
    return keep_line;
}
static bool format_line_with_state(LineProcessingState* state, 
                                 const char* input,
                                 char* output, 
                                 FilterConfig* config) {
    size_t output_len = 0;
    
    if (!process_chunk(state, input, output, RX_BUF_SIZE, &output_len)) {
        return false;
    }
    
    char* temp_buffer = malloc(RX_BUF_SIZE);
    if (!temp_buffer) return false;
    
    strncpy(temp_buffer, output, RX_BUF_SIZE - 1);
    temp_buffer[RX_BUF_SIZE - 1] = '\0';
    
    bool result = format_line(temp_buffer, output, config);
    
    free(temp_buffer);
    return result;
}

void handle_uart_rx_data(uint8_t *buf, size_t len, void *context) {
    AppState *state = (AppState *)context;
    static LineProcessingState line_state = {0};  // Add state here
    
    if(!state || !state->uart_context || !state->uart_context->is_serial_active) return;
    if(!buf || len == 0) return;

    const size_t MAX_BUFFER_SIZE = 2 * 1024;

    // Ensure proper null termination of input
    if(len >= RX_BUF_SIZE) len = RX_BUF_SIZE - 1;
    buf[len] = '\0';

    // Check if filtering is configured and enabled
    if(!state->filter_config || !state->filter_config->enabled) {
        // Original unfiltered handling
        size_t new_total_len = state->buffer_length + len + 1;

        // Handle buffer overflow
        if(new_total_len > MAX_BUFFER_SIZE) {
            size_t keep_size = MAX_BUFFER_SIZE / 2;
            if(state->textBoxBuffer && state->buffer_length > keep_size) {
                memmove(state->textBoxBuffer, 
                       state->textBoxBuffer + state->buffer_length - keep_size,
                       keep_size);
                state->buffer_length = keep_size;
                new_total_len = keep_size + len + 1;
            } else {
                free(state->textBoxBuffer);
                state->textBoxBuffer = NULL;
                state->buffer_length = 0;
                new_total_len = len + 1;
            }
        }

        // Allocate or reallocate buffer
        char *new_buffer = realloc(state->textBoxBuffer, new_total_len);
        if(!new_buffer) {
            FURI_LOG_E("UART", "Failed to allocate memory for text concatenation");
            return;
        }

        memcpy(new_buffer + state->buffer_length, buf, len);
        new_buffer[new_total_len - 1] = '\0';
        
        state->textBoxBuffer = new_buffer;
        state->buffer_length = new_total_len - 1;

        // Update text box
        text_box_set_text(state->text_box, state->textBoxBuffer);
        text_box_set_focus(state->text_box, TextBoxFocusEnd);

        // Write to log file if enabled
        if(state->uart_context->storageContext->log_file) {
            storage_file_write(state->uart_context->storageContext->log_file, buf, len);
        }
    } else {
        // Use the state-based line processing
        char output[RX_BUF_SIZE];
        if (format_line_with_state(&line_state, (char*)buf, output, state->filter_config)) {
            // Only handle complete lines
            size_t output_len = strlen(output);
            size_t new_total_len = state->buffer_length + output_len + 2; // +2 for newline and null

            // Handle buffer overflow
            if(new_total_len > MAX_BUFFER_SIZE) {
                size_t keep_size = MAX_BUFFER_SIZE / 2;
                if(state->textBoxBuffer && state->buffer_length > keep_size) {
                    memmove(state->textBoxBuffer, 
                           state->textBoxBuffer + state->buffer_length - keep_size,
                           keep_size);
                    state->buffer_length = keep_size;
                    new_total_len = keep_size + output_len + 2;
                } else {
                    free(state->textBoxBuffer);
                    state->textBoxBuffer = NULL;
                    state->buffer_length = 0;
                    new_total_len = output_len + 2;
                }
            }

            // Allocate or reallocate buffer
            char *new_buffer = realloc(state->textBoxBuffer, new_total_len);
            if(new_buffer) {
                if(state->buffer_length == 0) {
                    new_buffer[0] = '\0';
                }
                strcat(new_buffer, output);
                strcat(new_buffer, "\n");
                state->textBoxBuffer = new_buffer;
                state->buffer_length = strlen(new_buffer);

                // Update text box
                text_box_set_text(state->text_box, state->textBoxBuffer);
                text_box_set_focus(state->text_box, TextBoxFocusEnd);

                // Write to log file if enabled
                if(state->uart_context->storageContext->log_file) {
                    storage_file_write(
                        state->uart_context->storageContext->log_file, 
                        output, 
                        strlen(output));
                    storage_file_write(
                        state->uart_context->storageContext->log_file,
                        "\n",
                        1);
                }
            }
        }
    }
}
// UART worker thread function
static int32_t uart_worker(void *context) {
    UartContext *uart = (void *)context;
    while (1) {
        uint32_t events =
            furi_thread_flags_wait(WORKER_ALL_RX_EVENTS, FuriFlagWaitAny, FuriWaitForever);
        furi_check((events & FuriFlagError) == 0);
        if (events & WorkerEvtStop)
            break;
        if (events & WorkerEvtRxDone) {
            size_t len = furi_stream_buffer_receive(uart->rx_stream, uart->rx_buf, RX_BUF_SIZE, 0);
            if (len > 0) {
                if (uart->handle_rx_data_cb)
                    uart->handle_rx_data_cb(uart->rx_buf, len, uart->state);
            }
        }
        if (events & WorkerEvtPcapDone) {
            size_t len =
                furi_stream_buffer_receive(uart->pcap_stream, uart->rx_buf, RX_BUF_SIZE, 0);
            if (len > 0) {
                if (uart->handle_rx_pcap_cb)
                    uart->handle_rx_pcap_cb(uart->rx_buf, len, uart);
            }
        }
    }
    // Removed stream buffer frees from here
    return 0;
}

UartContext *uart_init(AppState *state) {
    uint32_t start_time = furi_get_tick();
    FURI_LOG_I("UART", "Starting UART initialization");
    
    UartContext *uart = malloc(sizeof(UartContext));
    if (!uart) {
        FURI_LOG_E("UART", "Failed to allocate UART context");
        return NULL;
    }
    memset(uart, 0, sizeof(UartContext));

    // Set state first for proper error handling
    uart->state = state;
    uart->is_serial_active = false;
    uart->streams_active = false;
    uart->storage_active = false;

    // Stream initialization timing
    uint32_t stream_start = furi_get_tick();
    FURI_LOG_D("UART", "Starting stream allocation");
    
    uart->rx_stream = furi_stream_buffer_alloc(RX_BUF_SIZE, 1);
    uart->pcap_stream = furi_stream_buffer_alloc(RX_BUF_SIZE, 1);
    uart->storage_stream = furi_stream_buffer_alloc(RX_BUF_SIZE, 1);
    uart->streams_active = (uart->rx_stream && uart->pcap_stream && uart->storage_stream);
    
    FURI_LOG_D("UART", "Stream allocation completed in %lums", furi_get_tick() - stream_start);

    // Set callbacks
    uart->handle_rx_data_cb = handle_uart_rx_data;
    uart->handle_rx_pcap_cb = uart_storage_rx_callback;

    // Thread initialization timing
    uint32_t thread_start = furi_get_tick();
    FURI_LOG_D("UART", "Starting thread initialization");
    
    uart->rx_thread = furi_thread_alloc();
    if (uart->rx_thread) {
        furi_thread_set_name(uart->rx_thread, "UART_Receive");
        furi_thread_set_stack_size(uart->rx_thread, 2048);
        furi_thread_set_context(uart->rx_thread, uart);
        furi_thread_set_callback(uart->rx_thread, uart_worker);
        furi_thread_start(uart->rx_thread);
        FURI_LOG_D("UART", "Thread initialization completed in %lums", furi_get_tick() - thread_start);
    } else {
        FURI_LOG_E("UART", "Failed to allocate RX thread");
    }

    // Storage initialization timing
    uint32_t storage_start = furi_get_tick();
    FURI_LOG_D("UART", "Starting storage initialization");
    
    uart->storageContext = uart_storage_init(uart);
    uart->storage_active = (uart->storageContext != NULL);
    
    FURI_LOG_D("UART", "Storage initialization completed in %lums", furi_get_tick() - storage_start);

    // Serial initialization timing
    uint32_t serial_start = furi_get_tick();
    FURI_LOG_D("UART", "Starting serial initialization");
    
    uart->serial_handle = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if (uart->serial_handle) {
        furi_hal_serial_init(uart->serial_handle, 115200);
        uart->is_serial_active = true;
        furi_hal_serial_async_rx_start(uart->serial_handle, uart_rx_callback, uart, false);
        FURI_LOG_D("UART", "Serial initialization completed in %lums", furi_get_tick() - serial_start);
    } else {
        FURI_LOG_E("UART", "Failed to acquire serial handle");
    }

    // Log final status
    uint32_t total_time = furi_get_tick() - start_time;
    FURI_LOG_I("UART", "UART initialization completed in %lums. Status:", total_time);
    FURI_LOG_I("UART", "- Streams: %s", uart->streams_active ? "Active" : "Failed");
    FURI_LOG_I("UART", "- Storage: %s", uart->storage_active ? "Active" : "Failed");
    FURI_LOG_I("UART", "- Serial: %s", uart->is_serial_active ? "Active" : "Failed");

    return uart;
}

// Cleanup UART resources and stop UART thread
void uart_free(UartContext *uart) {
    if (!uart) return;

    // Stop receiving new data first
    if (uart->serial_handle) {
        furi_hal_serial_async_rx_stop(uart->serial_handle);
        furi_delay_ms(10); // Small delay to ensure no more data is coming
    }

    // Stop thread and wait for it to finish
    if (uart->rx_thread) {
        furi_thread_flags_set(furi_thread_get_id(uart->rx_thread), WorkerEvtStop);
        furi_thread_join(uart->rx_thread);
        furi_thread_free(uart->rx_thread);
    }

    // Clean up serial after thread is stopped
    if (uart->serial_handle) {
        furi_hal_serial_deinit(uart->serial_handle);
        furi_hal_serial_control_release(uart->serial_handle);
    }

    // Free streams after everything is stopped
    if (uart->rx_stream) furi_stream_buffer_free(uart->rx_stream);
    if (uart->pcap_stream) furi_stream_buffer_free(uart->pcap_stream);
    if (uart->storage_stream) furi_stream_buffer_free(uart->storage_stream);

    // Clean up storage context last
    if (uart->storageContext) uart_storage_free(uart->storageContext);

    free(uart);
}

// Stop the UART thread (typically when exiting)
void uart_stop_thread(UartContext *uart)
{
    if (uart && uart->rx_thread)
    {
        furi_thread_flags_set(furi_thread_get_id(uart->rx_thread), WorkerEvtStop);
    }
}

// Send data over UART
void uart_send(UartContext *uart, const uint8_t *data, size_t len) {
    // Only try to send if serial is active
    if (uart && uart->serial_handle && uart->is_serial_active) {
        furi_hal_serial_tx(uart->serial_handle, data, len);
    }
}
void uart_receive_data(
    UartContext *uart,
    ViewDispatcher *view_dispatcher,
    AppState *state,
    const char *prefix,
    const char *extension,
    const char *TargetFolder)
{
    UNUSED(uart);
    text_box_set_text(state->text_box, "");
    text_box_set_focus(state->text_box, TextBoxFocusEnd);
    if (strlen(prefix) > 1)
    {
        uart->storageContext->HasOpenedFile = sequential_file_open(
            uart->storageContext->storage_api,
            uart->storageContext->current_file,
            TargetFolder,
            prefix,
            extension);
    }
    view_dispatcher_switch_to_view(view_dispatcher, 5);
    state->current_view = 5;
}

bool uart_is_esp_connected(UartContext* uart) {
    FURI_LOG_D("UART", "Checking ESP connection...");
    
    // Basic validation
    if(!uart) {
        FURI_LOG_E("UART", "UART context is NULL");
        return false;
    }
    if(!uart->serial_handle) {
        FURI_LOG_E("UART", "UART serial handle is NULL");
        return false;
    }
    if(!uart->state) {
        FURI_LOG_E("UART", "UART state is NULL");
        return false;
    }

    // Log initial state
    FURI_LOG_D("UART", "Initial buffer state - length: %d, buffer: %p", 
               uart->state->buffer_length,
               uart->state->textBoxBuffer);

    // Save and disable filtering
    FilterConfig* saved_config = uart->state->filter_config;
    uart->state->filter_config = NULL;
    FURI_LOG_D("UART", "Temporarily disabled filtering");

    // Clear and initialize buffer
    if(uart->state->textBoxBuffer) {
        FURI_LOG_D("UART", "Freeing existing buffer");
        free(uart->state->textBoxBuffer);
    }
    uart->state->textBoxBuffer = malloc(1);
    if(!uart->state->textBoxBuffer) {
        FURI_LOG_E("UART", "Failed to allocate buffer");
        uart->state->filter_config = saved_config;
        return false;
    }
    uart->state->textBoxBuffer[0] = '\0';
    uart->state->buffer_length = 0;
    FURI_LOG_D("UART", "Initialized new buffer");

    // Send test command
    const char* test_cmd = "stop\n";
    uart_send(uart, (uint8_t*)test_cmd, strlen(test_cmd));
    FURI_LOG_D("UART", "Sent info command (%d bytes)", strlen(test_cmd));

    // Wait for response with more detailed logging
    bool connected = false;
    uint32_t start_time = furi_get_tick();
    uint32_t check_count = 0;
    
    while(furi_get_tick() - start_time < ESP_CHECK_TIMEOUT_MS) {
        check_count++;
        if(uart->state->textBoxBuffer && uart->state->buffer_length > 0) {
            FURI_LOG_D("UART", "Received response - length: %d, content: %.20s...", 
                      uart->state->buffer_length,
                      uart->state->textBoxBuffer);
            connected = true;
            break;
        }
        if(check_count % 10 == 0) { // Log every 100ms
            FURI_LOG_D("UART", "Waiting for response... Time elapsed: %lums", 
                      furi_get_tick() - start_time);
        }
        furi_delay_ms(10);
    }

    // Log timeout if it occurs
    if(!connected) {
        FURI_LOG_W("UART", "Connection check timed out after %lums", 
                   furi_get_tick() - start_time);
    }

    // Restore filtering
    uart->state->filter_config = saved_config;
    FURI_LOG_D("UART", "Restored filtering configuration");

    FURI_LOG_I("UART", "ESP connection check result: %s", connected ? "Connected" : "Not connected");
    return connected;
}