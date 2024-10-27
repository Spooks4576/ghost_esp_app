#include "confirmation_view.h"
#include <gui/elements.h>
#include <furi.h>

struct ConfirmationView {
    View* view;
    ConfirmationViewCallback ok_callback;
    void* ok_callback_context;
    ConfirmationViewCallback cancel_callback;
    void* cancel_callback_context;
};

typedef struct {
    const char* header;
    const char* text;
} ConfirmationViewModel;

static void confirmation_view_draw_callback(Canvas* canvas, void* _model) {
    if(!canvas || !_model) return;

    ConfirmationViewModel* model = (ConfirmationViewModel*)_model;

    // Border
    canvas_draw_rframe(canvas, 0, 0, 128, 64, 2);

    // Header
    if(model->header) {
        canvas_set_font(canvas, FontPrimary);
        elements_multiline_text_aligned(canvas, 64, 5, AlignCenter, AlignTop, model->header);
    }

    // Text
    if(model->text) {
        canvas_set_font(canvas, FontSecondary);
        elements_multiline_text_aligned(canvas, 63, 20, AlignCenter, AlignTop, model->text);
    }

    // Draw OK button at bottom
    canvas_set_font(canvas, FontSecondary);
    elements_button_center(canvas, "OK");
}

static bool confirmation_view_input_callback(InputEvent* event, void* context) {
    FURI_LOG_D("ConfView", "Input received - Type: %d, Key: %d", event->type, event->key);
    
    if(!event || !context) {
        FURI_LOG_E("ConfView", "Null event or context in input callback");
        return false;
    }

    ConfirmationView* instance = (ConfirmationView*)context;
    if(!instance) {
        FURI_LOG_E("ConfView", "Null confirmation view instance");
        return false;
    }

    bool consumed = false;

    // Handle both short and long presses
    if(event->type == InputTypeShort || event->type == InputTypeLong) {
        if(event->key == InputKeyOk) {
            FURI_LOG_D("ConfView", "OK pressed - Callback: %p, Context: %p", 
                instance->ok_callback, instance->ok_callback_context);
                
            if(instance->ok_callback) {
                instance->ok_callback(instance->ok_callback_context);
            }
            consumed = true;
        } else if(event->key == InputKeyBack) {
            FURI_LOG_D("ConfView", "Back pressed - Callback: %p, Context: %p",
                instance->cancel_callback, instance->cancel_callback_context);
                
            if(instance->cancel_callback) {
                instance->cancel_callback(instance->cancel_callback_context);
            }
            consumed = true;
        }
    }

    return consumed;
}

ConfirmationView* confirmation_view_alloc(void) {
    ConfirmationView* instance = malloc(sizeof(ConfirmationView));
    if(!instance) return NULL;  // Check malloc success

    instance->view = view_alloc();
    if(!instance->view) {  // Check view allocation
        free(instance);
        return NULL;
    }

    // Initialize all pointers to NULL
    instance->ok_callback = NULL;
    instance->ok_callback_context = NULL;
    instance->cancel_callback = NULL;
    instance->cancel_callback_context = NULL;

    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, confirmation_view_draw_callback);
    view_set_input_callback(instance->view, confirmation_view_input_callback);

    // Allocate model properly
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(ConfirmationViewModel));

    with_view_model(
        instance->view,
        ConfirmationViewModel* model,
        {
            if(model) {
                model->header = NULL;
                model->text = NULL;
            }
        },
        true);

    return instance;
}

void confirmation_view_free(ConfirmationView* instance) {
    if(!instance) return;
    if(instance->view) {
        view_free(instance->view);
    }
    free(instance);
}

View* confirmation_view_get_view(ConfirmationView* instance) {
    if(!instance) return NULL;
    return instance->view;
}

void confirmation_view_set_header(ConfirmationView* instance, const char* text) {
    if(!instance || !instance->view) return;

    with_view_model(
        instance->view,
        ConfirmationViewModel* model,
        {
            if(model) {
                model->header = text;
            }
        },
        true);
}

void confirmation_view_set_text(ConfirmationView* instance, const char* text) {
    if(!instance || !instance->view) return;

    with_view_model(
        instance->view,
        ConfirmationViewModel* model,
        {
            if(model) {
                model->text = text;
            }
        },
        true);
}

void confirmation_view_set_ok_callback(
    ConfirmationView* instance,
    ConfirmationViewCallback callback,
    void* context) {
    if(!instance) return;
    instance->ok_callback = callback;
    instance->ok_callback_context = context;
}

void confirmation_view_set_cancel_callback(
    ConfirmationView* instance,
    ConfirmationViewCallback callback,
    void* context) {
    if(!instance) return;
    instance->cancel_callback = callback;
    instance->cancel_callback_context = context;
}
