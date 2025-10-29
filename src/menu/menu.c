#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/display.h>
#include <zephyr/input/input.h>
#include <zephyr/drivers/sensor.h>

#include <zephyr/logging/log.h>

#include <menu/menu.h>
#include <menu/pannel.h>

LOG_MODULE_REGISTER(menu, CONFIG_LOG_DEFAULT_LEVEL);

#define MENU_STACK_SIZE 4096
#define MENU_GROUP_STACK_SIZE 8

struct menu_t {
    struct menu_item_t *item;
    struct menu_group_t *groups;
    struct menu_group_t *main_group;
    struct pannel_t *pannel;
    struct menu_group_t *group_stack[8];
    int group_stack_top;
    struct menu_item_t *item_nav_from;
    struct menu_item_t *item_nav_to;
    k_tid_t tid;
    struct k_thread thread;
    K_KERNEL_STACK_MEMBER(stack, 4096);
    
    menu_state_t state;
    struct menu_item_t *current_item;
    struct menu_item_t *selected_item;
    struct menu_item_t *editing_item;
    struct k_sem render_sem;
    struct k_mutex pannel_mutex;
    struct k_mutex state_mutex;
    struct k_msgq update_msgq;
    
    const struct device *qdec_dev;
    const struct device *adc2_dev;
    
    int32_t qdec_value;
    bool key1_pressed;
    bool key2_pressed;
    int16_t adc2_value;
    bool needs_render;
    struct menu_group_t *group_to_refresh;
    struct menu_item_t *item_to_refresh;
    struct sensor_trigger trigger;
    bool disable_qdec;
    void *driver;
    struct k_timer label_refresh_timer;
    struct k_work label_refresh_work;
};

static void label_refresh_work_handler(struct k_work *work);
static struct menu_item_t *find_menu_item_by_id(struct menu_item_t *root, uint8_t id);
static void menu_get_item_layout(struct menu_group_t *group, struct menu_item_t *item_to_find, uint16_t *out_x, uint16_t *out_y, uint16_t *out_w);
static void menu_refresh_item_selection(struct menu_t *menu, struct menu_item_t *last_item, struct menu_item_t *current_item);
static void menu_refresh_single_item(struct menu_t *menu, struct menu_item_t *item);
static void menu_refresh_single_item_fast(struct menu_item_t *item, bool selected);
static void menu_render_item_value_only(struct menu_item_t *item);
static void menu_update_group_visibility(struct menu_t *menu);
static void _menu_update_group_visibility_nolock(struct menu_t *menu);
static struct menu_group_t *find_group_by_bind_item(struct menu_t *menu, struct menu_item_t *item);
static void render_truncated_text(struct pannel_t *pannel, const char *text, uint16_t x, uint16_t y, uint16_t color, uint16_t max_width);
static void menu_render_list_item_at_index(struct menu_t *menu, struct menu_item_t *item, uint8_t index, bool selected);
static void menu_render_input_min_max_editing(struct menu_t *menu, struct menu_item_t *item);


static void menu_render_group_chrome(struct menu_t *menu, struct menu_group_t *group)
{
    if (!menu || !group || !group->visible) {
        return;
    }

    pannel_render_rect(menu->pannel, group->x, group->y, group->width, group->height, group->color, false);

    if (group->title[0] != '\0') {
        size_t title_len = strlen((const char *)group->title);
        uint16_t title_width = title_len * CONFIG_FONT_WIDTH;
        uint16_t title_x = group->x + (group->width / 2) - (title_width / 2);
        uint16_t gap_x = title_x - 2;
        uint16_t gap_width = title_width + 4;
        pannel_render_rect(menu->pannel, gap_x, group->y, gap_width, 1, COLOR_BLACK, true);
        pannel_render_txt(menu->pannel, group->title, title_x, group->y - (CONFIG_FONT_HEIGHT / 2), COLOR_WHITE);
    }
}

static void render_truncated_text(struct pannel_t *pannel, const char *text, uint16_t x, uint16_t y, uint16_t color, uint16_t max_width)
{
    if (max_width < CONFIG_FONT_WIDTH) {
        return;
    }

    size_t text_len = strlen(text);
    uint16_t text_width = text_len * CONFIG_FONT_WIDTH;

    if (text_width <= max_width) {
        pannel_render_txt(pannel, (uint8_t *)text, x, y, color);
    } else {
        size_t max_chars = max_width / CONFIG_FONT_WIDTH;
        char truncated_text[33];
        if (max_chars > 32) max_chars = 32;
        strncpy(truncated_text, text, max_chars);
        truncated_text[max_chars] = '\0';
        pannel_render_txt(pannel, (uint8_t *)truncated_text, x, y, color);
    }
}

