#include "uart_utils.h"
#include <furi.h>
#include <stdlib.h>
#include <string.h>
#include "sequential_file.h"
#include <gui/modules/text_box.h>
#include <furi_hal_serial.h>

#define WORKER_ALL_RX_EVENTS (WorkerEvtStop | WorkerEvtRxDone | WorkerEvtPcapDone)
#define PCAP_WRITE_CHUNK_SIZE 1024
#define AP_LIST_TIMEOUT_MS 5000
#define INITIAL_BUFFER_SIZE 2048
#define BUFFER_GROWTH_FACTOR 1.5
#define MAX_BUFFER_SIZE (8 * 1024)  // 8KB max
static FuriMutex* buffer_mutex = NULL;
#define MUTEX_TIMEOUT_MS 1000
#define BUFFER_CLEAR_SIZE 128



static void strip_ansi_codes(const char* input, char* output);
static bool process_chunk(LineProcessingState* state, const char* input, 
                         char* output, size_t output_size,
                         size_t* output_len);
static bool format_line(const char* input, char* output, FilterConfig* config);

static bool init_buffer_mutex() {
    if(!buffer_mutex) {
        buffer_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
        if(!buffer_mutex) {
            FURI_LOG_E("UART", "Failed to create buffer mutex");
            return false;
        }
    }
    return true;
}

static void uart_rx_callback(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context) {
    UartContext* uart = (UartContext*)context;
    
    if(event == FuriHalSerialRxEventData) {
        uint8_t data = furi_hal_serial_async_rx(handle);
        const char* mark_begin = "[BUF/BEGIN]";
        const char* mark_close = "[BUF/CLOSE]";

        if(uart->mark_test_idx != 0) {
            if(data == mark_begin[uart->mark_test_idx] || 
               data == mark_close[uart->mark_test_idx]) {
                uart->mark_test_buf[uart->mark_test_idx++] = data;
                
                if(uart->mark_test_idx == sizeof(uart->mark_test_buf)) {
                    if(!memcmp(uart->mark_test_buf, (void*)mark_begin, sizeof(uart->mark_test_buf))) {
                        uart->pcap = true;
                    } else if(!memcmp(uart->mark_test_buf, (void*)mark_close, sizeof(uart->mark_test_buf))) {
                        uart->pcap = false;
                    }
                    uart->mark_test_idx = 0;
                }
                return;
            } else {
                if(uart->pcap) {
                    furi_stream_buffer_send(uart->pcap_stream, uart->mark_test_buf, uart->mark_test_idx, 0);
                    furi_thread_flags_set(furi_thread_get_id(uart->rx_thread), WorkerEvtPcapDone);
                } else {
                    furi_stream_buffer_send(uart->rx_stream, uart->mark_test_buf, uart->mark_test_idx, 0);
                    furi_thread_flags_set(furi_thread_get_id(uart->rx_thread), WorkerEvtRxDone);
                }
                uart->mark_test_idx = 0;
            }
        }

        if(data == mark_begin[0]) {
            uart->mark_test_buf[uart->mark_test_idx++] = data;
        } else {
            if(uart->pcap) {
                furi_stream_buffer_send(uart->pcap_stream, &data, 1, 0);
                furi_thread_flags_set(furi_thread_get_id(uart->rx_thread), WorkerEvtPcapDone);
            } else {
                furi_stream_buffer_send(uart->rx_stream, &data, 1, 0);
                furi_thread_flags_set(furi_thread_get_id(uart->rx_thread), WorkerEvtRxDone);
            }
        }
    }
}

