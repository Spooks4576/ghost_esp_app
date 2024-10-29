#include "uart_utils.h"
#include <furi.h>
#include <stdlib.h>
#include <string.h>
#include "sequential_file.h"
#include <gui/modules/text_box.h>


#define WORKER_ALL_RX_EVENTS (WorkerEvtStop | WorkerEvtRxDone | WorkerEvtPcapDone)

static bool is_ap_list_start(const char* line) {
    return strstr(line, "Found") != NULL && strstr(line, "access points:") != NULL;
}

static bool is_ap_list_entry(const char* line) {
    return strstr(line, "SSID:") != NULL && strstr(line, "BSSID:") != NULL && 
           strstr(line, "RSSI:") != NULL && strstr(line, "Company:") != NULL;
}

static bool should_filter_line(const char* line) {
    if(!line || strlen(line) == 0) return true;
    
    static bool in_ap_list = false;
    static int ap_count = 0;
    
    // If line contains multiple null characters or is corrupted, filter it
    bool has_corruption = false;
    size_t nulls = 0;
    for(size_t i = 0; i < strlen(line); i++) {
        if(line[i] == '\0') nulls++;
        if(nulls > 1) {
            has_corruption = true;
            break;
        }
    }
    if(has_corruption) return true;
    
    // Check if we're starting an AP list
    if(is_ap_list_start(line)) {
        in_ap_list = true;
        ap_count = 0;
        return false;  // Keep the header line
    }
    
    // If we're in an AP list, handle entries
    if(in_ap_list) {
        if(is_ap_list_entry(line)) {
            ap_count++;
            return false;  // Keep all AP entries
        } else if(strstr(line, "WiFiManager:") == NULL) {
            // Only exit AP list mode if this isn't another WiFiManager line
            in_ap_list = false;
        }
    }

    // Always keep these patterns (expanded list)
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
        NULL
    };

    for(int i = 0; keep_patterns[i]; i++) {
        if(strstr(line, keep_patterns[i])) return false;
    }

    // Filter out known noise (expanded list)
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

    for(int i = 0; filter_patterns[i]; i++) {
        if(strstr(line, filter_patterns[i])) return true;
    }

    return false;  // Keep by default
}

static void clean_text(char* str) {
    if(!str) return;
    
    // First pass: normalize spaces and remove artifacts
    char* write = str;
    char* read = str;
    bool last_was_space = true;  // Start true to trim leading spaces
    
    while(*read) {
        // Skip single-character splits
        if(*read == '\n' && (*(read+1) != '\0' && !isspace((unsigned char)*(read+1)))) {
            read++;
            continue;
        }
        
        // Normalize spaces
        if(isspace((unsigned char)*read)) {
            if(!last_was_space) {
                *write++ = ' ';
                last_was_space = true;
            }
        } else {
            *write++ = *read;
            last_was_space = false;
        }
        read++;
    }
    *write = '\0';
    
    // Remove trailing space
    if(write > str && *(write-1) == ' ') {
        *(write-1) = '\0';
    }
}

static void strip_ansi_codes(const char* input, char* output) {
    size_t j = 0;
    size_t input_len = strlen(input);
    bool in_escape = false;
    bool in_timestamp = false;
    char temp[RX_BUF_SIZE] = {0};
    size_t temp_idx = 0;
    
    for(size_t i = 0; i < input_len; i++) {
        unsigned char c = (unsigned char)input[i];
        
        // Handle escape sequences
        if(c == '\x1b' || (c == '[' && i > 0 && input[i-1] == '\x1b')) {
            in_escape = true;
            continue;
        }
        
        if(in_escape) {
            if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == 'm') {
                in_escape = false;
            }
            continue;
        }
        
        // Handle timestamps (e.g., "(1234567)")
        if(c == '(' && i + 1 < input_len && isdigit((unsigned char)input[i + 1])) {
            in_timestamp = true;
            continue;
        }
        if(in_timestamp) {
            if(c == ')') {
                in_timestamp = false;
            }
            continue;
        }
        
        // Skip any remaining color code fragments
        if(c == ';' && i + 2 < input_len && isdigit((unsigned char)input[i + 1]) && 
           input[i + 2] == 'm') {
            i += 2;
            continue;
        }
        
        // Collect character into temporary buffer
        temp[temp_idx++] = c;
        if(temp_idx >= RX_BUF_SIZE - 1) {
            clean_text(temp);
            strncpy(output + j, temp, RX_BUF_SIZE - j - 1);
            j += strlen(temp);
            temp_idx = 0;
        }
    }
    
    // Handle any remaining characters
    if(temp_idx > 0) {
        temp[temp_idx] = '\0';
        clean_text(temp);
        strncpy(output + j, temp, RX_BUF_SIZE - j - 1);
    }
    
    output[RX_BUF_SIZE - 1] = '\0';
    clean_text(output);
}