static void menu_render_item(struct menu_t *menu, struct menu_item_t *item,
                           uint16_t x, uint16_t y, bool selected, uint16_t render_width)
{
    if (!menu || !item || !menu->pannel || !item->visible) {
        return;
    }

    uint16_t text_color = COLOR_WHITE;
    uint16_t bg_color = COLOR_BLACK;
    size_t name_len = 0;
    uint16_t text_y = y + 2;

    if (selected) {
        text_color = COLOR_BLACK;
        bg_color = COLOR_WHITE;
    } else if (item->style & MENU_STYLE_CUSTOM_COLOR) {
        text_color = (item->style >> MENU_STYLE_COLOR_SHIFT);
    } else if (item->style & MENU_STYLE_HIGHLIGHT) {
        text_color = COLOR_YELLOW;
    } else if (item->style & MENU_STYLE_DISABLED) {
        text_color = COLOR_GRAY;
    }

    if (item->name[0] != '\0') {
        name_len = strlen((const char *)item->name);
    }

    uint16_t content_width = CONFIG_FONT_WIDTH * name_len;
    char value_buf[16] = {0};

    if (item->type == MENU_ITEM_TYPE_INPUT) {
        int32_t value_to_display = (menu->editing_item == item) ? item->input.editing_value : item->input.value;
        if (item->input.value_get_str_cb) {
            item->input.value_get_str_cb(item, value_buf, sizeof(value_buf));
        } else {
            snprintf(value_buf, sizeof(value_buf), "%d", value_to_display);
        }
        strncpy(item->input.rendered_value_str, value_buf, sizeof(item->input.rendered_value_str) - 1);
        item->input.rendered_value_str[sizeof(item->input.rendered_value_str) - 1] = '\0';
        content_width += (5 + strlen(value_buf)) * CONFIG_FONT_WIDTH;
    } else if (item->type == MENU_ITEM_TYPE_LABEL && item->label_cb) {
        char label_buf[32] = {0};
        item->label_cb(item, label_buf, sizeof(label_buf));
        content_width += 1 + strlen(label_buf) * CONFIG_FONT_WIDTH;
    } else if (item->type == MENU_ITEM_TYPE_SWITCH) {
        content_width += 5 + strlen("OFF") * CONFIG_FONT_WIDTH;
    }

    uint16_t box_width = (render_width > 0) ? render_width : content_width;

    pannel_render_rect(menu->pannel, x - 2, y,
                       box_width + 4,
                       CONFIG_FONT_HEIGHT + 4, bg_color, true);

    uint16_t text_x = x;
    if (item->group && item->group->item_text_align) {
         if (item->group->item_text_align & MENU_STYLE_CENTER) {
            text_x = x + (box_width - content_width) / 2;
        } else if (item->group->item_text_align & MENU_STYLE_RIGHT) {
            text_x = x + box_width - content_width;
        }
    }

    uint16_t available_width = 0;
    if (item->group) {
        uint16_t group_right_edge = item->group->x + item->group->width - 2;
        if (text_x < group_right_edge) {
            available_width = group_right_edge - text_x;
        }
    } else {
        available_width = 9999;
    }

    char full_text[128] = {0};
    char temp_buf[64] = {0};

    if (!(item->style & MENU_STYLE_VALUE_ONLY)) {
        strncat(full_text, (const char *)item->name, sizeof(full_text) - strlen(full_text) - 1);
    }

    switch(item->type) {
        case MENU_ITEM_TYPE_LABEL:
            if (item->label_cb) {
                item->label_cb(item, temp_buf, sizeof(temp_buf));
                strncat(full_text, ":", sizeof(full_text) - strlen(full_text) - 1);
                strncat(full_text, temp_buf, sizeof(full_text) - strlen(full_text) - 1);
                strncpy(item->label.rendered_label_str, temp_buf, sizeof(item->label.rendered_label_str) - 1);
                item->label.rendered_label_str[sizeof(item->label.rendered_label_str) - 1] = '\0';
            }
            break;
        case MENU_ITEM_TYPE_INPUT:
            if (!item->input.value_get_str_cb || (value_buf[0] != ':' && value_buf[0] != ' ')) {
                strncat(full_text, ":", sizeof(full_text) - strlen(full_text) - 1);
            }
            strncat(full_text, value_buf, sizeof(full_text) - strlen(full_text) - 1);
            break;
        case MENU_ITEM_TYPE_SWITCH:
            bool is_on = (menu->editing_item == item) ? item->switch_ctrl.editing_is_on : item->switch_ctrl.is_on;
            const char *switch_str;
            if (is_on) {
                switch_str = item->switch_ctrl.text_on ? item->switch_ctrl.text_on : "ON";
            } else {
                switch_str = item->switch_ctrl.text_off ? item->switch_ctrl.text_off : "OFF";
            }

            if (!(item->style & MENU_STYLE_VALUE_ONLY)) {
                strncat(full_text, ":", sizeof(full_text) - strlen(full_text) - 1);
            }
            strncat(full_text, switch_str, sizeof(full_text) - strlen(full_text) - 1);
            strncpy(item->switch_ctrl.rendered_value_str, switch_str, sizeof(item->switch_ctrl.rendered_value_str) - 1);
            item->switch_ctrl.rendered_value_str[sizeof(item->switch_ctrl.rendered_value_str) - 1] = '\0';
            break;
        case MENU_ITEM_TYPE_LIST:
            if (menu->editing_item == item) {
            // In editing mode, we might just show the name, as the list is rendered separately
            } else {
                if (item->list.num_options > 0 && item->list.selected_index < item->list.num_options) {
                    const char *selected_option = item->list.options[item->list.selected_index];
                    if (!(item->style & MENU_STYLE_VALUE_ONLY)) {
                        strncat(full_text, ":", sizeof(full_text) - strlen(full_text) - 1);
                    }
                    strncat(full_text, selected_option, sizeof(full_text) - strlen(full_text) - 1);
                    strncpy(item->list.rendered_value_str, selected_option, sizeof(item->list.rendered_value_str) - 1);
                    item->list.rendered_value_str[sizeof(item->list.rendered_value_str) - 1] = '\0';
                }
            }
            break;
        case MENU_ITEM_TYPE_CHECKBOX:
            {
                if (item->style & MENU_STYLE_CHECKBOX_IMG) {
                    const uint8_t *img_buf = item->checkbox.is_on ?
                                                (const uint8_t *)item->checkbox.text_on :
                                                (const uint8_t *)item->checkbox.text_off;
                    if (img_buf) {
                        uint16_t img_x = x;
                        if (item->style & MENU_STYLE_CENTER) {
                            img_x = x + (render_width - item->checkbox.img_width) / 2;
                        } else if (item->style & MENU_STYLE_RIGHT) {
                            img_x = x + render_width - item->checkbox.img_width;
                        }
                        pannel_render_buffer(menu->pannel, img_x, y, item->checkbox.img_width, item->checkbox.img_height, (uint8_t *)img_buf);
                    }
                    full_text[0] = '\0'; // Clear text to prevent rendering
                } else {
                    const char *checkbox_str = item->checkbox.is_on ?
                                                (item->checkbox.text_on ? item->checkbox.text_on : "ON") :
                                                (item->checkbox.text_off ? item->checkbox.text_off : "OFF");

                    if (item->style & MENU_STYLE_VALUE_ONLY) {
                        full_text[0] = '\0';
                    } else {
                        strncat(full_text, ":", sizeof(full_text) - strlen(full_text) - 1);
                    }
                    strncat(full_text, checkbox_str, sizeof(full_text) - strlen(full_text) - 1);
                    strncpy(item->checkbox.rendered_value_str, checkbox_str, sizeof(item->checkbox.rendered_value_str) - 1);
                    item->checkbox.rendered_value_str[sizeof(item->checkbox.rendered_value_str) - 1] = '\0';
                }
            }
            break;
        case MENU_ITEM_TYPE_INPUT_MIN_MAX:
           {
               snprintf(temp_buf, sizeof(temp_buf), "%d-%d", item->input_min_max.min_value, item->input_min_max.max_value);
               if (!(item->style & MENU_STYLE_VALUE_ONLY)) {
                   strncat(full_text, ":", sizeof(full_text) - strlen(full_text) - 1);
               }
               strncat(full_text, temp_buf, sizeof(full_text) - strlen(full_text) - 1);
               strncpy(item->input_min_max.rendered_value_str, temp_buf, sizeof(item->input_min_max.rendered_value_str) - 1);
               item->input_min_max.rendered_value_str[sizeof(item->input_min_max.rendered_value_str) - 1] = '\0';
               break;
           }
        default:
                break;
    }

    render_truncated_text(menu->pannel, full_text, text_x, text_y, text_color, available_width);
}

static void menu_render_list_item_at_index(struct menu_t *menu, struct menu_item_t *item, uint8_t index, bool selected)
{
    if (!menu || !item || index >= item->list.num_options) {
        return;
    }

    struct display_capabilities *caps;
    pannel_get_capabilities(menu->pannel, &caps);

    uint16_t start_y = 15;
    uint16_t step_y = CONFIG_FONT_HEIGHT + 5;
    uint16_t step_x = 8 * CONFIG_FONT_WIDTH;

    uint16_t text_color = selected ? COLOR_BLACK : COLOR_WHITE;
    uint16_t bg_color = selected ? COLOR_WHITE : COLOR_BLACK;
    uint16_t current_x;
    uint16_t current_y = start_y;

    uint16_t text_width = strlen(item->list.options[index]) * CONFIG_FONT_WIDTH;

    if (item->list.layout & MENU_LAYOUT_VERTICAL) {
        current_y += index * step_y;
        current_x = (caps->x_resolution / 2) - (text_width / 2);
    } else {
        current_x = 10;
        current_x += index * step_x;
    }

    pannel_render_rect(menu->pannel, current_x - 2, current_y, strlen(item->list.options[index]) * CONFIG_FONT_WIDTH + 4, CONFIG_FONT_HEIGHT + 4, bg_color, true);
    pannel_render_txt(menu->pannel, (uint8_t *)item->list.options[index], current_x, current_y + 2, text_color);
}

static void menu_render_list_editing(struct menu_t *menu, struct menu_item_t *item)
{
    if (!menu || !item || item->type != MENU_ITEM_TYPE_LIST) {
        return;
    }

    struct display_capabilities *caps;
    pannel_get_capabilities(menu->pannel, &caps);

    pannel_render_rect(menu->pannel, 5, 5, caps->x_resolution - 10, caps->y_resolution - 10, COLOR_WHITE, false);
    if (item->list.title) {
        uint16_t title_len = strlen(item->list.title);
        uint16_t title_width = title_len * CONFIG_FONT_WIDTH;
        uint16_t title_x = (caps->x_resolution / 2) - (title_width / 2);
        pannel_render_rect(menu->pannel, title_x - 2, 5, title_width + 4, 1, COLOR_BLACK, true);
        pannel_render_txt(menu->pannel, (uint8_t *)item->list.title, title_x, 5 - (CONFIG_FONT_HEIGHT / 2), COLOR_WHITE);
    }

    for (uint8_t i = 0; i < item->list.num_options; i++) {
        bool selected = (i == item->list.editing_index);
        menu_render_list_item_at_index(menu, item, i, selected);
    }
}