static bool process_chunk(LineProcessingState* state, const char* input, 
                         char* output, size_t output_size,
                         size_t* output_len) {
    if(!state || !input || !output || !output_len) return false;
    
    size_t input_len = strlen(input);
    if(input_len == 0) return false;

    // Handle case where we already have a complete line
    char* existing_newline = strchr(state->partial_buffer, '\n');
    if(existing_newline) {
        size_t line_len = existing_newline - state->partial_buffer;
        if(line_len >= output_size) line_len = output_size - 1;
        
        memcpy(output, state->partial_buffer, line_len);
        output[line_len] = '\0';
        *output_len = line_len;

        size_t remaining = state->partial_len - (line_len + 1);
        if(remaining > 0) {
            memmove(state->partial_buffer, existing_newline + 1, remaining);
            state->partial_len = remaining;
        } else {
            state->partial_len = 0;
        }
        state->partial_buffer[state->partial_len] = '\0';
        return true;
    }

    // Append new input if room
    size_t space = RX_BUF_SIZE - state->partial_len - 1;
    size_t to_copy = (input_len > space) ? space : input_len;
    
    memcpy(state->partial_buffer + state->partial_len, input, to_copy);
    state->partial_len += to_copy;
    state->partial_buffer[state->partial_len] = '\0';

    // Look for complete line in combined buffer
    char* newline = strchr(state->partial_buffer, '\n');
    if(newline) {
        size_t line_len = newline - state->partial_buffer;
        if(line_len >= output_size) line_len = output_size - 1;
        
        memcpy(output, state->partial_buffer, line_len);
        output[line_len] = '\0';
        *output_len = line_len;

        size_t remaining = state->partial_len - (line_len + 1);
        if(remaining > 0) {
            memmove(state->partial_buffer, newline + 1, remaining);
            state->partial_len = remaining;
        } else {
            state->partial_len = 0;
        }
        state->partial_buffer[state->partial_len] = '\0';
        return true;
    }

    // Force output if buffer nearly full
    if(state->partial_len >= RX_BUF_SIZE - 2) {
        size_t out_len = state->partial_len;
        if(out_len >= output_size) out_len = output_size - 1;
        
        memcpy(output, state->partial_buffer, out_len);
        output[out_len] = '\0';
        *output_len = out_len;
        
        state->partial_len = 0;
        state->partial_buffer[0] = '\0';
        return true;
    }

    return false;
}




