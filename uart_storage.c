#include "uart_utils.h"
#include "log_manager.h"
#include <furi.h>
#include <stdlib.h>
#include <string.h>
#include <storage/storage.h>
#include "sequential_file.h"

#define COMMAND_BUFFER_SIZE 128
#define PCAP_WRITE_CHUNK_SIZE 1024u

void uart_storage_rx_callback(uint8_t *buf, size_t len, void *context) {
    UartContext *app = context;
    
    if(!app || !app->storageContext || !app->storageContext->storage_api) {
        FURI_LOG_E("Storage", "Invalid storage context");
        return;
    }

    if(!app->storageContext->HasOpenedFile || 
       !app->storageContext->current_file ||
       !storage_file_is_open(app->storageContext->current_file)) {
        FURI_LOG_W("Storage", "No file open for writing");
        app->pcap = false;  // Disable PCAP mode if file isn't ready
        return;
    }

    // Write in chunks to avoid buffer overflow
    size_t written = 0;
    while(written < len) {
        size_t remaining = len - written;
        size_t chunk_size = (remaining < PCAP_WRITE_CHUNK_SIZE) ? 
                           remaining : PCAP_WRITE_CHUNK_SIZE;
            
        uint16_t bytes_written = storage_file_write(
            app->storageContext->current_file, 
            buf + written, 
            chunk_size);
            
        if(bytes_written != chunk_size) {
            FURI_LOG_E("Storage", "Write failed: %u/%zu bytes", bytes_written, chunk_size);
            app->pcap = false;  // Disable PCAP mode on write failure
            break;
        }
        written += bytes_written;
    }
}
UartStorageContext* uart_storage_init(UartContext* parentContext) {
    uint32_t start_time = furi_get_tick();
    FURI_LOG_D("Storage", "Starting storage initialization");

    UartStorageContext* ctx = malloc(sizeof(UartStorageContext));
    if(!ctx) {
        FURI_LOG_E("Storage", "Failed to allocate context");
        return NULL;
    }
    memset(ctx, 0, sizeof(UartStorageContext));
    ctx->parentContext = parentContext;

    ctx->storage_api = furi_record_open(RECORD_STORAGE);
    if(!ctx->storage_api) {
        FURI_LOG_E("Storage", "Failed to open storage API");
        free(ctx);
        return NULL;
    }

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

    // Create directories with verification
    if(!storage_simply_mkdir(ctx->storage_api, GHOST_ESP_APP_FOLDER)) {
        FURI_LOG_W("Storage", "Failed to create app folder");
    }
    if(!storage_simply_mkdir(ctx->storage_api, GHOST_ESP_APP_FOLDER_PCAPS)) {
        FURI_LOG_W("Storage", "Failed to create PCAP folder");
    }
    if(!storage_simply_mkdir(ctx->storage_api, GHOST_ESP_APP_FOLDER_LOGS)) {
        FURI_LOG_W("Storage", "Failed to create logs folder");
    }
    if(!storage_simply_mkdir(ctx->storage_api, GHOST_ESP_APP_FOLDER_WARDRIVE)) {
        FURI_LOG_W("Storage", "Failed to create wardrive folder");
    }

    // Verify PCAP directory exists and is writable
    File* test_dir = storage_file_alloc(ctx->storage_api);
    if(test_dir) {
        if(!storage_dir_open(test_dir, GHOST_ESP_APP_FOLDER_PCAPS)) {
            FURI_LOG_E("Storage", "PCAP directory not accessible");
        }
        storage_dir_close(test_dir);
        storage_file_free(test_dir);
    }

    // Initialize log file
    bool log_ok = sequential_file_open(
        ctx->storage_api,
        ctx->log_file,
        GHOST_ESP_APP_FOLDER_LOGS,
        "ghost_logs",
        "txt");

    if(!log_ok) {
        FURI_LOG_E("Storage", "Failed to open log file");
    }

    ctx->HasOpenedFile = log_ok;
    ctx->IsWritingToFile = false;

    FURI_LOG_I("Storage", "Storage initialization completed in %lums", 
               furi_get_tick() - start_time);
    return ctx;
}

void uart_storage_reset_logs(UartStorageContext *ctx) {
    if(!ctx || !ctx->storage_api) return;

    if(ctx->log_file) {
        storage_file_close(ctx->log_file);
        storage_file_free(ctx->log_file);
        ctx->log_file = storage_file_alloc(ctx->storage_api);
    }

    ctx->HasOpenedFile = sequential_file_open(
        ctx->storage_api,
        ctx->log_file,
        GHOST_ESP_APP_FOLDER_LOGS,
        "ghost_logs",
        "txt");
        
    if(!ctx->HasOpenedFile) {
        FURI_LOG_E("Storage", "Failed to reset log file");
    }
}

void uart_storage_free(UartStorageContext *ctx) {
    if(!ctx) return;

    if(ctx->current_file) {
        storage_file_close(ctx->current_file);
        storage_file_free(ctx->current_file);
    }

    if(ctx->log_file) {
        storage_file_close(ctx->log_file);
        storage_file_free(ctx->log_file);
    }

    if(ctx->storage_api) {
        furi_record_close(RECORD_STORAGE);
    }

    free(ctx);
}