static void menu_render_input_min_max_item_part(struct menu_t *menu, struct menu_item_t *item, uint8_t target, bool selected)
{
   struct display_capabilities *caps;
   pannel_get_capabilities(menu->pannel, &caps);
   char buf[32];

   if (target == 0) { // Min
       uint16_t y_pos = 20;
       snprintf(buf, sizeof(buf), "Min: %d", item->input_min_max.editing_min_value);
       pannel_render_rect(menu->pannel, 10, y_pos, caps->x_resolution - 20, CONFIG_FONT_HEIGHT + 4, selected ? COLOR_WHITE : COLOR_BLACK, true);
       pannel_render_txt(menu->pannel, (uint8_t *)buf, 12, y_pos + 2, selected ? COLOR_BLACK : COLOR_WHITE);
   } else if (target == 1) { // Max
       uint16_t y_pos = 20 + CONFIG_FONT_HEIGHT + 10;
       snprintf(buf, sizeof(buf), "Max: %d", item->input_min_max.editing_max_value);
       pannel_render_rect(menu->pannel, 10, y_pos, caps->x_resolution - 20, CONFIG_FONT_HEIGHT + 4, selected ? COLOR_WHITE : COLOR_BLACK, true);
       pannel_render_txt(menu->pannel, (uint8_t *)buf, 12, y_pos + 2, selected ? COLOR_BLACK : COLOR_WHITE);
   } else { // Buttons
       uint16_t y_pos = 20 + CONFIG_FONT_HEIGHT + 10 + CONFIG_FONT_HEIGHT + 15;
       uint16_t button_width = 40;
       uint16_t button_spacing = 20;
       uint16_t total_buttons_width = 2 * button_width + button_spacing;
       uint16_t buttons_x_start = (caps->x_resolution - total_buttons_width) / 2;

       if (target == 2) { // OK
           pannel_render_rect(menu->pannel, buttons_x_start, y_pos, button_width, CONFIG_FONT_HEIGHT + 4, selected ? COLOR_WHITE : COLOR_BLACK, true);
           pannel_render_txt(menu->pannel, (uint8_t *)"OK", buttons_x_start + (button_width - 2 * CONFIG_FONT_WIDTH) / 2, y_pos + 2, selected ? COLOR_BLACK : COLOR_WHITE);
       } else if (target == 3) { // Cancel
           pannel_render_rect(menu->pannel, buttons_x_start + button_width + button_spacing, y_pos, button_width, CONFIG_FONT_HEIGHT + 4, selected ? COLOR_WHITE : COLOR_BLACK, true);
           pannel_render_txt(menu->pannel, (uint8_t *)"Cancel", buttons_x_start + button_width + button_spacing + (button_width - 6 * CONFIG_FONT_WIDTH) / 2, y_pos + 2, selected ? COLOR_BLACK : COLOR_WHITE);
       }
   }
}

static void menu_render_input_min_max_editing(struct menu_t *menu, struct menu_item_t *item)
{
    if (!menu || !item || item->type != MENU_ITEM_TYPE_INPUT_MIN_MAX) {
        return;
    }

    struct display_capabilities *caps;
    pannel_get_capabilities(menu->pannel, &caps);

    pannel_render_rect(menu->pannel, 5, 5, caps->x_resolution - 10, caps->y_resolution - 10, COLOR_WHITE, false);

    uint16_t title_len = strlen((const char *)item->name);
    uint16_t title_width = title_len * CONFIG_FONT_WIDTH;
    uint16_t title_x = (caps->x_resolution / 2) - (title_width / 2);
    pannel_render_rect(menu->pannel, title_x - 2, 5, title_width + 4, 1, COLOR_BLACK, true);
    pannel_render_txt(menu->pannel, (uint8_t *)item->name, title_x, 5 - (CONFIG_FONT_HEIGHT / 2), COLOR_WHITE);

   menu_render_input_min_max_item_part(menu, item, 0, item->input_min_max.editing_target == 0);
   menu_render_input_min_max_item_part(menu, item, 1, item->input_min_max.editing_target == 1);
   menu_render_input_min_max_item_part(menu, item, 2, item->input_min_max.editing_target == 2);
   menu_render_input_min_max_item_part(menu, item, 3, item->input_min_max.editing_target == 3);
}

static void menu_render_group(struct menu_t *menu, struct menu_group_t *group)
{
    if (!menu || !group || !group->visible) {
        return;
    }

    menu_render_group_chrome(menu, group);

    uint16_t total_height = 0;
    struct menu_item_t *item = group->items;
    int visible_items = 0;
    while (item) {
        if (item->visible) {
            visible_items++;
        }
        item = item->group_next;
    }
    total_height = visible_items * (CONFIG_FONT_HEIGHT + 5);

    uint16_t start_x = group->x + 5;
    uint16_t start_y = group->y + 5;
    if (group->align & MENU_ALIGN_V_CENTER) {
        start_y = group->y + (group->height - total_height) / 2;
    }

    uint16_t render_width = group->width - 10;

    item = group->items;
    uint16_t current_y = start_y;
    while (item) {
        if (item->visible) {
            bool selected = (item == menu->current_item);
            menu_render_item(menu, item, start_x, current_y, selected, render_width);
            current_y += CONFIG_FONT_HEIGHT + 5;
        }
        item = item->group_next;
    }
}

static void menu_render(struct menu_t *menu)
{
    struct display_capabilities *caps;
    
    if (!menu || !menu->pannel) {
        return;
    }

    pannel_get_capabilities(menu->pannel, &caps);
    
    pannel_render_clear(menu->pannel, COLOR_BLACK);

    if (menu->editing_item && menu->editing_item->type == MENU_ITEM_TYPE_LIST) {
        menu_render_list_editing(menu, menu->editing_item);
        return;
    }

   if (menu->editing_item && menu->editing_item->type == MENU_ITEM_TYPE_INPUT_MIN_MAX) {
       menu_render_input_min_max_editing(menu, menu->editing_item);
       return;
   }

    struct menu_group_t *group = menu->groups;
    while (group) {
        menu_render_group(menu, group);
        group = group->next;
    }

    if (menu->group_stack_top == -1) {
        uint16_t y = 10;
        uint16_t x = 10;
        struct menu_item_t *item = menu->item;
        while (item) {
            if (!item->group) {
                bool selected = (item == menu->current_item);
                menu_render_item(menu, item, x, y, selected, 0);
                y += CONFIG_FONT_HEIGHT + 5;
                if (y > caps->y_resolution) {
                    break;
                }
            }
            item = item->next;
        }
    }
}

static void menu_refresh_group(struct menu_t *menu, struct menu_group_t *group)
{
    if (!menu || !group || !group->visible) {
        return;
    }

    k_mutex_lock(&menu->pannel_mutex, K_FOREVER);

    pannel_render_rect(menu->pannel, group->x, group->y, group->width, group->height, COLOR_BLACK, true);

    menu_render_group(menu, group);

    k_mutex_unlock(&menu->pannel_mutex);
}

