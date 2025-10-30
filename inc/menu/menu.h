#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>

struct menu_t;
struct menu_item_t;
struct device;
struct menu_group_t;

typedef enum {
    INPUT_TYPE_NONE = 0,
    INPUT_TYPE_QDEC,      // 正交编码器
    INPUT_TYPE_KEY1,      // 按键1
    INPUT_TYPE_KEY2,      // 按键2
    INPUT_TYPE_KEY3,
    INPUT_TYPE_KEY4,
    INPUT_TYPE_KEY5,
    INPUT_TYPE_KEY6,
} input_type_t;

typedef struct {
    input_type_t type;
    int32_t value;        // 对于QDEC是增量值，对于按键是按下/释放状态，对于ADC是原始值
    bool pressed;         // 对于按键是否按下
    const struct device *dev;
} menu_input_event_t;

typedef void (*menu_item_callback_t)(struct menu_item_t *item, uint8_t id);
typedef int (*menu_item_label_cb)(struct menu_item_t *item, char *buf, size_t len);

typedef bool (*menu_item_input_cb_t)(struct menu_item_t *item, menu_input_event_t *event);
typedef void (*menu_item_switch_cb_t)(struct menu_item_t *item, bool is_on);
typedef void (*menu_item_checkbox_cb_t)(struct menu_item_t *item, bool is_on);

typedef enum {
   DIALOG_STYLE_INFO,
   DIALOG_STYLE_ERR,
   DIALOG_STYLE_WARN,
   DIALOG_STYLE_CONFIRM,
} menu_dialog_style_t;

typedef void (*menu_dialog_confirm_cb_t)(struct menu_item_t *item, bool confirmed);

// 菜单样式定义
#define MENU_STYLE_NORMAL        0x00000001  // 普通样式
#define MENU_STYLE_HIGHLIGHT      0x00000002  // 高亮样式
#define MENU_STYLE_SELECTED       0x00000004  // 选中样式
#define MENU_STYLE_DISABLED       0x00000008  // 禁用样式
#define MENU_STYLE_BORDER         0x00000010  // 边框样式
#define MENU_STYLE_ROUND_CORNER   0x00000020  // 圆角样式
#define MENU_STYLE_CENTER         0x00000040  // 居中对齐
#define MENU_STYLE_RIGHT          0x00000080  // 右对齐
#define MENU_STYLE_LEFT           0x00000100  // 左对齐
#define MENU_STYLE_VALUE_LABEL    0x00000400  // 带数值的标签样式
#define MENU_STYLE_NON_NAVIGABLE  0x00000800  // 不可导航
#define MENU_STYLE_VALUE_ONLY     0x00001000  // 只渲染值
#define MENU_STYLE_CUSTOM_COLOR   0x00002000  // 使用自定义颜色
#define MENU_STYLE_CHECKBOX_IMG   0x00004000  // checkbox使用图像
#define MENU_STYLE_COLOR_SHIFT    16
#define MENU_SET_COLOR(color)     (((uint32_t)(color) << MENU_STYLE_COLOR_SHIFT) | MENU_STYLE_CUSTOM_COLOR)

// 颜色定义
#define COLOR_BLACK               0x0000
#define COLOR_WHITE               0xFFFF
#define COLOR_RED                 0xF800
#define COLOR_GREEN               0x07E0
#define COLOR_BLUE                0x001F
#define COLOR_YELLOW              0xFFE0
#define COLOR_CYAN                0x07FF
#define COLOR_MAGENTA             0xF81F
#define COLOR_GRAY                0x8410
#define COLOR_DARK_GRAY           0x4208
#define COLOR_LIGHT_GRAY          0xC618

// 菜单状态
typedef enum {
    MENU_STATE_IDLE = 0,
    MENU_STATE_NAVIGATING,
    MENU_STATE_SELECTED,
    MENU_STATE_PROCESSING,
} menu_state_t;

// 对齐方式定义
#define MENU_ALIGN_LEFT           0x00000001  // 水平左对齐
#define MENU_ALIGN_RIGHT          0x00000002  // 水平右对齐
#define MENU_ALIGN_H_CENTER       0x00000004  // 水平居中
#define MENU_ALIGN_TOP            0x00000008  // 垂直顶部对齐
#define MENU_ALIGN_BOTTOM         0x00000010  // 垂直底部对齐
#define MENU_ALIGN_V_CENTER       0x00000020  // 垂直居中
#define MENU_ALIGN_H_FILL         0x00000040  // 水平填充
#define MENU_ALIGN_V_FILL         0x00000080  // 垂直填充
#define MENU_LAYOUT_HORIZONTAL    0x00000100  // 水平布局
#define MENU_LAYOUT_VERTICAL      0x00000200  // 垂直布局

typedef enum {
    MENU_ITEM_TYPE_NORMAL,
    MENU_ITEM_TYPE_INPUT,
    MENU_ITEM_TYPE_SWITCH,
    MENU_ITEM_TYPE_LIST,
    MENU_ITEM_TYPE_CHECKBOX,
    MENU_ITEM_TYPE_INPUT_MIN_MAX,
    MENU_ITEM_TYPE_LABEL,
   MENU_ITEM_TYPE_DIALOG,
} menu_item_type_t;

