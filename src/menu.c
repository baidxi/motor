#include <menu/menu.h>

#include <stdio.h>

struct device;

#include <zephyr/logging/log.h>
#include <motor/mc.h>

LOG_MODULE_DECLARE(menu, CONFIG_LOG_DEFAULT_LEVEL);

extern void motor_ctrl(void *ctrl, bool enable);
extern void menu_driver_start(struct menu_t *menu, void (*start)(void *, bool), bool en);
static int menu_item_label_vbus_cb(struct menu_item_t *item, char *buf, size_t len);
static void startup_checkbox_cb(struct menu_item_t *item, bool is_on);
static void startup_confirm_cb(struct menu_item_t *item, bool confirmed);

static struct menu_item_t setup_item = {
    .name = "Setup",
    .id = 1,
    .style = MENU_STYLE_HIGHLIGHT | MENU_STYLE_BORDER,
    .visible = true,
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

static struct menu_item_t voltage_item = {
    .name = "vbus",
    .id = 10,
    .style = MENU_STYLE_NORMAL,
    .type = MENU_ITEM_TYPE_LABEL,
    .label_cb = menu_item_label_vbus_cb,
    .visible = true,
};

static struct menu_item_t startup_item = {
    .name = "Start",
    .id = 2,
    .style = MENU_STYLE_NORMAL | MENU_STYLE_VALUE_ONLY,
    .type = MENU_ITEM_TYPE_CHECKBOX,
    .checkbox = {
        .is_on = false,
        .cb = startup_checkbox_cb,
        .text_on = "Stop",
        .text_off = "Start",
    },
    .visible = true,
};

static void startup_confirm_cb(struct menu_item_t *item, bool confirmed)
{
    if (confirmed) {
        LOG_INF("User confirmed startup. Disabling QDEC and starting motor.");
        menu_disable_qdec(item->menu, true);
        mc_motor_ready(menu_driver_get(item->menu), true);
    } else {
        LOG_INF("User canceled startup.");
    }
}

static void startup_checkbox_cb(struct menu_item_t *item, bool is_on)
{
    if (is_on) {
        menu_dialog_show(item->menu, DIALOG_STYLE_CONFIRM, "Confirm", startup_confirm_cb, "Start motor?");
    } else {
        LOG_INF("Motor stopping.");
        mc_motor_ready(menu_driver_get(item->menu), false);
        menu_disable_qdec(item->menu, false); /* Re-enable encoder */
    }
}

static int menu_item_label_vbus_cb(struct menu_item_t *item, char *buf, size_t len)
{
    double voltage = mc_vbus_get(menu_driver_get(item->menu));

    snprintf(buf, len, "%.2fV", voltage);
    return 0;
}

int menu_init(const struct device *dev, struct menu_t **out)
{
    struct menu_t *menu;
    struct menu_group_t *status_group;
    struct menu_group_t *main_group;
    struct menu_group_t *setup_group;
    extern struct menu_item_t setup_motor_item;

    menu = menu_create(dev);

    if (!menu)
    {
        return -1;
    }

    status_group = menu_group_create(menu, "Status", 60, 5, 100, 75, COLOR_BLUE, MENU_LAYOUT_VERTICAL | MENU_ALIGN_V_CENTER, MENU_STYLE_LEFT);
    
    menu_group_add_item(status_group, &voltage_item);

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