static void menu_process_input(struct menu_t *menu, menu_input_event_t *event)
{
    bool force_render = false;
    struct menu_item_t *last_item;
    menu_input_event_t ev = {0};
    
    if (!menu || !event) {
        return;
    }

    k_mutex_lock(&menu->state_mutex, K_FOREVER);
    
    last_item = menu->current_item;
    
    switch (event->type) {
        case INPUT_TYPE_QDEC:
            if (menu->editing_item && menu->editing_item->input.dev == event->dev) {
                if (menu->editing_item->input.cb && !menu->editing_item->input.cb(menu->editing_item, event)) {
                    break;
                }
                menu->editing_item->input.user_adjusted = true;
                menu->editing_item->input.editing_value += event->value > 0 ? menu->editing_item->input.step : -menu->editing_item->input.step;
                if (menu->editing_item->input.editing_value > menu->editing_item->input.max) {
                    menu->editing_item->input.editing_value = menu->editing_item->input.max;
                } else if (menu->editing_item->input.editing_value < menu->editing_item->input.min) {
                    menu->editing_item->input.editing_value = menu->editing_item->input.min;
                }
                force_render = true;
            } else if (menu->editing_item && menu->editing_item->type == MENU_ITEM_TYPE_SWITCH) {
                menu->editing_item->switch_ctrl.editing_is_on = !menu->editing_item->switch_ctrl.editing_is_on;
            } else if (menu->editing_item && menu->editing_item->type == MENU_ITEM_TYPE_LIST) {
                uint8_t last_index = menu->editing_item->list.editing_index;
                if (event->value > 0) {
                    if (menu->editing_item->list.editing_index < menu->editing_item->list.num_options - 1) {
                        menu->editing_item->list.editing_index++;
                    }
                } else if (event->value < 0) {
                    if (menu->editing_item->list.editing_index > 0) {
                        menu->editing_item->list.editing_index--;
                    }
                }

                if (last_index != menu->editing_item->list.editing_index) {
                    k_mutex_lock(&menu->pannel_mutex, K_FOREVER);
                    menu_render_list_item_at_index(menu, menu->editing_item, last_index, false);
                    menu_render_list_item_at_index(menu, menu->editing_item, menu->editing_item->list.editing_index, true);
                    k_mutex_unlock(&menu->pannel_mutex);
                }
           } else if (menu->editing_item && menu->editing_item->type == MENU_ITEM_TYPE_INPUT_MIN_MAX) {
               struct item_input_min_max_t *min_max = &menu->editing_item->input_min_max;
               if (min_max->editing_target < 2) {
                   int32_t delta = event->value > 0 ? min_max->step : -min_max->step;
                   if (min_max->editing_target == 0) {
                       min_max->editing_min_value += delta;
                       if (min_max->editing_min_value > min_max->editing_max_value) {
                           min_max->editing_min_value = min_max->editing_max_value;
                       }
                       if (min_max->editing_min_value < min_max->min_limit) {
                           min_max->editing_min_value = min_max->min_limit;
                       }
                   } else {
                       min_max->editing_max_value += delta;
                       if (min_max->editing_max_value < min_max->editing_min_value) {
                           min_max->editing_max_value = min_max->editing_min_value;
                       }
                       if (min_max->editing_max_value > min_max->max_limit) {
                           min_max->editing_max_value = min_max->max_limit;
                       }
                   }

                   k_mutex_lock(&menu->pannel_mutex, K_FOREVER);
                   menu_render_input_min_max_item_part(menu, menu->editing_item, min_max->editing_target, true);
                   k_mutex_unlock(&menu->pannel_mutex);
               } else {
                   if (event->value != 0) {
                       uint8_t old_target = min_max->editing_target;
                       min_max->editing_target = (old_target == 2) ? 3 : 2;

                       k_mutex_lock(&menu->pannel_mutex, K_FOREVER);
                       menu_render_input_min_max_item_part(menu, menu->editing_item, old_target, false);
                       menu_render_input_min_max_item_part(menu, menu->editing_item, min_max->editing_target, true);
                       k_mutex_unlock(&menu->pannel_mutex);
                   }
               }
            } else if (menu->group_stack_top > -1) {
                if (menu->editing_item) {
                    break;
                }
                if (event->value > 0) {
                    struct menu_item_t *next_item = menu->current_item->group_next;
                    while (next_item && (next_item->type == MENU_ITEM_TYPE_LABEL || !next_item->visible || (next_item->style & MENU_STYLE_NON_NAVIGABLE))) {
                        next_item = next_item->group_next;
                    }
                    if (next_item) {
                        menu->current_item = next_item;
                    }
                } else if (event->value < 0) {
                    struct menu_item_t *prev_item = menu->current_item->group_prev;
                    while (prev_item && (prev_item->type == MENU_ITEM_TYPE_LABEL || !prev_item->visible || (prev_item->style & MENU_STYLE_NON_NAVIGABLE))) {
                        prev_item = prev_item->group_prev;
                    }
                    if (prev_item) {
                        menu->current_item = prev_item;
                    }
                }
            } else {
            key_process:
                if (menu->editing_item) {
                    break;
                }
                if (event->value > 0) {
                    struct menu_item_t *next_item = menu->current_item->next;
                    while (next_item) {
                        bool is_label = next_item->type == MENU_ITEM_TYPE_LABEL;
                        bool is_hidden = !next_item->visible;
                        bool in_inactive_group = next_item->group && !next_item->group->always_visible && next_item->group->bind_item != NULL;
                        bool is_non_navigable = next_item->style & MENU_STYLE_NON_NAVIGABLE;
                        if (is_label || is_hidden || in_inactive_group || is_non_navigable) {
                            next_item = next_item->next;
                        } else {
                            break;
                        }
                    }
                    if (next_item) {
                        menu->current_item = next_item;
                    }
                } else if (event->value < 0) {
                    struct menu_item_t *prev_item = menu->current_item->prev;
                    while (prev_item) {
                        bool is_label = prev_item->type == MENU_ITEM_TYPE_LABEL;
                        bool is_hidden = !prev_item->visible;
                        bool in_inactive_group = prev_item->group && !prev_item->group->always_visible && prev_item->group->bind_item != NULL;
                        bool is_non_navigable = prev_item->style & MENU_STYLE_NON_NAVIGABLE;
                        if (is_label || is_hidden || in_inactive_group || is_non_navigable) {
                            prev_item = prev_item->prev;
                        } else {
                            break;
                        }
                    }
                    if (prev_item) {
                        menu->current_item = prev_item;
                    }
                }
            }
            break;

        case INPUT_TYPE_KEY1:
            if (event->pressed) {
               if (menu->editing_item) {
                   if (menu->editing_item->type == MENU_ITEM_TYPE_INPUT_MIN_MAX) {
                       struct item_input_min_max_t *min_max = &menu->editing_item->input_min_max;
                       uint8_t old_target = min_max->editing_target;

                       if (min_max->editing_target < 2) {
                           min_max->editing_target++;
                           k_mutex_lock(&menu->pannel_mutex, K_FOREVER);
                           menu_render_input_min_max_item_part(menu, menu->editing_item, old_target, false);
                           menu_render_input_min_max_item_part(menu, menu->editing_item, min_max->editing_target, true);
                           k_mutex_unlock(&menu->pannel_mutex);
                       } else if (min_max->editing_target == 2) {
                           min_max->min_value = min_max->editing_min_value;
                           min_max->max_value = min_max->editing_max_value;
                           if (min_max->cb) {
                               min_max->cb(menu->editing_item, min_max->min_value, min_max->max_value);
                           }
                           menu->editing_item = NULL;
                           force_render = true;
                       } else {
                           menu->editing_item = NULL;
                           force_render = true;
                       }
                   } else {
                       struct menu_item_t *item_exiting_edit = menu->editing_item;

                       switch (item_exiting_edit->type) {
                           case MENU_ITEM_TYPE_INPUT:
                               item_exiting_edit->input.value = item_exiting_edit->input.editing_value;
                               if (item_exiting_edit->input.cb) {
                                   if (item_exiting_edit->input.cb(item_exiting_edit, &ev)) {
                                       item_exiting_edit->input.value = ev.value;
                                   }
                               }
                               break;
                           case MENU_ITEM_TYPE_SWITCH:
                               item_exiting_edit->switch_ctrl.is_on = item_exiting_edit->switch_ctrl.editing_is_on;
                               if (item_exiting_edit->switch_ctrl.cb) {
                                   item_exiting_edit->switch_ctrl.cb(item_exiting_edit, item_exiting_edit->switch_ctrl.is_on);
                               }
                               break;
                           case MENU_ITEM_TYPE_LIST:
                               item_exiting_edit->list.selected_index = item_exiting_edit->list.editing_index;
                               if (item_exiting_edit->list.cb) {
                                   item_exiting_edit->list.cb(item_exiting_edit, item_exiting_edit->list.selected_index);
                               }
                               break;
                           default:
                               break;
                       }
                       
                       if (item_exiting_edit->type == MENU_ITEM_TYPE_INPUT || item_exiting_edit->type == MENU_ITEM_TYPE_SWITCH) {
                           menu->item_to_refresh = item_exiting_edit;
                       } else {
                           force_render = true;
                       }
                       menu->editing_item = NULL;
                   }
                } else if (menu->current_item) {
                    switch (menu->current_item->type) {
                       case MENU_ITEM_TYPE_INPUT_MIN_MAX:
                           menu->editing_item = menu->current_item;
                           menu->editing_item->input_min_max.editing_min_value = menu->editing_item->input_min_max.min_value;
                           menu->editing_item->input_min_max.editing_max_value = menu->editing_item->input_min_max.max_value;
                           menu->editing_item->input_min_max.editing_target = 0;
                           force_render = true;
                           break;
                        case MENU_ITEM_TYPE_INPUT:
                            menu->editing_item = menu->current_item;
                            menu->editing_item->input.editing_value = menu->editing_item->input.live_value;
                            menu->editing_item->input.user_adjusted = false;
                            if (menu->editing_item->input.cb) {
                                if (menu->editing_item->input.cb(menu->editing_item, &ev)) {
                                    menu->editing_item->input.editing_value = ev.value;
                                }
                            }
                            menu->item_to_refresh = menu->current_item;
                            break;
                        case MENU_ITEM_TYPE_SWITCH:
                            menu->editing_item = menu->current_item;
                            menu->editing_item->switch_ctrl.editing_is_on = menu->editing_item->switch_ctrl.is_on;
                            menu->item_to_refresh = menu->current_item;
                            break;
                        case MENU_ITEM_TYPE_LIST:
                            menu->editing_item = menu->current_item;
                            menu->editing_item->list.editing_index = menu->editing_item->list.selected_index;
                            force_render = true;
                            break;
                        case MENU_ITEM_TYPE_CHECKBOX:
                            menu->current_item->checkbox.is_on = !menu->current_item->checkbox.is_on;
                            if (menu->current_item->checkbox.cb) {
                                menu->current_item->checkbox.cb(menu->current_item, menu->current_item->checkbox.is_on);
                            }
                            menu->item_to_refresh = menu->current_item;
                            break;
                        default: // For NORMAL items or items without a specific edit mode
                            {
                                struct menu_group_t *bound_group = find_group_by_bind_item(menu, menu->current_item);
                                if (bound_group) {
                                    bool already_active = false;
                                    if (menu->group_stack_top > -1) {
                                        if (menu->group_stack[menu->group_stack_top] == bound_group) {
                                            already_active = true;
                                        }
                                    }

                                    if (!already_active && menu->group_stack_top < (MENU_GROUP_STACK_SIZE - 1)) {
                                        menu->group_stack_top++;
                                        menu->group_stack[menu->group_stack_top] = bound_group;
                                        force_render = true;

                                        struct menu_item_t *first_item = bound_group->items;
                                        while(first_item && (first_item->type == MENU_ITEM_TYPE_LABEL || !first_item->visible)) {
                                            first_item = first_item->group_next;
                                        }
                                        if (first_item) {
                                            menu->current_item = first_item;
                                        }
                                    }
                                } else if (menu->current_item->items) {
                                    menu->current_item = menu->current_item->items;
                                } else if (menu->current_item->cb) {
                                    menu->current_item->cb(menu->current_item, menu->current_item->id);
                                    force_render = true;
                                }
                            }
                            break;
                    }
                }
            }
            break;

        case INPUT_TYPE_KEY2:
            if (event->pressed) {
                if (menu->editing_item) {
                   if (menu->editing_item->type == MENU_ITEM_TYPE_INPUT_MIN_MAX) {
                       menu->editing_item = NULL;
                       force_render = true;
                   } else {
                       struct menu_item_t *item_exiting_edit = menu->editing_item;
                       if (item_exiting_edit->type == MENU_ITEM_TYPE_INPUT && item_exiting_edit->input.cb) {
                           item_exiting_edit->input.cb(item_exiting_edit, NULL); // Notify callback of cancellation
                       }
                       
                       if (item_exiting_edit->type == MENU_ITEM_TYPE_INPUT || item_exiting_edit->type == MENU_ITEM_TYPE_SWITCH) {
                           menu->item_to_refresh = item_exiting_edit;
                       } else {
                           force_render = true;
                       }
                       menu->editing_item = NULL;
                   }
                } else if (menu->group_stack_top > -1) {
                    struct menu_group_t *exited_group = menu->group_stack[menu->group_stack_top];
                    menu->group_stack[menu->group_stack_top] = NULL;
                    menu->group_stack_top--;
                    force_render = true;
                    if (exited_group && exited_group->bind_item) {
                        menu->current_item = exited_group->bind_item;
                    }
                } else if (menu->current_item && menu->current_item->parent) {
                    menu->current_item = menu->current_item->parent;
                }
            }
            break;
        case INPUT_TYPE_KEY3:
            if (event->pressed) {
                event->value = 1;
                goto key_process;
            }
            break;
        case INPUT_TYPE_KEY4:
            if (event->pressed) {
                event->value = -1;
                goto key_process;
            }
            break;
        case INPUT_TYPE_KEY5:
        case INPUT_TYPE_KEY6:
            if (event->pressed && menu->editing_item && menu->editing_item->type == MENU_ITEM_TYPE_SWITCH) {
                menu->editing_item->switch_ctrl.editing_is_on = !menu->editing_item->switch_ctrl.editing_is_on;
            }
            break;
        default:
            break;
    }
    
    if (last_item != menu->current_item || force_render || menu->group_to_refresh || menu->item_to_refresh) {
        _menu_update_group_visibility_nolock(menu);

        if (menu->group_to_refresh) {
            /* Group refresh is pending, do nothing here */
        } else if (menu->item_to_refresh) {
            /* Single item refresh is pending */
        } else if (!force_render && last_item && last_item->group && last_item->group == menu->current_item->group) {
            menu->item_nav_from = last_item;
            menu->item_nav_to = menu->current_item;
        } else {
            menu->needs_render = true;
        }
        k_sem_give(&menu->render_sem);
    }
    k_mutex_unlock(&menu->state_mutex);
}

