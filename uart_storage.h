#ifndef UART_STORAGE_H
#define UART_STORAGE_H

#include <furi.h>
#include <storage/storage.h>

typedef struct {
    Storage* storage_api;
    File* current_file;
    File* log_file;
    UartContext* parentContext;
    bool HasOpenedFile;
    bool IsWritingToFile;
} UartStorageContext;

// Function prototypes

/**
 * @brief Initializes the UART storage context and starts listening for commands.
 *
 * @param serial_id The serial ID to be used for UART communication.
 * @param baud_rate The baud rate for UART communication.
 * @return UartStorageContext* Pointer to the initialized context.
 */
UartStorageContext* uart_storage_init(UartContext* parentContext);

/**
 * @brief Frees the UART storage context and stops UART communication.
 *
 * @param ctx Pointer to the UART storage context.
 */
void uart_storage_free(UartStorageContext* ctx);

void uart_storage_rx_callback(uint8_t* buf, size_t len, void* context);

#endif // UART_STORAGE_H