#define ADC_FILTER_WINDOW_SIZE 10

struct menu_item_t {
    uint8_t name[32];
    struct menu_item_t *parent;
    struct menu_item_t *items;
    struct menu_item_t *next;
    struct menu_item_t *prev;
    struct menu_item_t *group_next;
    struct menu_item_t *group_prev;
    uint8_t id;
    menu_item_callback_t cb;
    menu_item_label_cb label_cb;
    uint32_t style;
    void *priv_data;
    struct menu_group_t *group;
    bool visible;
    struct menu_t *menu;
    menu_item_type_t type;
    union {
        struct item_input_t {
            int32_t value;
            int32_t live_value;
            int32_t min;
            int32_t max;
            int32_t step;
            int32_t editing_value;
            bool user_adjusted;
            const struct device *dev;
            menu_item_input_cb_t cb;
            menu_item_label_cb value_get_str_cb;
            struct k_work work;
            uint16_t *values;
            size_t values_count;
            char rendered_value_str[16];
            uint16_t filter_window[ADC_FILTER_WINDOW_SIZE];
            uint8_t filter_index;
        } input;
        struct item_switch_t {
            bool is_on;
            bool editing_is_on;
            menu_item_switch_cb_t cb;
            char rendered_value_str[16];
            const char *text_on;
            const char *text_off;
        } switch_ctrl;
        struct item_list_t {
            const char **options;
            uint8_t num_options;
            uint8_t selected_index;
            uint8_t editing_index;
            void (*cb)(struct menu_item_t *item, uint8_t selected_index);
            uint32_t layout;
            const char *title;
            char rendered_value_str[16];
        } list;
        struct item_checkbox_t {
            bool is_on;
            menu_item_checkbox_cb_t cb;
            char rendered_value_str[16];
            const char *text_on;
            const char *text_off;
            uint16_t img_width;
            uint16_t img_height;
        } checkbox;
        struct item_label_t {
            char rendered_label_str[32];
        } label;
    };
   struct item_dialog_t {
       char title[32];
       char msg[128];
       menu_dialog_style_t style;
       menu_dialog_confirm_cb_t cb;
   } dialog;
    struct item_input_min_max_t {
        int32_t min_value;
        int32_t max_value;
        int32_t editing_min_value;
        int32_t editing_max_value;
        int32_t min_limit;
        int32_t max_limit;
        int32_t step;
        void (*cb)(struct menu_item_t *item, int32_t min, int32_t max);
        uint8_t editing_target; // 0 for min, 1 for max, 2 for OK, 3 for Cancel
        char rendered_value_str[32];
    } input_min_max;
};

struct menu_group_t {
    uint8_t title[32];
    uint16_t x, y, width, height;
    uint16_t color;
    struct menu_item_t *items;
    struct menu_group_t *next;
    bool visible;
    struct menu_item_t *bind_item;
    bool always_visible;
    uint32_t align;
    uint32_t item_text_align;
    struct menu_t *menu;
};

struct menu_update_msg {
    struct menu_item_t *item;
    int32_t value;
};

#define MENU_UPDATE_MSGQ_MAX_MSGS 10

struct menu_t;

extern struct menu_t *menu_create(const struct device *render_dev);
extern void menu_item_queue_update(struct menu_item_t *item, int32_t value);
extern int menu_sensor_bind(struct menu_t *menu, const struct device *dev);
extern int menu_item_add(struct menu_t *menu, struct menu_item_t *item, uint8_t parent);
extern int menu_input_event(struct menu_t *menu, menu_input_event_t *event);
extern void menu_set_current_item(struct menu_t *menu, struct menu_item_t *item);
extern struct menu_item_t *menu_get_current_item(struct menu_t *menu);
extern void menu_render_start(struct menu_t *menu);
extern struct menu_group_t *menu_group_create(struct menu_t *menu, const char *title, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color, uint32_t align, uint32_t item_text_align);
extern int menu_group_add_item(struct menu_group_t *group, struct menu_item_t *item);
extern void menu_item_set_visible(struct menu_item_t *item, bool visible);
extern void menu_group_set_visible(struct menu_group_t *group, bool visible);
extern void menu_group_bind_item(struct menu_group_t *group, struct menu_item_t *item);
extern void menu_group_set_always_visible(struct menu_group_t *group, bool always_visible);
extern void menu_group_set_align(struct menu_group_t *group, uint32_t align);
extern void menu_group_set_item_text_align(struct menu_group_t *group, uint32_t align);
extern void menu_set_main_group(struct menu_t *menu, struct menu_group_t *group);
extern void menu_item_refresh(struct menu_item_t *item);
extern bool menu_item_is_editing(struct menu_item_t *item);
extern void menu_disable_qdec(struct menu_t *menu, bool disable);
extern void menu_driver_bind(struct menu_t *menu, void *driver);
extern int menu_init(const struct device *dev, struct menu_t **out);
extern void menu_driver_start(struct menu_t *menu, void (*start)(void *, bool), bool en);
extern void *menu_driver_get(struct menu_t *menu);
extern int menu_dialog_show(struct menu_t *menu, menu_dialog_style_t style, const char *title, menu_dialog_confirm_cb_t cb, const char *fmt, ...);