int menu_input_event(struct menu_t *menu, menu_input_event_t *event)
{
    if (!menu || !event) {
        return -EINVAL;
    }
    
    menu_process_input(menu, event);
    return 0;
}


static void menu_input_key_cb(struct input_event *evt, void *user_data)
{
    struct menu_t *menu = user_data;
    menu_input_event_t ev = {0};

    switch(evt->code)
    {
        case INPUT_KEY_ENTER:
            ev.type = INPUT_TYPE_KEY1;
            ev.pressed = evt->value;
            break;
        case INPUT_KEY_ESC:
            ev.type = INPUT_TYPE_KEY2;
            ev.pressed = evt->value;
            break;
        case INPUT_KEY_UP:
            ev.type = INPUT_TYPE_KEY3;
            ev.pressed = ev.value;
            break;
        case INPUT_KEY_DOWN:
            ev.type = INPUT_TYPE_KEY4;
            ev.pressed = ev.value;
            break;
        case INPUT_KEY_LEFT:
            ev.type = INPUT_TYPE_KEY5;
            ev.pressed = ev.value;
            break;
        case INPUT_KEY_RIGHT:
            ev.type = INPUT_TYPE_KEY6;
            ev.pressed = ev.value;
            break;
    }
    ev.dev = evt->dev;
    menu_input_event(menu, &ev);
}

static struct menu_t local_menu = {0};

static void menu_state_qdec_cb(const struct device *dev,
					 const struct sensor_trigger *trigger)
{
	const int32_t QDEC_THRESHOLD = 10;
	struct sensor_value qdec_val;
    int val;
	struct menu_t *menu = CONTAINER_OF(trigger, struct menu_t, trigger);
    menu_input_event_t ev = {
        .type = INPUT_TYPE_QDEC,
        .dev = dev,
    };

    if (!menu->disable_qdec) {
        if (sensor_sample_fetch(dev) == 0) {
        if (sensor_channel_get(dev, SENSOR_CHAN_ROTATION, &qdec_val) == 0) {
                LOG_DBG("v1:%d v2:%d", qdec_val.val1, qdec_val.val2);
                val = qdec_val.val1 - menu->qdec_value;

                if (val > QDEC_THRESHOLD) {
                    ev.value = 1;
                    menu_process_input(menu, &ev);
                    menu->qdec_value = qdec_val.val1;
                } else if (val < -QDEC_THRESHOLD) {
                    ev.value = -1;
                    menu_process_input(menu, &ev);
                    menu->qdec_value = qdec_val.val1;
                }
            }
        }
    }

}

static void label_refresh_work_handler(struct k_work *work)
{
    struct menu_t *menu = CONTAINER_OF(work, struct menu_t, label_refresh_work);
    struct menu_group_t *group;
    struct menu_item_t *item;
    char new_label_buf[32];

    k_mutex_lock(&menu->state_mutex, K_FOREVER);

    group = menu->groups;
    while (group) {
        if (group->visible) {
            item = group->items;
            while (item) {
                if (item->visible && item->type == MENU_ITEM_TYPE_LABEL && item->label_cb) {
                    item->label_cb(item, new_label_buf, sizeof(new_label_buf));
                    if (strcmp(new_label_buf, item->label.rendered_label_str) != 0) {
                        /*
                         * Data has changed. Queue an update request for the UI thread.
                         * DO NOT call rendering functions from this worker thread.
                         * The value in the message (0) is ignored for LABEL types.
                         */
                        menu_item_queue_update(item, 0);
                    }
                }
                item = item->group_next;
            }
        }
        group = group->next;
    }

    k_mutex_unlock(&menu->state_mutex);
}

