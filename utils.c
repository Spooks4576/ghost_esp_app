#include "utils.h"
#include "app_state.h"

// Structure to hold confirmation dialog data
typedef struct {
    const char* header;
    const char* text;
    ConfirmationViewCallback ok_callback;
    ConfirmationViewCallback cancel_callback;
} ConfirmationDialogData;

static ConfirmationDialogData current_dialog;

void show_confirmation_dialog_ex(
    void* context,
    const char* header,
    const char* text,
    ConfirmationViewCallback ok_callback,
    ConfirmationViewCallback cancel_callback) {
    
    AppState* state = (AppState*)context;
    
    // Store the current dialog data
    current_dialog.header = header;
    current_dialog.text = text;
    current_dialog.ok_callback = ok_callback;
    current_dialog.cancel_callback = cancel_callback;
    
    // Set up the confirmation dialog
    confirmation_view_set_header(state->confirmation_view, header);
    confirmation_view_set_text(state->confirmation_view, text);
    confirmation_view_set_ok_callback(state->confirmation_view, ok_callback, state);
    confirmation_view_set_cancel_callback(state->confirmation_view, cancel_callback, state);
    
    // Save current view before switching
    state->previous_view = state->current_view;
    
    // Switch to confirmation view
    view_dispatcher_switch_to_view(state->view_dispatcher, 7);
    state->current_view = 7;
}

void show_confirmation_view_wrapper(void* context, ConfirmationView* view) {
    (void)view;  // Mark parameter as unused to avoid compiler warning
    // Redirect to show_confirmation_dialog_ex with the stored dialog data
    show_confirmation_dialog_ex(context, current_dialog.header, current_dialog.text, current_dialog.ok_callback, current_dialog.cancel_callback);
}