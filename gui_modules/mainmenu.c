#include "mainmenu.h"

#include <gui/elements.h>
#include <furi.h>
#include <m-array.h>

#include <ghost_esp_icons.h>

struct MainMenu {
    View* view;

    FuriTimer* locked_timer;
};

typedef struct {
    FuriString* label;
    uint32_t index;
    MainMenuItemCallback callback;
    void* callback_context;

    bool locked;
    FuriString* locked_message;
} MainMenuItem;

static void MainMenuItem_init(MainMenuItem* item) {
    item->label = furi_string_alloc();
    item->index = 0;
    item->callback = NULL;
    item->callback_context = NULL;
    item->locked = false;
    item->locked_message = furi_string_alloc();
}

static void MainMenuItem_init_set(MainMenuItem* item, const MainMenuItem* src) {
    item->label = furi_string_alloc_set(src->label);
    item->index = src->index;
    item->callback = src->callback;
    item->callback_context = src->callback_context;
    item->locked = src->locked;
    item->locked_message = furi_string_alloc_set(src->locked_message);
}

static void MainMenuItem_set(MainMenuItem* item, const MainMenuItem* src) {
    furi_string_set(item->label, src->label);
    item->index = src->index;
    item->callback = src->callback;
    item->callback_context = src->callback_context;
    item->locked = src->locked;
    furi_string_set(item->locked_message, src->locked_message);
}

static void MainMenuItem_clear(MainMenuItem* item) {
    furi_string_free(item->label);
    furi_string_free(item->locked_message);
}

ARRAY_DEF(
    MainMenuItemArray,
    MainMenuItem,
    (INIT(API_2(MainMenuItem_init)),
     SET(API_6(MainMenuItem_set)),
     INIT_SET(API_6(MainMenuItem_init_set)),
     CLEAR(API_2(MainMenuItem_clear))))

typedef struct {
    MainMenuItemArray_t items;
    FuriString* header;
    size_t position;
    size_t window_position;

    bool locked_message_visible;
    bool is_vertical;
} MainMenuModel;

static void main_menu_process_up(MainMenu* main_menu);
static void main_menu_process_down(MainMenu* main_menu);
static void main_menu_process_ok(MainMenu* main_menu);

static size_t main_menu_items_on_screen(MainMenuModel* model) {
    UNUSED(model);
    size_t res = 8;
    return res;
}