static void label_refresh_timer_cb(struct k_timer *timer)
{
    struct menu_t *menu = CONTAINER_OF(timer, struct menu_t, label_refresh_timer);
    k_work_submit(&menu->label_refresh_work);
}

static void menu_state_machine_func(void *v1, void *v2, void *v3)
{
    struct menu_t *menu = (struct menu_t *)v1;
    const char *current_str;

    if (!menu) {
        return;
    }
    
    k_mutex_lock(&menu->state_mutex, K_FOREVER);
    menu->state = MENU_STATE_IDLE;

    if (!menu->item) {
        while (!menu->item) {
            k_mutex_unlock(&menu->state_mutex);
            k_msleep(100);
            k_mutex_lock(&menu->state_mutex, K_FOREVER);
        }
    }

    struct menu_item_t *first_item = menu->item;
    while (first_item) {
        bool is_non_navigable = first_item->style & MENU_STYLE_NON_NAVIGABLE;
        bool is_hidden = !first_item->visible;
        bool in_inactive_group = first_item->group && !first_item->group->always_visible && first_item->group->bind_item != NULL;

        if (!is_non_navigable && !is_hidden && !in_inactive_group) {
            break;
        }
        first_item = first_item->next;
    }
    menu->current_item = first_item;
    k_mutex_unlock(&menu->state_mutex);

    menu_update_group_visibility(menu);
    menu->needs_render = true;
    k_sem_give(&menu->render_sem);

    k_timer_start(&menu->label_refresh_timer, K_MSEC(500), K_MSEC(500));
    
    menu->adc2_dev = DEVICE_DT_GET(DT_ALIAS(adc2));

    menu->qdec_value = 0;
    menu->key1_pressed = false;
    menu->key2_pressed = false;
    menu->adc2_value = 0;

    struct k_poll_event events[2];
    k_poll_event_init(&events[0], K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &menu->render_sem);
    k_poll_event_init(&events[1], K_POLL_TYPE_MSGQ_DATA_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &menu->update_msgq);

    while (1) {
        int rc = k_poll(events, 2, K_MSEC(100));

        if (rc == 0) {
            if (events[0].state == K_POLL_STATE_SEM_AVAILABLE) {
                k_sem_take(&menu->render_sem, K_NO_WAIT);
                k_mutex_lock(&menu->state_mutex, K_FOREVER);
                if (menu->item_nav_from) {
                    menu_refresh_item_selection(menu, menu->item_nav_from, menu->item_nav_to);
                    menu->item_nav_from = NULL;
                    menu->item_nav_to = NULL;
                } else if (menu->item_to_refresh) {
                    menu_refresh_single_item(menu, menu->item_to_refresh);
                    menu->item_to_refresh = NULL;
                } else if (menu->group_to_refresh) {
                    menu_refresh_group(menu, menu->group_to_refresh);
                    menu->group_to_refresh = NULL;
                } else if (menu->needs_render) {
                    k_mutex_lock(&menu->pannel_mutex, K_FOREVER);
                    menu_render(menu);
                    k_mutex_unlock(&menu->pannel_mutex);
                    menu->needs_render = false;
                }
                k_mutex_unlock(&menu->state_mutex);
            }

            if (events[1].state == K_POLL_STATE_MSGQ_DATA_AVAILABLE) {
                struct menu_update_msg msg;
                if (k_msgq_get(&menu->update_msgq, &msg, K_NO_WAIT) == 0) {
                    k_mutex_lock(&menu->state_mutex, K_FOREVER);
                    if (msg.item && msg.item != menu->editing_item) {
                        if (msg.item->type == MENU_ITEM_TYPE_INPUT) {
                            msg.item->input.value = msg.value;
                        }
                        menu_refresh_single_item(menu, msg.item);
                    }
                    k_mutex_unlock(&menu->state_mutex);
                }
            }
        } else if (rc == -EAGAIN) {
            k_mutex_lock(&menu->state_mutex, K_FOREVER);
            if (menu->editing_item) {
                switch(menu->editing_item->type) {
                    case MENU_ITEM_TYPE_INPUT:
                        if (!menu->editing_item->input.user_adjusted) {
                            menu->editing_item->input.editing_value = menu->editing_item->input.live_value;
                        }
                        char editing_value_buf[16];
                        snprintf(editing_value_buf, sizeof(editing_value_buf), "%d", menu->editing_item->input.editing_value);
                        if (strcmp(editing_value_buf, menu->editing_item->input.rendered_value_str) != 0) {
                            menu_refresh_single_item_fast(menu->editing_item, true);
                        }
                        break;
                    case MENU_ITEM_TYPE_SWITCH:
                        if (menu->editing_item->switch_ctrl.editing_is_on) {
                            current_str = menu->editing_item->switch_ctrl.text_on ? menu->editing_item->switch_ctrl.text_on : "ON";
                        } else {
                            current_str = menu->editing_item->switch_ctrl.text_off ? menu->editing_item->switch_ctrl.text_off : "OFF";
                        }

                        if (strcmp(current_str, menu->editing_item->switch_ctrl.rendered_value_str) != 0) {
                            menu_refresh_single_item_fast(menu->editing_item, true);
                        }
                        break;
                    default:
                        break;
                }
            } else {
                // This block is now only for non-editing items.
                // The editing item refresh is handled in the block above.
                // We no longer need to refresh non-editing INPUT items based on live value changes.
            }
            k_mutex_unlock(&menu->state_mutex);
        }
        events[0].state = K_POLL_STATE_NOT_READY;
        events[1].state = K_POLL_STATE_NOT_READY;
    }
}

void menu_set_current_item(struct menu_t *menu, struct menu_item_t *item)
{
    if (menu) {
        k_mutex_lock(&menu->state_mutex, K_FOREVER);
        if (menu->current_item != item) {
            menu->current_item = item;
            menu->needs_render = true;
            k_sem_give(&menu->render_sem);
        }
        k_mutex_unlock(&menu->state_mutex);
    }
}

struct menu_item_t *menu_get_current_item(struct menu_t *menu)
{
    struct menu_item_t *item = NULL;
    if (menu) {
        k_mutex_lock(&menu->state_mutex, K_FOREVER);
        item = menu->current_item;
        k_mutex_unlock(&menu->state_mutex);
    }
    return item;
}

INPUT_CALLBACK_DEFINE_NAMED(DEVICE_DT_GET(DT_NODELABEL(buttons)), menu_input_key_cb, &local_menu, key);

static char g_update_msgq_buffer[MENU_UPDATE_MSGQ_MAX_MSGS * sizeof(struct menu_update_msg)];

struct menu_t *menu_create(const struct device *render_dev)
{
    struct menu_t *menu = &local_menu;
        
    menu->pannel = pannel_create(render_dev);
    if (!menu->pannel)
    {
        k_free(menu);
        return NULL;
    }

    k_sem_init(&menu->render_sem, 0, 1);
    k_mutex_init(&menu->pannel_mutex);
    k_mutex_init(&menu->state_mutex);
    k_msgq_init(&menu->update_msgq, g_update_msgq_buffer, sizeof(struct menu_update_msg), MENU_UPDATE_MSGQ_MAX_MSGS);
    
    k_timer_init(&menu->label_refresh_timer, label_refresh_timer_cb, NULL);
    k_work_init(&menu->label_refresh_work, label_refresh_work_handler);

    menu->group_stack_top = -1;
    menu->group_to_refresh = NULL;
    menu->item_nav_from = NULL;
    menu->item_nav_to = NULL;
    menu->item_to_refresh = NULL;

    menu->tid = k_thread_create(&menu->thread,
            menu->stack,
            MENU_STACK_SIZE,
            menu_state_machine_func,
            menu, NULL, NULL, 5, 0, K_FOREVER);

