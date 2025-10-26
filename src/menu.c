#include <menu/menu.h>

#include <stdio.h>

struct device;

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(menu, CONFIG_LOG_DEFAULT_LEVEL);

static void startup_checkbox_cb(struct menu_item_t *item, bool is_on)
{
    LOG_INF("Startup checkbox is now %s", is_on ? "ON" : "OFF");
    menu_disable_qdec(item->menu, is_on);
}

static struct menu_item_t setup_item = {
    .name = "Setup",
    .id = 1,
    .style = MENU_STYLE_HIGHLIGHT | MENU_STYLE_BORDER,
    .visible = true,
};

static struct menu_item_t startup_item = {
    .name = "Run",
    .id = 2,
    .style = MENU_STYLE_NORMAL | MENU_STYLE_VALUE_ONLY | MENU_SET_COLOR(COLOR_GREEN),
    .type = MENU_ITEM_TYPE_CHECKBOX,
    .checkbox = {
        .is_on = false,
        .cb = startup_checkbox_cb,
        .text_on = "Stop",
        .text_off = "Start",
    },
};

static struct menu_item_t setup_display_item = {
    .name = "Display",
    .id = 4,
    .style = MENU_STYLE_NORMAL,
};

static struct menu_item_t setup_power_item = {
    .name = "Power",
    .id = 5,
    .style = MENU_STYLE_NORMAL,
};

// Placeholder for an 8x8 'off' image (a red square)
static const uint16_t img_off_data[128] = {
    0x0000, 0xE71D, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 
    0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDF5D, 0x0000, 
    0xE73C, 0xDEFC, 0xDEFD, 0x0000, 0x0000, 0xDEFD, 0xDEFC, 0xDEFC, 
    0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFD, 
    0xDEFC, 0xDEFD, 0x0000, 0x0000, 0x0000, 0x0000, 0xDF1C, 0xDEFC, 
    0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 
    0xDEFC, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xDEFC, 
    0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 
    0xDEFC, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xDEFC, 
    0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 
    0xDEFC, 0xDEFD, 0x0000, 0x0000, 0x0000, 0x0000, 0xDEFD, 0xDEFC, 
    0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 
    0xE71E, 0xDEFC, 0xDF1D, 0x0000, 0x0000, 0xDEFD, 0xDEFC, 0xDEFC, 
    0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFD, 
    0x0000, 0xDEFD, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 
    0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xDEFC, 0xE73C, 0x0000
};

// Placeholder for an 8x8 'on' image (a green square)
static const uint16_t img_on_data[128] = {
    0x0000, 0x147F, 0x149F, 0x1C7F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 
    0x1C9F, 0x1C9F, 0x1C9F, 0x1C7F, 0x147F, 0x149F, 0x145F, 0x0000, 
    0x0C7F, 0x147F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 
    0x1C9F, 0x149F, 0x1C7F, 0x0000, 0x0000, 0x147F, 0x1C9F, 0x14DF, 
    0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 
    0x147F, 0x149F, 0x0000, 0x0000, 0x0000, 0x0000, 0x149F, 0x1C9F, 
    0x1C7F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 
    0x149F, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x147F, 
    0x147F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 
    0x149F, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1C9F, 
    0x149F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 
    0x149F, 0x147F, 0x0000, 0x0000, 0x0000, 0x0000, 0x1C9F, 0x1C9F, 
    0x0CBF, 0x149F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 
    0x1C9F, 0x149F, 0x149F, 0x0000, 0x0000, 0x147F, 0x1C7F, 0x14DF, 
    0x0000, 0x149F, 0x147F, 0x1C7F, 0x1C9F, 0x1C9F, 0x1C9F, 0x1C9F, 
    0x1C9F, 0x1C9F, 0x149F, 0x1C7F, 0x147F, 0x1C9F, 0x0C7F, 0x0000
};

static void test_checkbox_cb(struct menu_item_t *item, bool is_on)
{
    LOG_INF("Test checkbox is now %s", is_on ? "ON" : "OFF");
}

static struct menu_item_t test_image_checkbox_item = {
    .name = "Img",
    .id = 10, // a new unique ID
    .style = MENU_STYLE_CHECKBOX_IMG | MENU_STYLE_CENTER,
    .type = MENU_ITEM_TYPE_CHECKBOX,
    .checkbox = {
        .is_on = false,
        .cb = test_checkbox_cb,
        .text_on = (const char *)img_on_data,
        .text_off = (const char *)img_off_data,
        .img_width = 16,
        .img_height = 8,
    },
};

int menu_init(const struct device *dev, struct menu_t **out)
{
    struct menu_t *menu;
    struct menu_group_t *status_group;
    struct menu_group_t *main_group;
    struct menu_group_t *setup_group;
    extern struct menu_item_t setup_motor_item;
    extern struct menu_item_t status_vbus_item;

    menu = menu_create(dev);

    if (!menu)
    {
        return -1;
    }

    status_group = menu_group_create(menu, "Status", 60, 5, 100, 75, COLOR_BLUE, MENU_LAYOUT_VERTICAL | MENU_ALIGN_V_CENTER, MENU_STYLE_LEFT);
    
    menu_group_add_item(status_group, &status_vbus_item);

    main_group = menu_group_create(menu, "main", 0, 5, 55, 75, COLOR_WHITE, MENU_LAYOUT_VERTICAL | MENU_ALIGN_V_CENTER, MENU_STYLE_CENTER);

    menu_group_add_item(main_group, &setup_item);
    menu_group_add_item(main_group, &startup_item);
    menu_group_add_item(main_group, &test_image_checkbox_item);

    setup_group = menu_group_create(menu, "Setup", 40, 5, 100, 75, COLOR_MAGENTA, MENU_LAYOUT_VERTICAL | MENU_ALIGN_V_CENTER, MENU_STYLE_CENTER);

    menu_group_add_item(setup_group, &setup_motor_item);
    menu_group_add_item(setup_group, &setup_display_item);
    menu_group_add_item(setup_group, &setup_power_item);
    menu_group_bind_item(setup_group, &setup_item);

    menu_set_main_group(menu, main_group);

    if (out)
        *out = menu;

    return 0;
}