static void main_menu_view_draw_callback(Canvas* canvas, void* _model) {
    MainMenuModel* model = _model;

    // Constants for layout
    const uint8_t total_cards = MainMenuItemArray_size(model->items);
    const uint8_t card_width = 28; // Reduced width to fit 4 cards
    const uint8_t card_height = 44; // Increased height for higher cards
    const uint8_t card_margin = 1; // Small margin between cards
    const uint8_t visible_cards = total_cards;

    // Ensure all 4 cards fit the screen
    const uint8_t starting_x =
        (canvas_width(canvas) - (visible_cards * card_width + (visible_cards - 1) * card_margin)) /
        2;
    const uint8_t base_card_y_position = 5; // Position the cards higher on the screen
    const uint8_t selected_card_offset = 3; // Offset for the selected card

    canvas_clear(canvas);

    // Draw the header at the bottom of the cards
    if(!furi_string_empty(model->header)) {
        canvas_set_font(canvas, FontSecondary);
        int header_x_position =
            (canvas_width(canvas) -
             canvas_string_width(canvas, furi_string_get_cstr(model->header))) /
            2;
        int header_y_position = base_card_y_position + card_height + 10; // Default header position
        // Adjust header position if selected card might overlap
        canvas_draw_str(
            canvas,
            header_x_position,
            header_y_position, // Position header below the cards
            furi_string_get_cstr(model->header));
    }

    // Iterate over menu items and draw each card
    size_t position = 0;
    MainMenuItemArray_it_t it;
    for(MainMenuItemArray_it(it, model->items); !MainMenuItemArray_end_p(it);
        MainMenuItemArray_next(it)) {
        const bool is_selected = (position == model->position);
        const uint8_t card_x = starting_x + position * (card_width + card_margin);

        // Adjust the y position for the selected card
        const uint8_t card_y_position = is_selected ? base_card_y_position - selected_card_offset :
                                                      base_card_y_position;

        // Draw card background
        canvas_set_color(canvas, is_selected ? ColorBlack : ColorWhite);
        elements_slightly_rounded_box(canvas, card_x, card_y_position, card_width, card_height);

        // Draw card outline
        canvas_set_color(canvas, ColorBlack);
        elements_slightly_rounded_frame(canvas, card_x, card_y_position, card_width, card_height);

        const uint8_t icon_width = 20;
        const uint8_t icon_x_position = card_x + (card_width - icon_width) / 2;
        const uint8_t icon_y_position = card_y_position + 3;

        if(position == 0) {
            canvas_set_color(canvas, is_selected ? ColorWhite : ColorBlack);
            canvas_draw_icon(canvas, icon_x_position, icon_y_position, &I_Wifi_icon);
        }
        if(position == 1) {
            canvas_set_color(canvas, is_selected ? ColorWhite : ColorBlack);
            canvas_draw_icon(canvas, icon_x_position, icon_y_position, &I_BLE_icon);
        }

        if(position == 2) {
            canvas_set_color(canvas, is_selected ? ColorWhite : ColorBlack);
            canvas_draw_icon(canvas, icon_x_position, icon_y_position, &I_GPS);
        }

        if(position == 3) {
            canvas_set_color(canvas, is_selected ? ColorWhite : ColorBlack);
            canvas_draw_icon(canvas, icon_x_position, icon_y_position, &I_Cog);
        }

        // Draw the card text centered at the bottom
        FuriString* disp_str = furi_string_alloc_set(MainMenuItemArray_cref(it)->label);
        elements_string_fit_width(canvas, disp_str, card_width + 40); // Fit text within the card
        canvas_set_color(canvas, is_selected ? ColorWhite : ColorBlack);
        canvas_draw_str(
            canvas, card_x + 5, card_y_position + card_height - 8, furi_string_get_cstr(disp_str));
        furi_string_free(disp_str);

        position++;
    }
}

static bool main_menu_view_input_callback(InputEvent* event, void* context) {
    MainMenu* main_menu = context;
    furi_assert(main_menu);
    bool consumed = false;

    bool locked_message_visible = false;
    with_view_model(
        main_menu->view,
        MainMenuModel * model,
        { locked_message_visible = model->locked_message_visible; },
        false);

    if((event->type != InputTypePress && event->type != InputTypeRelease) &&
       locked_message_visible) {
        with_view_model(
            main_menu->view,
            MainMenuModel * model,
            { model->locked_message_visible = false; },
            true);
        consumed = true;
    } else if(event->type == InputTypeShort) {
        switch(event->key) {
        case InputKeyLeft:
            consumed = true;
            main_menu_process_up(main_menu);
            break;
        case InputKeyRight:
            consumed = true;
            main_menu_process_down(main_menu);
            break;
        case InputKeyOk:
            consumed = true;
            main_menu_process_ok(main_menu);
            break;
        default:
            break;
        }
    } else if(event->type == InputTypeRepeat) {
        if(event->key == InputKeyUp) {
            consumed = true;
            main_menu_process_up(main_menu);
        } else if(event->key == InputKeyDown) {
            consumed = true;
            main_menu_process_down(main_menu);
        }
    }

    return consumed;
}

void main_menu_timer_callback(void* context) {
    furi_assert(context);
    MainMenu* main_menu = context;

    with_view_model(
        main_menu->view, MainMenuModel * model, { model->locked_message_visible = false; }, true);
}

