#include "uart_utils.h"
#include <furi.h>
#include <stdlib.h>
#include <string.h>
#include <storage/storage.h>
#include "sequential_file.h"

#define COMMAND_BUFFER_SIZE 128

void uart_storage_rx_callback(uint8_t *buf, size_t len, void *context) {
    furi_assert(context);
    UartContext *app = context;
    
    if(app->storageContext->HasOpenedFile) {
        FURI_LOG_D("UartStorage", "Writing %d bytes to file", len);
        storage_file_write(app->storageContext->current_file, buf, len);
    }
}

// Initialize UART storage context
UartStorageContext *uart_storage_init(UartContext *parentContext) {
    FURI_LOG_I("UartStorage", "Initializing UART storage");
    
    UartStorageContext *ctx = malloc(sizeof(UartStorageContext));
    if(!ctx) {
        FURI_LOG_E("UartStorage", "Failed to allocate context");
        return NULL;
    }

    // Initialize context
    ctx->storage_api = furi_record_open(RECORD_STORAGE);
    ctx->current_file = storage_file_alloc(ctx->storage_api);
    ctx->log_file = storage_file_alloc(ctx->storage_api);
    ctx->parentContext = parentContext;
    ctx->HasOpenedFile = false;
    ctx->IsWritingToFile = false;

    // Create necessary directories
    const char* directories[] = {
        GHOST_ESP_APP_FOLDER,
        GHOST_ESP_APP_FOLDER_LOGS,
        GHOST_ESP_APP_FOLDER_WARDRIVE,
        GHOST_ESP_APP_FOLDER_PCAPS
    };

    for(size_t i = 0; i < sizeof(directories)/sizeof(directories[0]); i++) {
        if(!storage_dir_exists(ctx->storage_api, directories[i])) {
            FURI_LOG_I("UartStorage", "Creating directory: %s", directories[i]);
            if(!storage_simply_mkdir(ctx->storage_api, directories[i])) {
                FURI_LOG_W("UartStorage", "Failed to create directory: %s", directories[i]);
            }
        }
    }

    // Open log file
    FURI_LOG_I("UartStorage", "Opening log file");
    if(!sequential_file_open(
        ctx->storage_api,
        ctx->log_file,
        GHOST_ESP_APP_FOLDER_LOGS,
        "ghost_logs",
        "txt")) {
        FURI_LOG_E("UartStorage", "Failed to open log file");
    }

    FURI_LOG_I("UartStorage", "UART storage initialized successfully");
    return ctx;
}

// Free UART storage context
void uart_storage_free(UartStorageContext *ctx) {
    if(!ctx) {
        FURI_LOG_E("UartStorage", "Null context in uart_storage_free");
        return;
    }

    FURI_LOG_I("UartStorage", "Freeing UART storage");

    if(ctx->current_file) {
        storage_file_free(ctx->current_file);
    }
    if(ctx->log_file) {
        storage_file_free(ctx->log_file);
    }

    if(ctx->storage_api) {
        furi_record_close(RECORD_STORAGE);
    }

    free(ctx);
    FURI_LOG_I("UartStorage", "UART storage freed");
}