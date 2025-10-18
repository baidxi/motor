#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/display.h>
#include <zephyr/input/input.h>
#include <zephyr/drivers/sensor.h>

#include <menu.h>
#include <pannel.h>

#define MENU_STACK_SIZE 4096
// 辅助函数：根据ID查找菜单项
static struct menu_item_t *find_menu_item_by_id(struct menu_item_t *root, uint8_t id);

struct menu_t {
    struct menu_item_t *item;
    struct pannel_t *pannel;
    k_tid_t tid;
    struct k_thread thread;
    K_KERNEL_STACK_MEMBER(stack, MENU_STACK_SIZE);
    
    // 菜单状态
    menu_state_t state;
    struct menu_item_t *current_item;
    struct menu_item_t *selected_item;
    
    // 输入设备
    const struct device *qdec_dev;
    // const struct device *key1_dev;
    // const struct device *key2_dev;
    const struct device *adc2_dev;
    
    // 输入状态
    int32_t qdec_value;
    bool key1_pressed;
    bool key2_pressed;
    int16_t adc2_value;
    bool needs_render;
    struct sensor_trigger trigger;
};

static void menu_render_item(struct menu_t *menu, struct menu_item_t *item,
                           uint16_t x, uint16_t y, bool selected)
{
    if (!menu || !item || !menu->pannel) {
        return;
    }
    
    // 根据样式确定颜色
    uint16_t text_color = COLOR_WHITE;
    uint16_t bg_color = COLOR_BLACK;
    
    if (selected) {
        text_color = COLOR_BLACK;
        bg_color = COLOR_WHITE;
    } else if (item->style & MENU_STYLE_HIGHLIGHT) {
        text_color = COLOR_YELLOW;
    } else if (item->style & MENU_STYLE_DISABLED) {
        text_color = COLOR_GRAY;
    }
    
    // 绘制背景
    if (selected || (item->style & MENU_STYLE_BORDER)) {
        // 确保name是有效的字符串
        size_t name_len = 0;
        if (item->name[0] != '\0') {
            name_len = strlen((const char *)item->name);
        }
        pannel_render_rect(menu->pannel, x, y,
                          CONFIG_FONT_WIDTH * name_len + 4,
                          CONFIG_FONT_HEIGHT + 4, bg_color, true);
    }
    
    // 绘制文本
    uint16_t text_x = x + 2;
    uint16_t text_y = y + 2;
    
    // 确保name是有效的字符串
    size_t name_len = 0;
    if (item->name[0] != '\0') {
        name_len = strlen((const char *)item->name);
    }
    
    if (item->style & MENU_STYLE_CENTER) {
        // 居中对齐
        text_x = x + (CONFIG_FONT_WIDTH * name_len) / 2;
    } else if (item->style & MENU_STYLE_RIGHT) {
        // 右对齐
        text_x = x + CONFIG_FONT_WIDTH * name_len;
    }
    
    // 确保name是有效的字符串
    if (item->name[0] != '\0') {
        pannel_render_txt(menu->pannel, (uint8_t *)item->name, text_x, text_y, text_color);
    }
}

// 辅助函数：渲染菜单
static void menu_render(struct menu_t *menu)
{
    struct display_capabilities *caps;
    
    if (!menu || !menu->pannel) {
        return;
    }

    pannel_get_capabilities(menu->pannel, &caps);
    
    pannel_render_clear(menu->pannel, COLOR_BLACK);
    // 渲染当前菜单
    if (menu->current_item) {
        uint16_t y = 10;
        uint16_t x = 10;
        
        // 如果有父菜单，显示返回选项
        if (menu->current_item->parent) {
            pannel_render_txt(menu->pannel, (uint8_t *)"..", x, y, COLOR_WHITE);
            y += CONFIG_FONT_HEIGHT + 5;
        }
        
        // 渲染同级菜单项
        struct menu_item_t *item = menu->current_item->parent ?
                                  menu->current_item->parent->items :
                                  menu->item;
        
        while (item) {
            bool selected = (item == menu->current_item);
            menu_render_item(menu, item, x, y, selected);
            y += CONFIG_FONT_HEIGHT + 5;
            
            // 检查是否超出屏幕
            if (y + CONFIG_FONT_HEIGHT > caps->y_resolution) {
                break;
            }
            
            item = item->next;
        }
    }
}

