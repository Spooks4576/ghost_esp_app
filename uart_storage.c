#include "uart_utils.h"
#include "log_manager.h"
#include <furi.h>
#include <stdlib.h>
#include <string.h>
#include <storage/storage.h>
#include "sequential_file.h"


#define COMMAND_BUFFER_SIZE 128

void uart_storage_rx_callback(uint8_t *buf, size_t len, void *context) {
    UartContext *app = context;
    if(app->storageContext->HasOpenedFile) {
        storage_file_write(app->storageContext->current_file, buf, len);
    }
}

UartStorageContext *uart_storage_init(UartContext *parentContext) {
    uint32_t start_time = furi_get_tick();
    FURI_LOG_D("Storage", "Starting storage initialization");

    UartStorageContext *ctx = malloc(sizeof(UartStorageContext));
    if(!ctx) {
        FURI_LOG_E("Storage", "Failed to allocate context");
        return NULL;
    }

    memset(ctx, 0, sizeof(UartStorageContext));
    ctx->parentContext = parentContext;

    // Open storage API
    uint32_t storage_open_start = furi_get_tick();
    ctx->storage_api = furi_record_open(RECORD_STORAGE);
    FURI_LOG_D("Storage", "Storage API open took %lums", furi_get_tick() - storage_open_start);

    if(!ctx->storage_api) {
        FURI_LOG_E("Storage", "Failed to open storage API");
        free(ctx);
        return NULL;
    }

    // Allocate file handles
    ctx->current_file = storage_file_alloc(ctx->storage_api);
    ctx->log_file = storage_file_alloc(ctx->storage_api);

    if(!ctx->current_file || !ctx->log_file) {
        FURI_LOG_E("Storage", "Failed to allocate file handles");
        if(ctx->current_file) storage_file_free(ctx->current_file);
        if(ctx->log_file) storage_file_free(ctx->log_file);
        furi_record_close(RECORD_STORAGE);
        free(ctx);
        return NULL;
    }

    // Create directories efficiently
    uint32_t dir_start = furi_get_tick();
    storage_simply_mkdir(ctx->storage_api, GHOST_ESP_APP_FOLDER);
    storage_simply_mkdir(ctx->storage_api, GHOST_ESP_APP_FOLDER_LOGS);
    storage_simply_mkdir(ctx->storage_api, GHOST_ESP_APP_FOLDER_PCAPS);
    storage_simply_mkdir(ctx->storage_api, GHOST_ESP_APP_FOLDER_WARDRIVE);
    FURI_LOG_D("Storage", "Directory creation took %lums", furi_get_tick() - dir_start);

    // Initialize log file efficiently
    uint32_t log_start = furi_get_tick();
    bool log_ok = sequential_file_open(
        ctx->storage_api,
        ctx->log_file,
        GHOST_ESP_APP_FOLDER_LOGS,
        "ghost_logs",
        "txt");
    
    if(!log_ok) {
        FURI_LOG_W("Storage", "Failed to create initial log file");
    }
    FURI_LOG_D("Storage", "Log file creation took %lums", furi_get_tick() - log_start);

    ctx->HasOpenedFile = log_ok;

    FURI_LOG_I("Storage", "Storage initialization completed in %lums", 
               furi_get_tick() - start_time);
    return ctx;
}
void uart_storage_reset_logs(UartStorageContext *ctx) {
    if(!ctx || !ctx->storage_api) return;

    FURI_LOG_D("Storage", "Resetting log files");

    // Close existing log file if open
    if(ctx->log_file) {
        storage_file_close(ctx->log_file);
        storage_file_free(ctx->log_file);
        ctx->log_file = storage_file_alloc(ctx->storage_api);
    }

    // Create new log file
    ctx->HasOpenedFile = sequential_file_open(
        ctx->storage_api,
        ctx->log_file,
        GHOST_ESP_APP_FOLDER_LOGS,
        "ghost_logs",
        "txt");

    if(!ctx->HasOpenedFile) {
        FURI_LOG_E("Storage", "Failed to create new log file during reset");
    }
}
void uart_storage_free(UartStorageContext *ctx) {
    if(!ctx) return;

    uint32_t start_time = furi_get_tick();
    FURI_LOG_D("Storage", "Starting storage cleanup");

    if(ctx->current_file) storage_file_free(ctx->current_file);
    if(ctx->log_file) storage_file_free(ctx->log_file);
    if(ctx->storage_api) furi_record_close(RECORD_STORAGE);

    free(ctx);
    FURI_LOG_D("Storage", "Storage cleanup completed in %lums", 
               furi_get_tick() - start_time);
}