MainMenu* main_menu_alloc(void) {
    MainMenu* main_menu = malloc(sizeof(MainMenu));
    main_menu->view = view_alloc();
    view_set_context(main_menu->view, main_menu);
    view_allocate_model(main_menu->view, ViewModelTypeLocking, sizeof(MainMenuModel));
    view_set_draw_callback(main_menu->view, main_menu_view_draw_callback);
    view_set_input_callback(main_menu->view, main_menu_view_input_callback);

    main_menu->locked_timer =
        furi_timer_alloc(main_menu_timer_callback, FuriTimerTypeOnce, main_menu);

    with_view_model(
        main_menu->view,
        MainMenuModel * model,
        {
            MainMenuItemArray_init(model->items);
            model->position = 0;
            model->window_position = 0;
            model->header = furi_string_alloc();
        },
        true);

    return main_menu;
}

void main_menu_free(MainMenu* main_menu) {
    furi_check(main_menu);

    with_view_model(
        main_menu->view,
        MainMenuModel * model,
        {
            furi_string_free(model->header);
            MainMenuItemArray_clear(model->items);
        },
        true);
    furi_timer_stop(main_menu->locked_timer);
    furi_timer_free(main_menu->locked_timer);
    view_free(main_menu->view);
    free(main_menu);
}

View* main_menu_get_view(MainMenu* main_menu) {
    furi_check(main_menu);
    return main_menu->view;
}

void main_menu_add_item(
    MainMenu* main_menu,
    const char* label,
    uint32_t index,
    MainMenuItemCallback callback,
    void* callback_context) {
    main_menu_add_lockable_item(main_menu, label, index, callback, callback_context, false, NULL);
}

void main_menu_add_lockable_item(
    MainMenu* main_menu,
    const char* label,
    uint32_t index,
    MainMenuItemCallback callback,
    void* callback_context,
    bool locked,
    const char* locked_message) {
    MainMenuItem* item = NULL;
    furi_check(label);
    furi_check(main_menu);
    if(locked) {
        furi_check(locked_message);
    }

    with_view_model(
        main_menu->view,
        MainMenuModel * model,
        {
            item = MainMenuItemArray_push_new(model->items);
            furi_string_set_str(item->label, label);
            item->index = index;
            item->callback = callback;
            item->callback_context = callback_context;
            item->locked = locked;
            if(locked) {
                furi_string_set_str(item->locked_message, locked_message);
            }
        },
        true);
}

void main_menu_change_item_label(MainMenu* main_menu, uint32_t index, const char* label) {
    furi_check(main_menu);
    furi_check(label);

    with_view_model(
        main_menu->view,
        MainMenuModel * model,
        {
            MainMenuItemArray_it_t it;
            for(MainMenuItemArray_it(it, model->items); !MainMenuItemArray_end_p(it);
                MainMenuItemArray_next(it)) {
                if(index == MainMenuItemArray_cref(it)->index) {
                    furi_string_set_str(MainMenuItemArray_cref(it)->label, label);
                    break;
                }
            }
        },
        true);
}

void main_menu_reset(MainMenu* main_menu) {
    furi_check(main_menu);
    view_set_orientation(main_menu->view, ViewOrientationHorizontal);

    with_view_model(
        main_menu->view,
        MainMenuModel * model,
        {
            MainMenuItemArray_reset(model->items);
            model->position = 0;
            model->window_position = 0;
            model->is_vertical = false;
            furi_string_reset(model->header);
        },
        true);
}

uint32_t main_menu_get_selected_item(MainMenu* main_menu) {
    furi_check(main_menu);

    uint32_t selected_item_index = 0;

    with_view_model(
        main_menu->view,
        MainMenuModel * model,
        {
            if(model->position < MainMenuItemArray_size(model->items)) {
                const MainMenuItem* item = MainMenuItemArray_cget(model->items, model->position);
                selected_item_index = item->index;
            }
        },
        false);

    return selected_item_index;
}