// 辅助函数：处理输入事件
static void menu_process_input(struct menu_t *menu, menu_input_event_t *event)
{
    if (!menu || !event) {
        return;
    }
    
    struct menu_item_t *last_item = menu->current_item;
    
    switch (event->type) {
        case INPUT_TYPE_QDEC:
            // QDEC用于上下导航
            if (event->value > 0) {
                // 向下移动
                if (menu->current_item && menu->current_item->next) {
                    menu->current_item = menu->current_item->next;
                }
            } else if (event->value < 0) {
                // 向上移动
                if (menu->current_item && menu->current_item->prev) {
                    menu->current_item = menu->current_item->prev;
                }
            }
            break;
            
        case INPUT_TYPE_KEY1:
            // KEY1用于选择/进入
            if (event->pressed) {
                if (menu->current_item) {
                    if (menu->current_item->items) {
                        // 有子菜单，进入子菜单
                        menu->current_item = menu->current_item->items;
                    } else {
                        // 没有子菜单，执行回调
                        if (menu->current_item->cb) {
                            menu->current_item->cb(menu->current_item, menu->current_item->id);
                        }
                    }
                }
            }
            break;
            
        case INPUT_TYPE_KEY2:
            // KEY2用于返回
            if (event->pressed) {
                if (menu->current_item && menu->current_item->parent) {
                    menu->current_item = menu->current_item->parent;
                }
            }
            break;
            
        case INPUT_TYPE_ADC2_CH12:
            // ADC2通道12可用于调节参数
            // 这里可以根据需要实现特定的功能
            break;
            
        default:
            break;
    }
    
    if (last_item != menu->current_item) {
        menu->needs_render = true;
    }
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
        case INPUT_KEY_1:
            ev.type = INPUT_TYPE_KEY1;
            ev.pressed = evt->value;
            break;
        case INPUT_KEY_2:
            ev.type = INPUT_TYPE_KEY2;
            ev.pressed = evt->value;
            break;
    }

    menu_input_event(menu, &ev);
}

static struct menu_t local_menu = {0};

static void menu_state_qdec_cb(const struct device *dev,
					 const struct sensor_trigger *trigger)
{
	static int32_t last_qdec_value = 0;
	struct sensor_value qdec_val;
	struct menu_t *menu = CONTAINER_OF(trigger, struct menu_t, trigger);

	if (sensor_sample_fetch(dev) == 0) {
		if (sensor_channel_get(dev, SENSOR_CHAN_ROTATION, &qdec_val) == 0) {
			if (qdec_val.val1 != last_qdec_value) {
				menu_input_event_t event = {
					.type = INPUT_TYPE_QDEC,
					.value = qdec_val.val1 > last_qdec_value ? 1 : -1,
				};
				menu_process_input(menu, &event);
				last_qdec_value = qdec_val.val1;
			}
		}
	}
}

static void menu_state_machine_func(void *v1, void *v2, void *v3)
{
    struct menu_t *menu = (struct menu_t *)v1;

    if (!menu) {
        return;
    }
    
    // 初始化当前项为根菜单项
    menu->current_item = menu->item;
    menu->state = MENU_STATE_IDLE;
    
    // 确保当前菜单项有效
    if (!menu->current_item && menu->item) {
        menu->current_item = menu->item;
    }
    
    // 如果菜单项为空，等待菜单项被添加
    if (!menu->current_item) {
        // 等待菜单项被添加
        while (!menu->item) {
            k_msleep(100);
        }
        menu->current_item = menu->item;
    }
    
    menu->needs_render = true;
    
    // 初始化输入设备
    menu->qdec_dev = DEVICE_DT_GET(DT_ALIAS(qdec0));
    menu->adc2_dev = device_get_binding("adc_2");

    menu->qdec_value = 0;
    menu->key1_pressed = false;
    menu->key2_pressed = false;
    menu->adc2_value = 0;

    menu->trigger.chan = SENSOR_CHAN_ROTATION;
    menu->trigger.type = SENSOR_TRIG_DATA_READY;

    sensor_trigger_set(menu->qdec_dev, &menu->trigger, menu_state_qdec_cb);

    while (1) {
        // 渲染菜单
        if (menu->needs_render) {
            menu_render(menu);
            menu->needs_render = false;
        }
        
        // 等待一段时间
        k_msleep(50); // 缩短等待时间以提高响应速度
    }
}

