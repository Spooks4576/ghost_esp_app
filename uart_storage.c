#include "uart_utils.h"
#include <furi.h>
#include <stdlib.h>
#include <string.h>
#include <storage/storage.h>
#include "sequential_file.h"

#define COMMAND_BUFFER_SIZE 128

void uart_storage_rx_callback(uint8_t* buf, size_t len, void* context) {
    furi_assert(context);
    UartContext* app = context;

    if(app->storageContext->HasOpenedFile) {
        storage_file_write(app->storageContext->current_file, buf, len);
    }
}

// Initialize UART storage context
UartStorageContext* uart_storage_init(UartContext* parentContext) {
    UartStorageContext* ctx = malloc(sizeof(UartStorageContext));
    ctx->storage_api = furi_record_open(RECORD_STORAGE);
    ctx->current_file = storage_file_alloc(ctx->storage_api);
    ctx->log_file = storage_file_alloc(ctx->storage_api);
    ctx->parentContext = parentContext;

    if(!storage_dir_exists(ctx->storage_api, GHOST_ESP_APP_FOLDER)) {
        storage_simply_mkdir(ctx->storage_api, GHOST_ESP_APP_FOLDER);
    }

    if(!storage_dir_exists(ctx->storage_api, GHOST_ESP_APP_FOLDER_LOGS)) {
        storage_simply_mkdir(ctx->storage_api, GHOST_ESP_APP_FOLDER_LOGS);
    }

    if(!storage_dir_exists(ctx->storage_api, GHOST_ESP_APP_FOLDER_WARDRIVE)) {
        storage_simply_mkdir(ctx->storage_api, GHOST_ESP_APP_FOLDER_WARDRIVE);
    }

    if(!storage_dir_exists(ctx->storage_api, GHOST_ESP_APP_FOLDER_PCAPS)) {
        storage_simply_mkdir(ctx->storage_api, GHOST_ESP_APP_FOLDER_PCAPS);
    }

    sequential_file_open(
        ctx->storage_api, ctx->log_file, GHOST_ESP_APP_FOLDER_LOGS, "ghost_logs", "txt");

    return ctx;
}

// Free UART storage context
void uart_storage_free(UartStorageContext* ctx) {
    UartStorageContext* mainctx = (UartStorageContext*)ctx;

    if(mainctx->current_file) {
        storage_file_free(mainctx->current_file);
        storage_file_free(mainctx->log_file);
    }
    furi_record_close(RECORD_STORAGE);
    free(mainctx);
}