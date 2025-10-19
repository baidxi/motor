#include <menu/menu.h>

#include <stdio.h>

struct device;

// static struct menu_item_t status_item = {
//     .name = "Status",
//     .id = 0,
//     .style = MENU_STYLE_HIGHLIGHT | MENU_STYLE_BORDER,
//     .visible = true,
// };

static struct menu_item_t setup_item = {
    .name = "Setup",
    .id = 1,
    .style = MENU_STYLE_HIGHLIGHT | MENU_STYLE_BORDER,
    .visible = true,
};

static struct menu_item_t startup_item = {
    .name = "Start",
    .id = 2,
    .style = MENU_STYLE_HIGHLIGHT | MENU_STYLE_BORDER,
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