static bool should_filter_line(const char* raw_line) {
    if(!raw_line || !*raw_line) return true;  // Filter empty lines

    // First strip ANSI and clean the line
    char clean_line[RX_BUF_SIZE];
    strip_ansi_codes(raw_line, clean_line);
    
    // Debug messages always filtered
    if(strstr(clean_line, "wifi:flush") ||
       strstr(clean_line, "wifi:stop") ||
       strstr(clean_line, "wifi:lmac") ||
       strstr(clean_line, "wifi:new:") ||
       strstr(clean_line, "wifi:station:") ||
       strstr(clean_line, "wifi:<ba-")) return true;

    // AP List handling - Keep all AP list related messages
    if(strstr(clean_line, "WiFiManager:") ||
       strstr(clean_line, "SSID:") ||
       strstr(clean_line, "BSSID:") ||
       (strstr(clean_line, "Found") && strstr(clean_line, "access points:"))) {
        return false;
    }

    // Keep important messages
    static const char* keep_patterns[] = {
        "BLE_MANAGER:", "ESP32",
        "WiFi scan", "scan started", "scan stopped",
        "monitor mode", "AP count", "Added station",
        "HTTP server", "DHCP server", "IP Address:",
        "AP IP Address:", "ready to scan",
        "mode : sta", "mode : softAP",
        NULL
    };

    // Debug patterns to filter
    static const char* filter_patterns[] = {
        "No deauth transmission", "wifi:flush txq",
        "wifi:stop sw txq", "wifi:lmac stop hw",
        "wifi:enable tsf", "wifi:Total power save buffer",
        "wifi:Init max length of beacon", "wifi:new:",
        "wifi:station:", "wifi:<ba-", "own_addr_type=",
        "duration=forever",
        NULL
    };

    // Check patterns
    for(int i = 0; keep_patterns[i]; i++) {
        if(strstr(clean_line, keep_patterns[i])) return false;
    }

    for(int i = 0; filter_patterns[i]; i++) {
        if(strstr(clean_line, filter_patterns[i])) return true;
    }
    
    return false; // Default to keeping line
}
static void clean_text(char* str) {
    if(!str) return;
    
    const char* read = str;
    char* write = str;
    bool in_escape = false;
    bool last_space = true;
    
    while(*read) {
        unsigned char c = (unsigned char)*read;
        
        if(c == '\x1b') {
            in_escape = true;
            read++;
            continue;
        }
        if(in_escape) {
            if((c >= 'A' && c <= 'Z') || 
               (c >= 'a' && c <= 'z') || 
               c == 'm') {
                in_escape = false;
            }
            read++;
            continue;
        }
        
        if(isspace((unsigned char)c)) {
            if(!last_space) {
                *write++ = ' ';
                last_space = true;
            }
        } else {
            *write++ = c;
            last_space = false;
        }
        read++;
    }
    
    if(write > str && *(write-1) == ' ') write--;
    *write = '\0';
}
static void strip_ansi_codes(const char* input, char* output) {
    if (!input || !output) return;
    
    size_t j = 0;
    bool in_escape = false;
    bool in_timestamp = false;
    char last_char = 0;
    
    for (size_t i = 0; input[i]; i++) {
        unsigned char c = input[i];
        char next_char = input[i + 1];
        
        // Handle ANSI escape sequences
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
        
        // Handle timestamps
        if (c == '(' && next_char && isdigit((unsigned char)next_char)) {
            in_timestamp = true;
            continue;
        }
        if (in_timestamp) {
            if (c == ')') {
                in_timestamp = false;
                if (next_char && !isspace((unsigned char)next_char)) {
                    output[j++] = ' ';
                }
            }
            continue;
        }
        
        // Skip color code fragments
        if (c == ';' && next_char && isdigit((unsigned char)next_char) && 
            input[i + 2] && input[i + 2] == 'm') {
            i += 2;
            continue;
        }
        
        if (j < RX_BUF_SIZE - 1) {
            output[j++] = c;
        }
        last_char = c;
    }
    
    output[j] = '\0';
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
    
    // Do all processing on cleaned line
    strip_ansi_codes(input, temp);
    clean_text(temp);

    bool keep_line = !should_filter_line(temp);
    
    if (keep_line) {
        // Add prefix if needed
        const char* prefix = "";
        if (config->add_prefixes) {
            if (strstr(temp, "WiFi") || strstr(temp, "AP_MANAGER") || 
                strstr(temp, "SSID:") || strstr(temp, "BSSID:")) {
                prefix = "[WIFI] ";
            } else if (strstr(temp, "BLE")) {
                prefix = "[BLE] ";
            } else if (strstr(temp, "Found Flipper")) {
                prefix = "[FLIPPER] ";
            }
        }

        if (strlen(prefix) > 0) {
            snprintf(output, RX_BUF_SIZE - 1, "%s%s", prefix, temp);
        } else {
            strncpy(output, temp, RX_BUF_SIZE - 1);
        }
        output[RX_BUF_SIZE - 1] = '\0';
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

static UartLineState* get_line_state(UartContext* uart) {
    if(!uart->line_state) {
        uart->line_state = malloc(sizeof(UartLineState));
        if(uart->line_state) {
            memset(uart->line_state, 0, sizeof(UartLineState));
            uart->line_state->initialized = true;
        }
    }
    return uart->line_state;
}

void handle_uart_rx_data(uint8_t *buf, size_t len, void *context) {
    AppState *state = (AppState *)context;
    
    if(!state || !state->uart_context || !state->uart_context->is_serial_active) return;
    if(!buf || len == 0) return;

    // Initialize buffer mutex if needed
    if(!init_buffer_mutex()) return;

    // Try to acquire mutex with timeout
    if(furi_mutex_acquire(buffer_mutex, MUTEX_TIMEOUT_MS) != FuriStatusOk) {
        FURI_LOG_W("UART", "Buffer mutex timeout");
        return;
    }

    // Get instance-specific line state
    UartLineState* uart_line_state = get_line_state(state->uart_context);
    if(!uart_line_state) {
        furi_mutex_release(buffer_mutex);
        return;
    }

    // Handle logging to file first, before any buffer modifications
    if(state->uart_context->storageContext && 
       state->uart_context->storageContext->log_file) {
        // Ensure proper null termination for logging
        uint8_t* log_buf = malloc(len + 1);
        if(log_buf) {
            memcpy(log_buf, buf, len);
            log_buf[len] = '\0';
            
            uint16_t written = storage_file_write(
                state->uart_context->storageContext->log_file, 
                log_buf, 
                len
            );
            
            if(written != len) {
                FURI_LOG_E("UART", "Log write failed: %d/%d bytes", written, len);
                // Force a file sync to ensure data is written
                storage_file_sync(state->uart_context->storageContext->log_file);
            }
            
            free(log_buf);
        }
    }

    // Ensure proper null termination of input for display
    if(len >= RX_BUF_SIZE) len = RX_BUF_SIZE - 1;
    buf[len] = '\0';

    if(!state->filter_config || !state->filter_config->enabled) {
        // Unfiltered handling
        size_t new_total_len = state->buffer_length + len + 1;

        if(new_total_len > MAX_BUFFER_SIZE) {
            size_t keep_size = MAX_BUFFER_SIZE / 2;
            if(state->textBoxBuffer && state->buffer_length > keep_size) {
                memmove(state->textBoxBuffer, 
                       state->textBoxBuffer + state->buffer_length - keep_size,
                       keep_size);
                state->buffer_length = keep_size;
                new_total_len = keep_size + len + 1;
            } else {
                if(state->textBoxBuffer) free(state->textBoxBuffer);
                state->textBoxBuffer = malloc(BUFFER_CLEAR_SIZE);
                if(state->textBoxBuffer) state->textBoxBuffer[0] = '\0';
                state->buffer_length = 0;
                new_total_len = len + 1;
            }
        }

        char* new_buffer = realloc(state->textBoxBuffer, new_total_len);
        if(!new_buffer) {
            FURI_LOG_E("UART", "Buffer realloc failed");
            furi_mutex_release(buffer_mutex);
            return;
        }

        memcpy(new_buffer + state->buffer_length, buf, len);
        new_buffer[new_total_len - 1] = '\0';
        state->textBoxBuffer = new_buffer;
        state->buffer_length = new_total_len - 1;

    } else {
        // Filtered handling with instance-specific state
        char output[RX_BUF_SIZE];
        
        if(format_line_with_state(&uart_line_state->line_state, (char*)buf, output, state->filter_config)) {
            size_t output_len = strlen(output);
            if(output_len > 0) {  // Only process non-empty output
                size_t new_total_len = state->buffer_length + output_len + 2;  // +2 for newline and null

                if(new_total_len > MAX_BUFFER_SIZE) {
                    // Find last complete line before truncating
                    char* last_nl = strrchr(state->textBoxBuffer, '\n');
                    if(last_nl) {
                        size_t keep_size = last_nl - state->textBoxBuffer + 1;
                        memmove(state->textBoxBuffer, state->textBoxBuffer, keep_size);
                        state->buffer_length = keep_size;
                        new_total_len = keep_size + output_len + 2;
                    } else {
                        state->buffer_length = 0;
                        new_total_len = output_len + 2;
                    }
                }

                char* new_buffer = realloc(state->textBoxBuffer, new_total_len);
                if(new_buffer) {
                    if(state->buffer_length == 0) {
                        new_buffer[0] = '\0';
                    }
                    state->textBoxBuffer = new_buffer;
                    strcat(new_buffer, output);
                    strcat(new_buffer, "\n");
                    state->buffer_length = strlen(new_buffer);
                }
            }
        }
    }

    // Update text box based on view preference
    if(state->textBoxBuffer && state->buffer_length > 0) {
        bool view_from_start = state->settings.view_logs_from_start_index;
        
        if(view_from_start) {
            text_box_set_text(state->text_box, state->textBoxBuffer);
            text_box_set_focus(state->text_box, TextBoxFocusStart);
        } else {
            size_t display_size = state->buffer_length;
            if(display_size > (size_t)TEXT_BOX_STORE_SIZE - 1) {
                display_size = (size_t)TEXT_BOX_STORE_SIZE - 1;
            }
            const char* display_start = state->textBoxBuffer + 
                (state->buffer_length > display_size ? state->buffer_length - display_size : 0);
            text_box_set_text(state->text_box, display_start);
            text_box_set_focus(state->text_box, TextBoxFocusEnd);
        }
    }

    furi_mutex_release(buffer_mutex);
}
// UART worker thread function
static int32_t uart_worker(void* context) {
    UartContext* uart = (void*)context;

    while(1) {
        uint32_t events = furi_thread_flags_wait(
            WORKER_ALL_RX_EVENTS, 
            FuriFlagWaitAny, 
            FuriWaitForever);
            
        furi_check((events & FuriFlagError) == 0);
        
        if(events & WorkerEvtStop) break;
        
        if(events & WorkerEvtRxDone) {
            size_t len = furi_stream_buffer_receive(
                uart->rx_stream, 
                uart->rx_buf, 
                RX_BUF_SIZE, 
                0);
                
            if(len > 0) {
                if(uart->handle_rx_data_cb)
                    uart->handle_rx_data_cb(uart->rx_buf, len, uart->state);
            }
        }
        
        if(events & WorkerEvtPcapDone) {
            size_t len = furi_stream_buffer_receive(
                uart->pcap_stream, 
                uart->rx_buf, 
                RX_BUF_SIZE, 
                0);
                
            if(len > 0) {
                if(uart->handle_rx_pcap_cb)
                    uart->handle_rx_pcap_cb(uart->rx_buf, len, uart);
            }
        }
    }

    furi_stream_buffer_free(uart->rx_stream);
    furi_stream_buffer_free(uart->pcap_stream);

    return 0;
}

void update_text_box_view(AppState* state) {
    if(!state || !state->text_box || !state->textBoxBuffer) return;
    
    bool view_from_start = state->settings.view_logs_from_start_index;
    size_t content_length = strlen(state->textBoxBuffer);
    
    if(view_from_start) {
        // Show from beginning
        text_box_set_text(state->text_box, state->textBoxBuffer);
        text_box_set_focus(state->text_box, TextBoxFocusStart);
    } else {
        // Show from end with proper size handling
        size_t display_size = content_length;
        if(display_size > (size_t)TEXT_BOX_STORE_SIZE - 1) {
            display_size = (size_t)TEXT_BOX_STORE_SIZE - 1;
        }
        const char* display_start = state->textBoxBuffer + 
            (content_length > display_size ? content_length - display_size : 0);
        text_box_set_text(state->text_box, display_start);
        text_box_set_focus(state->text_box, TextBoxFocusEnd);
    }
}
UartContext* uart_init(AppState* state) {
    FURI_LOG_I("UART", "Starting UART initialization");
    
    UartContext* uart = malloc(sizeof(UartContext));
    if(!uart) {
        FURI_LOG_E("UART", "Failed to allocate UART context");
        return NULL;
    }
    memset(uart, 0, sizeof(UartContext));

    // Initialize mutex
    if(!init_buffer_mutex()) {
        free(uart);
        return NULL;
    }

    uart->state = state;
    uart->is_serial_active = false;
    uart->streams_active = false;
    uart->storage_active = false;
    uart->pcap = false;
    uart->mark_test_idx = 0;

    // Stream initialization
    uart->rx_stream = furi_stream_buffer_alloc(RX_BUF_SIZE, 1);
    uart->pcap_stream = furi_stream_buffer_alloc(RX_BUF_SIZE, 1);
    uart->storage_stream = furi_stream_buffer_alloc(RX_BUF_SIZE, 1);
    uart->streams_active = (uart->rx_stream && uart->pcap_stream && uart->storage_stream);

    // Set callbacks
    uart->handle_rx_data_cb = handle_uart_rx_data;
    uart->handle_rx_pcap_cb = uart_storage_rx_callback;

    // Initialize thread
    uart->rx_thread = furi_thread_alloc();
    if(uart->rx_thread) {
        furi_thread_set_name(uart->rx_thread, "UART_Receive");
        furi_thread_set_stack_size(uart->rx_thread, 4096);
        furi_thread_set_context(uart->rx_thread, uart);
        furi_thread_set_callback(uart->rx_thread, uart_worker);
        furi_thread_start(uart->rx_thread);
    }

    // Initialize storage
    uart->storageContext = uart_storage_init(uart);
    uart->storage_active = (uart->storageContext != NULL);

    // Initialize serial
    uart->serial_handle = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(uart->serial_handle) {
        furi_hal_serial_init(uart->serial_handle, 115200);
        uart->is_serial_active = true;
        furi_hal_serial_async_rx_start(uart->serial_handle, uart_rx_callback, uart, false);
    }

    return uart;
}
void uart_free(UartContext *uart) {
    if(!uart) return;

    // Clean up line state
    if(uart->line_state) {
        free(uart->line_state);
        uart->line_state = NULL;
    }


    // Stop receiving new data first
    if(uart->serial_handle) {
        furi_hal_serial_async_rx_stop(uart->serial_handle);
        furi_delay_ms(10);
    }

    // Clean up mutex if it exists
    if(buffer_mutex) {
        furi_mutex_free(buffer_mutex);
        buffer_mutex = NULL;
    }

    // Stop thread and wait for it to finish
    if(uart->rx_thread) {
        furi_thread_flags_set(furi_thread_get_id(uart->rx_thread), WorkerEvtStop);
        furi_thread_join(uart->rx_thread);
        furi_thread_free(uart->rx_thread);
    }

    // Clean up serial after thread is stopped
    if(uart->serial_handle) {
        furi_hal_serial_deinit(uart->serial_handle);
        furi_hal_serial_control_release(uart->serial_handle);
    }

    // Free streams after everything is stopped
    if(uart->rx_stream) furi_stream_buffer_free(uart->rx_stream);
    if(uart->pcap_stream) furi_stream_buffer_free(uart->pcap_stream);
    if(uart->storage_stream) furi_stream_buffer_free(uart->storage_stream);

    // Clean up storage context last
    if(uart->storageContext) uart_storage_free(uart->storageContext);

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


bool uart_is_esp_connected(UartContext* uart) {
    FURI_LOG_D("UART", "Checking ESP connection...");
    
    if(!uart || !uart->serial_handle || !uart->state) {
        FURI_LOG_E("UART", "Invalid UART context");
        return false;
    }

    // Save state
    FilterConfig* saved_config = uart->state->filter_config;
    uart->state->filter_config = NULL;
    
    // Initialize clean buffer with mutex protection
    if(furi_mutex_acquire(buffer_mutex, MUTEX_TIMEOUT_MS) != FuriStatusOk) {
        FURI_LOG_E("UART", "Failed to acquire mutex");
        uart->state->filter_config = saved_config;
        return false;
    }
    
    if(uart->state->textBoxBuffer) {
        free(uart->state->textBoxBuffer);
    }
    uart->state->textBoxBuffer = malloc(RX_BUF_SIZE);  // Larger buffer
    if(!uart->state->textBoxBuffer) {
        FURI_LOG_E("UART", "Buffer allocation failed");
        furi_mutex_release(buffer_mutex);
        uart->state->filter_config = saved_config;
        return false;
    }
    uart->state->textBoxBuffer[0] = '\0';
    uart->state->buffer_length = 0;
    furi_mutex_release(buffer_mutex);

    // Try multiple test commands
    const char* test_cmds[] = {
        "stop\n",      // Stop any running operation
    };
    
    bool connected = false;
    const uint32_t RETRY_COUNT = 3;
    const uint32_t CMD_TIMEOUT_MS = 250; // Longer timeout per command
    
    for(uint32_t retry = 0; retry < RETRY_COUNT && !connected; retry++) {
        for(size_t i = 0; i < COUNT_OF(test_cmds) && !connected; i++) {
            // Clear buffer before each command
            if(furi_mutex_acquire(buffer_mutex, MUTEX_TIMEOUT_MS) == FuriStatusOk) {
                uart->state->textBoxBuffer[0] = '\0';
                uart->state->buffer_length = 0;
                furi_mutex_release(buffer_mutex);
            }

            // Send test command
            uart_send(uart, (uint8_t*)test_cmds[i], strlen(test_cmds[i]));
            FURI_LOG_D("UART", "Sent command: %s", test_cmds[i]);

            uint32_t start_time = furi_get_tick();
            while(furi_get_tick() - start_time < CMD_TIMEOUT_MS) {
                if(furi_mutex_acquire(buffer_mutex, MUTEX_TIMEOUT_MS) == FuriStatusOk) {
                    if(uart->state->textBoxBuffer && uart->state->buffer_length > 0) {
                        FURI_LOG_D("UART", "Got response: %.20s", uart->state->textBoxBuffer);
                        connected = true;
                        furi_mutex_release(buffer_mutex);
                        break;
                    }
                    furi_mutex_release(buffer_mutex);
                }
                furi_delay_ms(10);
            }
        }

        if(!connected && retry < RETRY_COUNT - 1) {
            FURI_LOG_D("UART", "Retry %lu of %lu", retry + 1, RETRY_COUNT);
            furi_delay_ms(100); // Delay between retries
        }
    }

    // Restore state
    uart->state->filter_config = saved_config;

    if(!connected) {
        FURI_LOG_W("UART", "ESP connection check failed after all retries");
    } else {
        FURI_LOG_I("UART", "ESP connected successfully");
    }

    return connected;
}
void uart_receive_data(
    UartContext* uart,
    ViewDispatcher* view_dispatcher,
    AppState* state,
    const char* prefix,
    const char* extension,
    const char* TargetFolder) {
    
    if(!uart || !uart->storageContext || !view_dispatcher || !state || !prefix || !extension || !TargetFolder) {
        FURI_LOG_E("UART", "Invalid parameters in uart_receive_data");
        return;
    }

    // Safely disable PCAP mode if active
    if(uart->pcap) {
        uart->pcap = false;
        furi_delay_ms(10); // Allow buffers to flush
        if(uart->pcap_stream) {
            furi_stream_buffer_reset(uart->pcap_stream);
        }
    }
    
    text_box_set_text(state->text_box, "");
    text_box_set_focus(state->text_box, TextBoxFocusEnd);

    if(strlen(prefix) > 1) {
        // Close any existing file first
        if(uart->storageContext->HasOpenedFile && uart->storageContext->current_file) {
            storage_file_close(uart->storageContext->current_file);
            uart->storageContext->HasOpenedFile = false;
        }

        // Open new file with proper error handling
        uart->storageContext->HasOpenedFile = sequential_file_open(
            uart->storageContext->storage_api,
            uart->storageContext->current_file,
            TargetFolder,
            prefix,
            extension);
            
        if(!uart->storageContext->HasOpenedFile) {
            FURI_LOG_E("UART", "Failed to open file for writing");
            return;
        }
    }

    view_dispatcher_switch_to_view(view_dispatcher, 5);
    state->current_view = 5;
}