static bool format_line(const char* input, char* output, FilterConfig* config) {
    if(!input || !output || !config) return false;
    
    if(!config->enabled) {
        strncpy(output, input, RX_BUF_SIZE - 1);
        output[RX_BUF_SIZE - 1] = '\0';
        return true;
    }

    char* temp = malloc(RX_BUF_SIZE);
    if(!temp) return false;
    
    // Initial cleanup
    strncpy(temp, input, RX_BUF_SIZE - 1);
    temp[RX_BUF_SIZE - 1] = '\0';

    // Remove ANSI codes and clean up text
    if(config->strip_ansi_codes) {
        char* stripped = malloc(RX_BUF_SIZE);
        if(stripped) {
            strip_ansi_codes(temp, stripped);
            strncpy(temp, stripped, RX_BUF_SIZE - 1);
            free(stripped);
        }
    }

    // Remove additional artifacts and normalize
    clean_text(temp);

    // Fix common split patterns
    char* fixed = temp;
    while(*fixed) {
        if(strncmp(fixed, "Wi Fi", 5) == 0) {
            memmove(fixed + 4, fixed + 5, strlen(fixed + 5) + 1);
            memcpy(fixed, "WiFi", 4);
        }
        fixed++;
    }

    // Remove any remaining timestamps and ID numbers at start of lines
    char* start = temp;
    while(*start) {
        if(isdigit((unsigned char)*start) && strstr(start, "]")) {
            char* end = strstr(start, "]");
            if(end) {
                memmove(start, end + 1, strlen(end + 1) + 1);
                continue;
            }
        }
        break;
    }

    bool keep_line = !should_filter_line(start);
    
    if(keep_line) {
        const char* prefix = "";
        if(config->add_prefixes) {
            if(strstr(start, "WiFi") || strstr(start, "AP_MANAGER") || 
               strstr(start, "SSID:") || strstr(start, "BSSID:")) {
                prefix = "[WIFI] ";
            } else if(strstr(start, "BLE")) {
                prefix = "[BLE] ";
            } else if(strstr(start, "Found Flipper")) {
                prefix = "[FLIPPER] ";
            }
        }

        // Add prefix only if it's not already there
        if(strlen(prefix) > 0 && strncmp(start, prefix, strlen(prefix)) != 0) {
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

void handle_uart_rx_data(uint8_t *buf, size_t len, void *context) {
    AppState *state = (AppState *)context;
    if(!state || !state->uart_context || !state->uart_context->is_serial_active) return;

    const size_t MAX_BUFFER_SIZE = 2 * 1024;

    if(!state || !buf || len == 0) return;

    // Ensure proper null termination of input
    if(len >= RX_BUF_SIZE) len = RX_BUF_SIZE - 1;
    buf[len] = '\0';

    // Check if filtering is configured and enabled
    if(!state->filter_config || !state->filter_config->enabled) {
        // No filtering or filtering disabled - use original behavior
        size_t new_total_len = state->buffer_length + len + 1;

        // Handle buffer overflow (original logic)
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

        // Allocate or reallocate buffer (original logic)
        char *new_buffer = realloc(state->textBoxBuffer, new_total_len);
        if(!new_buffer) {
            FURI_LOG_E("UART", "Failed to allocate memory for text concatenation");
            return;
        }

        // Copy new data
        memcpy(new_buffer + state->buffer_length, buf, len);
        new_buffer[new_total_len - 1] = '\0';
        
        state->textBoxBuffer = new_buffer;
        state->buffer_length = new_total_len - 1;

        // Update text box and scroll to end
        text_box_set_text(state->text_box, state->textBoxBuffer);
        text_box_set_focus(state->text_box, TextBoxFocusEnd);

        // Write to log file if enabled
        if(state->uart_context->storageContext->log_file) {
            storage_file_write(state->uart_context->storageContext->log_file, buf, len);
        }
        return;
    }

    // Filtering is enabled - process line by line
    char* line_start = (char*)buf;
    char* current = line_start;
    char* filtered_buffer = malloc(MAX_BUFFER_SIZE);
    size_t filtered_len = 0;

    if(!filtered_buffer) {
        FURI_LOG_E("UART", "Failed to allocate filter buffer");
        return;
    }

    filtered_buffer[0] = '\0';

    // Process each line through the filter
    while(current < (char*)buf + len) {
        if(*current == '\n' || current == (char*)buf + len - 1) {
            size_t line_len = current - line_start + 1;
            char* line_buf = malloc(line_len + 1);
            
            if(line_buf) {
                memcpy(line_buf, line_start, line_len);
                line_buf[line_len] = '\0';

                char output_line[RX_BUF_SIZE];
                if(format_line(line_buf, output_line, state->filter_config)) {
                    size_t output_len = strlen(output_line);
                    if(filtered_len + output_len < MAX_BUFFER_SIZE - 2) {
                        strcat(filtered_buffer, output_line);
                        strcat(filtered_buffer, "\n");
                        filtered_len += output_len + 1;
                    }
                }
                free(line_buf);
            }
            line_start = current + 1;
        }
        current++;
    }

    // Only proceed if we have filtered content
    if(filtered_len > 0) {
        size_t new_total_len = state->buffer_length + filtered_len + 1;

        // Handle buffer overflow
        if(new_total_len > MAX_BUFFER_SIZE) {
            size_t keep_size = MAX_BUFFER_SIZE / 2;
            if(state->textBoxBuffer && state->buffer_length > keep_size) {
                memmove(state->textBoxBuffer, 
                       state->textBoxBuffer + state->buffer_length - keep_size,
                       keep_size);
                state->buffer_length = keep_size;
                new_total_len = keep_size + filtered_len + 1;
            } else {
                free(state->textBoxBuffer);
                state->textBoxBuffer = NULL;
                state->buffer_length = 0;
                new_total_len = filtered_len + 1;
            }
        }

        // Allocate or reallocate buffer
        char *new_buffer = realloc(state->textBoxBuffer, new_total_len);
        if(new_buffer) {
            if(state->buffer_length == 0) {
                new_buffer[0] = '\0';
            }
            strcat(new_buffer, filtered_buffer);
            state->textBoxBuffer = new_buffer;
            state->buffer_length = strlen(new_buffer);

            // Update text box and scroll to end
            text_box_set_text(state->text_box, state->textBoxBuffer);
            text_box_set_focus(state->text_box, TextBoxFocusEnd);

            // Write to log file if enabled
            if(state->uart_context->storageContext->log_file) {
                storage_file_write(
                    state->uart_context->storageContext->log_file, 
                    filtered_buffer, 
                    filtered_len);
            }
        } else {
            FURI_LOG_E("UART", "Failed to allocate memory for text concatenation");
        }
    }

    free(filtered_buffer);
}

// UART receive callback function
static void
uart_rx_callback(FuriHalSerialHandle *handle, FuriHalSerialRxEvent event, void *context)
{
    UartContext *uart = (UartContext *)context;

    if (event == FuriHalSerialRxEventData)
    {
        uint8_t data = furi_hal_serial_async_rx(handle);

        const char *mark_begin = "[BUF/BEGIN]";
        const char *mark_close = "[BUF/CLOSE]";

        if (uart->mark_test_idx != 0)
        {
            if (data == mark_begin[uart->mark_test_idx] ||
                data == mark_close[uart->mark_test_idx])
            {
                uart->mark_test_buf[uart->mark_test_idx++] = data;

                if (uart->mark_test_idx == sizeof(uart->mark_test_buf))
                {
                    if (!memcmp(
                            uart->mark_test_buf, (void *)mark_begin, sizeof(uart->mark_test_buf)))
                    {
                        uart->pcap = true;
                    }
                    else if (!memcmp(
                                 uart->mark_test_buf,
                                 (void *)mark_close,
                                 sizeof(uart->mark_test_buf)))
                    {
                        uart->pcap = false;
                    }
                    else
                    {
                    }

                    uart->mark_test_idx = 0;
                }
                return;
            }
            else
            {
                if (uart->pcap)
                {
                    furi_stream_buffer_send(
                        uart->pcap_stream, uart->mark_test_buf, uart->mark_test_idx, 0);
                    furi_thread_flags_set(furi_thread_get_id(uart->rx_thread), WorkerEvtPcapDone);
                }
                else
                {
                    furi_stream_buffer_send(
                        uart->rx_stream, uart->mark_test_buf, uart->mark_test_idx, 0);
                    furi_thread_flags_set(furi_thread_get_id(uart->rx_thread), WorkerEvtRxDone);
                }
                uart->mark_test_idx = 0;
            }
        }

        if (data == mark_begin[0])
        {
            uart->mark_test_buf[uart->mark_test_idx++] = data;
        }
        else
        {
            if (uart->pcap)
            {
                furi_stream_buffer_send(uart->pcap_stream, &data, 1, 0);
                furi_thread_flags_set(furi_thread_get_id(uart->rx_thread), WorkerEvtPcapDone);
            }
            else
            {
                furi_stream_buffer_send(uart->rx_stream, &data, 1, 0);
                furi_thread_flags_set(furi_thread_get_id(uart->rx_thread), WorkerEvtRxDone);
            }
        }
    }
    else
    {
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
        furi_thread_set_stack_size(uart->rx_thread, 1024);
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
    const char* test_cmd = "info\n";
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