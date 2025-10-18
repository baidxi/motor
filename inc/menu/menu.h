#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct menu_t;
struct menu_item_t;
struct device;

typedef void (*menu_item_callback_t)(struct menu_item_t *item, uint8_t id);

// 菜单样式定义
#define MENU_STYLE_NORMAL        0x00000001  // 普通样式
#define MENU_STYLE_HIGHLIGHT      0x00000002  // 高亮样式
#define MENU_STYLE_SELECTED       0x00000004  // 选中样式
#define MENU_STYLE_DISABLED       0x00000008  // 禁用样式
#define MENU_STYLE_BORDER         0x00000010  // 边框样式
#define MENU_STYLE_ROUND_CORNER   0x00000020  // 圆角样式
#define MENU_STYLE_CENTER         0x00000040  // 居中对齐
#define MENU_STYLE_RIGHT          0x00000080  // 右对齐

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

// 输入设备类型
typedef enum {
    INPUT_TYPE_NONE = 0,
    INPUT_TYPE_QDEC,      // 正交编码器
    INPUT_TYPE_KEY1,      // 按键1
    INPUT_TYPE_KEY2,      // 按键2
    INPUT_TYPE_ADC2_CH12, // ADC2通道12
} input_type_t;

// 输入事件
typedef struct {
    input_type_t type;
    int32_t value;        // 对于QDEC是增量值，对于按键是按下/释放状态，对于ADC是原始值
    bool pressed;         // 对于按键是否按下
} menu_input_event_t;

// 菜单状态
typedef enum {
    MENU_STATE_IDLE = 0,
    MENU_STATE_NAVIGATING,
    MENU_STATE_SELECTED,
    MENU_STATE_PROCESSING,
} menu_state_t;

struct menu_item_t {
    uint8_t name[32];
    struct menu_item_t *parent;
    struct menu_item_t *items;
    struct menu_item_t *next;
    struct menu_item_t *prev;
    uint8_t id;
    menu_item_callback_t cb;
    uint32_t style;
    void *priv_data;
};

struct menu_t *menu_create(const struct device *render_dev);
int menu_item_add(struct menu_t *menu, struct menu_item_t *item, uint8_t parent);
int menu_input_event(struct menu_t *menu, menu_input_event_t *event);
void menu_set_current_item(struct menu_t *menu, struct menu_item_t *item);
struct menu_item_t *menu_get_current_item(struct menu_t *menu);
void menu_render_start(struct menu_t *menu);