// 设置当前菜单项
void menu_set_current_item(struct menu_t *menu, struct menu_item_t *item)
{
    if (menu && menu->current_item != item) {
        menu->current_item = item;
        menu->needs_render = true;
    }
}

// 获取当前菜单项
struct menu_item_t *menu_get_current_item(struct menu_t *menu)
{
    return menu ? menu->current_item : NULL;
}

INPUT_CALLBACK_DEFINE_NAMED(DEVICE_DT_GET(DT_NODELABEL(buttons)), menu_input_key_cb, &local_menu, key);

struct menu_t *menu_create(const struct device *render_dev)
{
    struct menu_t *menu = &local_menu;
        
    menu->pannel = pannel_create(render_dev);
    if (!menu->pannel)
    {
        k_free(menu);
        return NULL;
    }

    menu->tid = k_thread_create(&menu->thread,
            menu->stack,
            MENU_STACK_SIZE,
            menu_state_machine_func,
            menu, NULL, NULL, 5, 0, K_FOREVER);

    return menu;
}

int menu_item_add(struct menu_t *menu, struct menu_item_t *item, uint8_t parent)
{
    // 参数检查
    if (!menu || !item) {
        return -EINVAL; // 无效参数
    }

    // 检查菜单项名称是否为空
    if (item->name[0] == '\0') {
        return -EINVAL; // 菜单项名称不能为空
    }

    // 检查菜单项ID是否已存在（只有在菜单不为空时才检查）
    if (menu->item && find_menu_item_by_id(menu->item, item->id)) {
        return -EEXIST; // 菜单项ID已存在
    }

    // 初始化新菜单项的指针
    item->parent = NULL;
    item->next = NULL;
    item->prev = NULL;
    item->items = NULL;

    // 如果是第一个菜单项，直接设置为菜单的根项
    if (!menu->item) {
        menu->item = item;
        return 0;
    }

    // 查找父菜单项
    struct menu_item_t *parent_item = NULL;
    if (parent == 0) {
        // 如果parent为0，添加到根级别
        parent_item = NULL;
    } else {
        // 查找指定ID的父菜单项
        struct menu_item_t *current = menu->item;
        parent_item = find_menu_item_by_id(current, parent);
        if (!parent_item) {
            return -ENOENT; // 父菜单项不存在
        }
    }

    // 设置新菜单项的父节点
    item->parent = parent_item;

    if (parent_item) {
        // 添加到父菜单项的子菜单列表
        if (!parent_item->items) {
            // 如果父菜单项还没有子菜单，直接添加
            parent_item->items = item;
        } else {
            // 否则，添加到子菜单列表的末尾
            struct menu_item_t *child = parent_item->items;
            while (child->next) {
                child = child->next;
            }
            child->next = item;
            item->prev = child; // 设置前驱指针
        }
    } else {
        // 添加到根级别
        struct menu_item_t *current = menu->item;
        while (current->next) {
            current = current->next;
        }
        current->next = item;
        item->prev = current; // 设置前驱指针
    }

    return 0;
}

// 辅助函数：根据ID查找菜单项（纯迭代实现，节省内存）
static struct menu_item_t *find_menu_item_by_id(struct menu_item_t *root, uint8_t id)
{
    if (!root) {
        return NULL;
    }

    // 使用纯迭代方式遍历菜单树
    // 从根节点开始
    struct menu_item_t *current = root;
    
    // 使用prev指针回溯，实现深度优先遍历
    while (current) {
        // 检查当前节点
        if (current->id == id) {
            return current;
        }
        
        // 如果有子节点，先处理子节点
        if (current->items) {
            current = current->items;
        }
        // 如果没有子节点但有同级节点，处理同级节点
        else if (current->next) {
            current = current->next;
        }
        // 如果既没有子节点也没有同级节点，回溯到父节点
        else {
            // 回溯到父节点，然后检查父节点的同级节点
            while (current && !current->next) {
                current = current->parent;
            }
            
            // 如果找到了有同级节点的父节点，移动到同级节点
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