void main_menu_set_selected_item(MainMenu* main_menu, uint32_t index) {
    furi_check(main_menu);
    with_view_model(
        main_menu->view,
        MainMenuModel * model,
        {
            size_t position = 0;
            MainMenuItemArray_it_t it;
            for(MainMenuItemArray_it(it, model->items); !MainMenuItemArray_end_p(it);
                MainMenuItemArray_next(it)) {
                if(index == MainMenuItemArray_cref(it)->index) {
                    break;
                }
                position++;
            }

            const size_t items_size = MainMenuItemArray_size(model->items);

            if(position >= items_size) {
                position = 0;
            }

            model->position = position;
            model->window_position = position;

            if(model->window_position > 0) {
                model->window_position -= 1;
            }

            const size_t items_on_screen = main_menu_items_on_screen(model);

            if(items_size <= items_on_screen) {
                model->window_position = 0;
            } else {
                const size_t pos = items_size - items_on_screen;
                if(model->window_position > pos) {
                    model->window_position = pos;
                }
            }
        },
        true);
}

void main_menu_process_up(MainMenu* main_menu) {
    with_view_model(
        main_menu->view,
        MainMenuModel * model,
        {
            const size_t items_on_screen = main_menu_items_on_screen(model);
            const size_t items_size = MainMenuItemArray_size(model->items);

            if(model->position > 0) {
                model->position--;
                if((model->position == model->window_position) && (model->window_position > 0)) {
                    model->window_position--;
                }
            } else {
                model->position = items_size - 1;
                if(model->position > items_on_screen - 1) {
                    model->window_position = model->position - (items_on_screen - 1);
                }
            }
        },
        true);
}

void main_menu_process_down(MainMenu* main_menu) {
    with_view_model(
        main_menu->view,
        MainMenuModel * model,
        {
            const size_t items_on_screen = main_menu_items_on_screen(model);
            const size_t items_size = MainMenuItemArray_size(model->items);

            if(model->position < items_size - 1) {
                model->position++;
                if((model->position - model->window_position > items_on_screen - 2) &&
                   (model->window_position < items_size - items_on_screen)) {
                    model->window_position++;
                }
            } else {
                model->position = 0;
                model->window_position = 0;
            }
        },
        true);
}

void main_menu_process_ok(MainMenu* main_menu) {
    MainMenuItem* item = NULL;

    with_view_model(
        main_menu->view,
        MainMenuModel * model,
        {
            const size_t items_size = MainMenuItemArray_size(model->items);
            if(model->position < items_size) {
                item = MainMenuItemArray_get(model->items, model->position);
            }
            if(item && item->locked) {
                model->locked_message_visible = true;
                furi_timer_start(main_menu->locked_timer, furi_kernel_get_tick_frequency() * 3);
            }
        },
        true);

    if(item && !item->locked && item->callback) {
        item->callback(item->callback_context, item->index);
    }
}

void main_menu_set_header(MainMenu* main_menu, const char* header) {
    furi_check(main_menu);

    with_view_model(
        main_menu->view,
        MainMenuModel * model,
        {
            if(header == NULL) {
                furi_string_reset(model->header);
            } else {
                furi_string_set_str(model->header, header);
            }
        },
        true);
}

void main_menu_set_orientation(MainMenu* main_menu, ViewOrientation orientation) {
    furi_check(main_menu);
    const bool is_vertical = orientation == ViewOrientationVertical ||
                             orientation == ViewOrientationVerticalFlip;

    view_set_orientation(main_menu->view, orientation);

    with_view_model(
        main_menu->view,
        MainMenuModel * model,
        {
            model->is_vertical = is_vertical;

            // Recalculating the position
            // Need if _set_orientation is called after _set_selected_item
            size_t position = model->position;
            const size_t items_size = MainMenuItemArray_size(model->items);
            const size_t items_on_screen = main_menu_items_on_screen(model);

            if(position >= items_size) {
                position = 0;
            }

            model->position = position;
            model->window_position = position;

            if(model->window_position > 0) {
                model->window_position -= 1;
            }

            if(items_size <= items_on_screen) {
                model->window_position = 0;
            } else {
                const size_t pos = items_size - items_on_screen;
                if(model->window_position > pos) {
                    model->window_position = pos;
                }
            }
        },
        true);
}