    return menu;
}

void menu_item_queue_update(struct menu_item_t *item, int32_t value)
{
    if (!item || !item->menu) {
        return;
    }

    struct menu_update_msg msg = {
        .item = item,
        .value = value,
    };

    if (k_msgq_put(&item->menu->update_msgq, &msg, K_NO_WAIT) != 0) {
        LOG_WRN("Menu update queue full!");
    }
}

int menu_sensor_bind(struct menu_t *menu, const struct device *dev)
{
    if (!device_is_ready(dev))
    {
        return -ENODEV;
    }

    menu->qdec_dev = dev;

    menu->trigger.chan = SENSOR_CHAN_ROTATION;
    menu->trigger.type = SENSOR_TRIG_DATA_READY;

    sensor_trigger_set(menu->qdec_dev, &menu->trigger, menu_state_qdec_cb);

    return 0;
}

void menu_disable_qdec(struct menu_t *menu, bool disable)
{
    menu->disable_qdec = disable;

    if (disable) {
        sensor_trigger_set(menu->qdec_dev, &menu->trigger, NULL);
    } else {
        sensor_trigger_set(menu->qdec_dev, &menu->trigger, menu_state_qdec_cb);
    }
}

int menu_item_add(struct menu_t *menu, struct menu_item_t *item, uint8_t parent)
{
    if (!menu || !item) {
        return -EINVAL;
    }

    if (item->name[0] == '\0') {
        return -EINVAL;
    }

    if (menu->item && find_menu_item_by_id(menu->item, item->id)) {
        return -EEXIST;
    }

    item->parent = NULL;
    item->next = NULL;
    item->prev = NULL;
    item->items = NULL;
    item->group_next = NULL;
    item->group_prev = NULL;
    item->visible = true;

    if (!menu->item) {
        menu->item = item;
        return 0;
    }

    struct menu_item_t *parent_item = NULL;
    if (parent == 0) {
        parent_item = NULL;
    } else {
        struct menu_item_t *current = menu->item;
        parent_item = find_menu_item_by_id(current, parent);
        if (!parent_item) {
            return -ENOENT;
        }
    }

    item->parent = parent_item;

    if (parent_item) {
        if (!parent_item->items) {
            parent_item->items = item;
        } else {
            struct menu_item_t *child = parent_item->items;
            while (child->next) {
                child = child->next;
            }
            child->next = item;
            item->prev = child; 
        }
    } else {
        struct menu_item_t *current = menu->item;
        while (current->next) {
            current = current->next;
        }
        current->next = item;
        item->prev = current;
    }

    return 0;
}

static struct menu_item_t *find_menu_item_by_id(struct menu_item_t *root, uint8_t id)
{
    if (!root) {
        return NULL;
    }

    struct menu_item_t *current = root;

    while (current) {
        if (current->id == id) {
            return current;
        }
        
        if (current->items) {
            current = current->items;
        }

        else if (current->next) {
            current = current->next;
        }
        else {

            while (current && !current->next) {
                current = current->parent;
            }

            if (current) {
                current = current->next;
            }
        }
    }
    
    return NULL;
}

void menu_render_start(struct menu_t *menu)
{
    if (!menu || !menu->tid)
    {
        return;
    }

    k_thread_start(menu->tid);
}

struct menu_group_t *menu_group_create(struct menu_t *menu, const char *title, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color, uint32_t align, uint32_t item_text_align)
{
    struct menu_group_t *group = k_malloc(sizeof(struct menu_group_t));
    if (!group) {
        return NULL;
    }

    strncpy(group->title, title, sizeof(group->title) - 1);
    group->title[sizeof(group->title) - 1] = '\0';
    group->x = x;
    group->y = y;
    group->width = width;
    group->height = height;
    group->color = color;
    group->items = NULL;
    group->next = NULL;
    group->visible = true;
    group->bind_item = NULL;
    group->always_visible = false;
    group->align = align;
    group->item_text_align = item_text_align;
    group->menu = menu;

    if (!menu->groups) {
        menu->groups = group;
    } else {
        struct menu_group_t *current = menu->groups;
        while (current->next) {
            current = current->next;
        }
        current->next = group;
    }

    return group;
}

int menu_group_add_item(struct menu_group_t *group, struct menu_item_t *item)
{
    if (!group || !item) {
        return -EINVAL;
    }

    item->group = group;
    item->menu = group->menu;

    if (group->menu && item->type != MENU_ITEM_TYPE_LABEL) {
        menu_item_add(group->menu, item, 0);
    }

    if (!group->items) {
        group->items = item;
        item->group_prev = NULL;
    } else {
        struct menu_item_t *current = group->items;
        while (current->group_next) {
            current = current->group_next;
        }
        current->group_next = item;
        item->group_prev = current;
    }
    item->group_next = NULL;

    return 0;
}

void menu_item_set_visible(struct menu_item_t *item, bool visible)
{
    if (item) {
        item->visible = visible;
    }
}

void menu_group_set_visible(struct menu_group_t *group, bool visible)
{
    if (group) {
        group->visible = visible;
    }
}

void menu_group_bind_item(struct menu_group_t *group, struct menu_item_t *item)
{
    if (group) {
        group->bind_item = item;
    }
}

void menu_group_set_always_visible(struct menu_group_t *group, bool always_visible)
{
    if (group) {
        group->always_visible = always_visible;
    }
}

static void _menu_update_group_visibility_nolock(struct menu_t *menu)
{
    struct menu_group_t *group = menu->groups;
    struct menu_group_t *active_group = NULL;

    if (menu->group_stack_top > -1) {
        active_group = menu->group_stack[menu->group_stack_top];
    }

    while (group) {
        if (group->always_visible) {
            group->visible = true;
        } else if (active_group) {
            group->visible = (group == active_group);
        } else {
            group->visible = (group->bind_item == NULL);
        }
        group = group->next;
    }
}

static void menu_update_group_visibility(struct menu_t *menu)
{
    if (!menu) {
        return;
    }

    k_mutex_lock(&menu->state_mutex, K_FOREVER);
    _menu_update_group_visibility_nolock(menu);
    k_mutex_unlock(&menu->state_mutex);
}

static struct menu_group_t *find_group_by_bind_item(struct menu_t *menu, struct menu_item_t *item)
{
    if (!menu || !item) {
        return NULL;
    }

    struct menu_group_t *group = menu->groups;
    while (group) {
        if (group->bind_item == item) {
            return group;
        }
        group = group->next;
    }

    return NULL;
}

void menu_group_set_align(struct menu_group_t *group, uint32_t align)
{
    if (group) {
        group->align = align;
    }
}

void menu_group_set_item_text_align(struct menu_group_t *group, uint32_t align)
{
    if (group) {
        group->item_text_align = align;
    }
}

void menu_set_main_group(struct menu_t *menu, struct menu_group_t *group)
{
    if (menu) {
        menu->main_group = group;
    }
}

static void menu_refresh_item_selection(struct menu_t *menu, struct menu_item_t *last_item, struct menu_item_t *current_item)
{
    if (!menu || !last_item || !current_item || !last_item->group || last_item->group != current_item->group) {
        return;
    }

    struct menu_group_t *group = current_item->group;

    k_mutex_lock(&menu->pannel_mutex, K_FOREVER);

    uint16_t item_x, last_y, current_y, item_w;
    menu_get_item_layout(group, last_item, &item_x, &last_y, &item_w);
    menu_get_item_layout(group, current_item, &item_x, &current_y, &item_w);

    menu_render_item(menu, last_item, item_x, last_y, false, item_w);
    menu_render_item(menu, current_item, item_x, current_y, true, item_w);

    k_mutex_unlock(&menu->pannel_mutex);
}

static void menu_render_item_value_only(struct menu_item_t *item)
{
	if (!item || !item->menu || !item->group || !item->group->visible) {
		return;
	}

	struct menu_t *menu = item->menu;

	k_mutex_lock(&menu->pannel_mutex, K_FOREVER);

	uint16_t item_x, item_y, item_w;
	menu_get_item_layout(item->group, item, &item_x, &item_y, &item_w);

	bool selected = (item == menu->current_item);
	menu_render_item(menu, item, item_x, item_y, selected, item_w);

	k_mutex_unlock(&menu->pannel_mutex);
}

static void menu_refresh_single_item(struct menu_t *menu, struct menu_item_t *item)
{
    if (!menu || !item || !item->group || !item->group->visible) {
        return;
    }

    k_mutex_lock(&menu->pannel_mutex, K_FOREVER);

    uint16_t item_x, item_y, item_w;
    menu_get_item_layout(item->group, item, &item_x, &item_y, &item_w);

    bool selected = (item == menu->current_item);
    menu_render_item(menu, item, item_x, item_y, selected, item_w);

    k_mutex_unlock(&menu->pannel_mutex);
}

static void menu_refresh_single_item_fast(struct menu_item_t *item, bool selected)
{
    if (!item || !item->menu || !item->group || !item->group->visible) {
        return;
    }

    if (item->group->item_text_align == 0 && (item->type == MENU_ITEM_TYPE_SWITCH || item->type == MENU_ITEM_TYPE_INPUT)) {
        struct menu_t *menu = item->menu;
        uint16_t item_x, item_y, item_w;
        char new_value_buf[16];
        const char *old_value_str;

        if (item->type == MENU_ITEM_TYPE_SWITCH) {
            bool is_on = (menu->editing_item == item) ? item->switch_ctrl.editing_is_on : item->switch_ctrl.is_on;
            const char * text = is_on ? (item->switch_ctrl.text_on ? item->switch_ctrl.text_on : "ON")
                                     : (item->switch_ctrl.text_off ? item->switch_ctrl.text_off : "OFF");
            strncpy(new_value_buf, text, sizeof(new_value_buf) - 1);
            old_value_str = item->switch_ctrl.rendered_value_str;
        } else { // INPUT
            int32_t value_to_display = (menu->editing_item == item) ? item->input.editing_value : item->input.value;
            snprintf(new_value_buf, sizeof(new_value_buf), "%d", value_to_display);
            old_value_str = item->input.rendered_value_str;
        }

        if (strlen(new_value_buf) != strlen(old_value_str)) {
            goto full_refresh;
        }

        menu_get_item_layout(item->group, item, &item_x, &item_y, &item_w);

        uint16_t text_y = item_y + 2;
        uint16_t value_x = item_x;
        if (!(item->style & MENU_STYLE_VALUE_ONLY)) {
            value_x += (strlen((const char *)item->name) + 1) * CONFIG_FONT_WIDTH; // +1 for ':'
        }

        uint16_t bg_color = selected ? COLOR_WHITE : COLOR_BLACK;
        uint16_t text_color = selected ? COLOR_BLACK : COLOR_WHITE;

        k_mutex_lock(&menu->pannel_mutex, K_FOREVER);

        uint16_t old_value_width = strlen(old_value_str) * CONFIG_FONT_WIDTH;
        if (old_value_width > 0) {
             pannel_render_rect(menu->pannel, value_x, text_y, old_value_width, CONFIG_FONT_HEIGHT, bg_color, true);
        }

        pannel_render_txt(menu->pannel, (uint8_t *)new_value_buf, value_x, text_y, text_color);

        k_mutex_unlock(&menu->pannel_mutex);
        return; 
    }

full_refresh:
    k_mutex_lock(&item->menu->pannel_mutex, K_FOREVER);
    uint16_t item_x, item_y, item_w;
    menu_get_item_layout(item->group, item, &item_x, &item_y, &item_w);
    menu_render_item(item->menu, item, item_x, item_y, selected, item_w);
    k_mutex_unlock(&item->menu->pannel_mutex);
}


static void menu_get_item_layout(struct menu_group_t *group, struct menu_item_t *item_to_find, uint16_t *out_x, uint16_t *out_y, uint16_t *out_w)
{
    if (!group || !item_to_find || !out_x || !out_y || !out_w) {
        return;
    }

    uint16_t total_height = 0;
    struct menu_item_t *item = group->items;
    int visible_items = 0;
    while (item) {
        if (item->visible) {
            visible_items++;
        }
        item = item->group_next;
    }
    total_height = visible_items * (CONFIG_FONT_HEIGHT + 5);

    uint16_t start_x = group->x + 5;
    uint16_t start_y = group->y + 5;
    if (group->align & MENU_ALIGN_V_CENTER) {
        start_y = group->y + (group->height - total_height) / 2;
    }

    item = group->items;
    uint16_t current_y = start_y;
    while (item) {
        if (item->visible) {
            if (item == item_to_find) {
                *out_x = start_x;
                *out_y = current_y;
                *out_w = group->width - 10;
                return;
            }
            current_y += CONFIG_FONT_HEIGHT + 5;
        }
        item = item->group_next;
    }
}

void menu_item_refresh(struct menu_item_t *item)
{
	if (!item || !item->menu || !item->group || !item->group->visible) {
		return;
	}

	struct menu_t *menu = item->menu;
	struct menu_group_t *group = item->group;

	k_mutex_lock(&menu->pannel_mutex, K_FOREVER);

	pannel_render_rect(menu->pannel, group->x + 1, group->y + 4, group->width - 2, group->height - 5, COLOR_BLACK, true);

	uint16_t max_item_width = 0;
	uint16_t total_height = 0;
	struct menu_item_t *current_item_in_loop = group->items;
	while (current_item_in_loop) {
		if (current_item_in_loop->visible) {
			size_t name_len = strlen((const char *)current_item_in_loop->name);
			uint16_t current_item_width = CONFIG_FONT_WIDTH * name_len;
			if (current_item_in_loop->type == MENU_ITEM_TYPE_INPUT) {
				char value_buf[16] = {0};
				int32_t value_to_display = (menu->editing_item == current_item_in_loop) ? current_item_in_loop->input.editing_value : current_item_in_loop->input.value;
				snprintf(value_buf, sizeof(value_buf), "%d", value_to_display);
				current_item_width += 5 + strlen(value_buf) * CONFIG_FONT_WIDTH;
            } else if (current_item_in_loop->type == MENU_ITEM_TYPE_SWITCH) {
                current_item_width += 5 + strlen("OFF") * CONFIG_FONT_WIDTH;
            } else if (current_item_in_loop->type == MENU_ITEM_TYPE_LABEL && current_item_in_loop->label_cb) {
    char label_buf[32] = {0};
    current_item_in_loop->label_cb(current_item_in_loop, label_buf, sizeof(label_buf));
    current_item_width += 1 + strlen(label_buf) * CONFIG_FONT_WIDTH;
			}
			if (current_item_width > max_item_width) {
				max_item_width = current_item_width;
			}
			total_height += CONFIG_FONT_HEIGHT + 5;
		}
		current_item_in_loop = current_item_in_loop->group_next;
	}

	uint16_t start_x = group->x + 5;
	uint16_t start_y = group->y + 5;
	if (group->align & MENU_ALIGN_V_CENTER) {
		start_y = group->y + (group->height - total_height) / 2;
	}
	if (group->item_text_align & MENU_STYLE_CENTER) {
		start_x = group->x + (group->width - max_item_width) / 2;
	} else if (group->item_text_align & MENU_STYLE_RIGHT) {
		start_x = group->x + group->width - max_item_width - 5;
	}

	current_item_in_loop = group->items;
	uint16_t current_y = start_y;
	while (current_item_in_loop) {
		if (current_item_in_loop->visible) {
			bool selected = (current_item_in_loop == menu->current_item);
			menu_render_item(menu, current_item_in_loop, start_x, current_y, selected, max_item_width);
			current_y += CONFIG_FONT_HEIGHT + 5;
		}
		current_item_in_loop = current_item_in_loop->group_next;
	}

	k_mutex_unlock(&menu->pannel_mutex);
}

bool menu_item_is_editing(struct menu_item_t *item)
{
    if (!item || !item->menu) {
        return false;
    }
    k_mutex_lock(&item->menu->state_mutex, K_FOREVER);
    bool is_editing = (item->menu->editing_item == item);
    k_mutex_unlock(&item->menu->state_mutex);
    return is_editing;
}

void menu_driver_bind(struct menu_t *menu, void *driver)
{
    menu->driver = driver;
}

void *menu_driver_get(struct menu_t *menu)
{
    return menu->driver;
}

void menu_driver_start(struct menu_t *menu, void (*start)(void *, bool), bool en)
{
    start(menu->driver, en);
}