#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdlib.h>

#include "lcd_test.h"
#include "menu/font_8x8.h"

LOG_MODULE_REGISTER(lcd_test, LOG_LEVEL_INF);

/* LCD屏幕参数 */
#define LCD_WIDTH       160
#define LCD_HEIGHT      80
#define FONT_WIDTH      8
#define FONT_HEIGHT     8

/* 颜色定义（RGB565格式） */
#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_RED       0xF800
#define COLOR_GREEN     0x07E0
#define COLOR_BLUE      0x001F
#define COLOR_YELLOW    0xFFE0
#define COLOR_CYAN      0x07FF
#define COLOR_MAGENTA   0xF81F
#define COLOR_ORANGE    0xFC00

/* 显示设备指针 */
static const struct device *display_dev = NULL;
/* 显示能力结构体 */
static struct display_capabilities caps;
/* 像素格式 */
static uint8_t bytes_per_pixel = 2;

/**
 * @brief 初始化LCD测试
 */
int lcd_test_init(const struct device *dev)
{
    if (!dev) {
        LOG_ERR("Display device is NULL");
        return -1;
    }

    if (!device_is_ready(dev)) {
        LOG_ERR("Display device not ready");
        return -1;
    }

    display_dev = dev;
    
    /* 获取显示能力 */
    display_get_capabilities(display_dev, &caps);
    
    /* 根据像素格式确定每像素字节数 */
    switch (caps.current_pixel_format) {
        case PIXEL_FORMAT_ARGB_8888:
            bytes_per_pixel = 4;
            break;
        case PIXEL_FORMAT_RGB_888:
            bytes_per_pixel = 3;
            break;
        case PIXEL_FORMAT_RGB_565:
        case PIXEL_FORMAT_BGR_565:
            bytes_per_pixel = 2;
            break;
        case PIXEL_FORMAT_L_8:
        case PIXEL_FORMAT_MONO01:
        case PIXEL_FORMAT_MONO10:
            bytes_per_pixel = 1;
            break;
        case PIXEL_FORMAT_AL_88:
            bytes_per_pixel = 2;
            break;
        default:
            LOG_ERR("Unsupported pixel format: %d", caps.current_pixel_format);
            return -1;
    }

    LOG_INF("LCD initialized: %dx%d, pixel format: %d, bytes per pixel: %d",
            caps.x_resolution, caps.y_resolution, caps.current_pixel_format, bytes_per_pixel);

    /* 关闭显示空白 */
    display_blanking_off(display_dev);

    return 0;
}

/**
 * @brief 清除LCD屏幕
 */
int lcd_test_clear(const struct device *dev, uint32_t color)
{
    struct display_buffer_descriptor desc;
    size_t row_buf_size = caps.x_resolution * bytes_per_pixel;
    uint8_t *row_buf = k_malloc(row_buf_size);
    
    if (!row_buf) {
        LOG_ERR("Failed to allocate buffer for clearing screen");
        return -1;
    }

    /* 填充行缓冲区 */
    for (size_t i = 0; i < row_buf_size; i += bytes_per_pixel) {
        if (bytes_per_pixel == 1) {
            row_buf[i] = color & 0xFF;
        } else if (bytes_per_pixel == 2) {
            *(uint16_t *)(row_buf + i) = color & 0xFFFF;
        } else if (bytes_per_pixel == 3) {
            row_buf[i] = (color >> 16) & 0xFF;
            row_buf[i + 1] = (color >> 8) & 0xFF;
            row_buf[i + 2] = color & 0xFF;
        } else if (bytes_per_pixel == 4) {
            *(uint32_t *)(row_buf + i) = color;
        }
    }

    /* 设置显示描述符 */
    desc.buf_size = row_buf_size;
    desc.width = caps.x_resolution;
    desc.height = 1;
    desc.pitch = caps.x_resolution;
    desc.frame_incomplete = false;

    /* 逐行写入显示 */
    for (uint16_t y = 0; y < caps.y_resolution; y++) {
        display_write(dev, 0, y, &desc, row_buf);
    }

    k_free(row_buf);
    return 0;
}

/**
 * @brief 在LCD上绘制一个点
 */
int lcd_test_draw_pixel(const struct device *dev, uint16_t x, uint16_t y, uint32_t color)
{
    struct display_buffer_descriptor desc;
    size_t buf_size = bytes_per_pixel;
    uint8_t buf[4];  // 最大支持4字节每像素
    
    /* 检查坐标是否在屏幕范围内 */
    if (x >= caps.x_resolution || y >= caps.y_resolution) {
        return -1;
    }

    /* 填充像素数据 */
    if (bytes_per_pixel == 1) {
        buf[0] = color & 0xFF;
    } else if (bytes_per_pixel == 2) {
        *(uint16_t *)buf = color & 0xFFFF;
    } else if (bytes_per_pixel == 3) {
        buf[0] = (color >> 16) & 0xFF;
        buf[1] = (color >> 8) & 0xFF;
        buf[2] = color & 0xFF;
    } else if (bytes_per_pixel == 4) {
        *(uint32_t *)buf = color;
    }

    /* 设置显示描述符 */
    desc.buf_size = buf_size;
    desc.width = 1;
    desc.height = 1;
    desc.pitch = 1;
    desc.frame_incomplete = false;

    /* 写入显示 */
    display_write(dev, x, y, &desc, buf);

    return 0;
}

/**
 * @brief 在LCD上绘制一条线（Bresenham算法）
 */
int lcd_test_draw_line(const struct device *dev, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t color)
{
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    int e2;

    while (true) {
        lcd_test_draw_pixel(dev, x0, y0, color);

        if (x0 == x1 && y0 == y1) {
            break;
        }

        e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }

    return 0;
}

/**
 * @brief 在LCD上绘制一个矩形
 */
int lcd_test_draw_rectangle(const struct device *dev, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color, bool fill)
{
    if (fill) {
        /* 填充矩形 */
        struct display_buffer_descriptor desc;
        size_t buf_size = width * bytes_per_pixel;
        uint8_t *buf = k_malloc(buf_size);
        
        if (!buf) {
            LOG_ERR("Failed to allocate buffer for rectangle");
            return -1;
        }

        /* 填充缓冲区 */
        for (size_t i = 0; i < buf_size; i += bytes_per_pixel) {
            if (bytes_per_pixel == 1) {
                buf[i] = color & 0xFF;
            } else if (bytes_per_pixel == 2) {
                *(uint16_t *)(buf + i) = color & 0xFFFF;
            } else if (bytes_per_pixel == 3) {
                buf[i] = (color >> 16) & 0xFF;
                buf[i + 1] = (color >> 8) & 0xFF;
                buf[i + 2] = color & 0xFF;
            } else if (bytes_per_pixel == 4) {
                *(uint32_t *)(buf + i) = color;
            }
        }

        /* 设置显示描述符 */
        desc.buf_size = buf_size;
        desc.width = width;
        desc.height = 1;
        desc.pitch = width;
        desc.frame_incomplete = false;

        /* 逐行写入显示 */
        for (uint16_t i = 0; i < height; i++) {
            display_write(dev, x, y + i, &desc, buf);
        }

        k_free(buf);
    } else {
        /* 只绘制矩形边框 */
        lcd_test_draw_line(dev, x, y, x + width - 1, y, color);  // 上边
        lcd_test_draw_line(dev, x, y + height - 1, x + width - 1, y + height - 1, color);  // 下边
        lcd_test_draw_line(dev, x, y, x, y + height - 1, color);  // 左边
        lcd_test_draw_line(dev, x + width - 1, y, x + width - 1, y + height - 1, color);  // 右边
    }

    return 0;
}

/**
 * @brief 在LCD上绘制一个圆（中点圆算法）
 */
int lcd_test_draw_circle(const struct device *dev, uint16_t x0, uint16_t y0, uint16_t radius, uint32_t color, bool fill)
{
    int x = radius;
    int y = 0;
    int err = 0;

    while (x >= y) {
        if (fill) {
            /* 绘制填充圆 */
            lcd_test_draw_line(dev, x0 - x, y0 + y, x0 + x, y0 + y, color);
            lcd_test_draw_line(dev, x0 - y, y0 + x, x0 + y, y0 + x, color);
            lcd_test_draw_line(dev, x0 - x, y0 - y, x0 + x, y0 - y, color);
            lcd_test_draw_line(dev, x0 - y, y0 - x, x0 + y, y0 - x, color);
        } else {
            /* 只绘制圆边框 */
            lcd_test_draw_pixel(dev, x0 + x, y0 + y, color);
            lcd_test_draw_pixel(dev, x0 + y, y0 + x, color);
            lcd_test_draw_pixel(dev, x0 - y, y0 + x, color);
            lcd_test_draw_pixel(dev, x0 - x, y0 + y, color);
            lcd_test_draw_pixel(dev, x0 - x, y0 - y, color);
            lcd_test_draw_pixel(dev, x0 - y, y0 - x, color);
            lcd_test_draw_pixel(dev, x0 + y, y0 - x, color);
            lcd_test_draw_pixel(dev, x0 + x, y0 - y, color);
        }

        if (err <= 0) {
            y += 1;
            err += 2 * y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2 * x + 1;
        }
    }

    return 0;
}

/**
 * @brief 在LCD上显示文本
 */
int lcd_test_draw_text(const struct device *dev, uint16_t x, uint16_t y, const char *text, uint32_t color, uint32_t bg_color)
{
    size_t len = strlen(text);
    struct display_buffer_descriptor desc;
    size_t row_buf_size = len * FONT_WIDTH * bytes_per_pixel;
    uint8_t *row_buf = k_malloc(row_buf_size);
    
    if (!row_buf) {
        LOG_ERR("Failed to allocate buffer for text");
        return -1;
    }

    /* 逐行绘制文本 */
    for (int row = 0; row < FONT_HEIGHT; row++) {
        /* 清空行缓冲区为背景色 */
        for (size_t i = 0; i < row_buf_size; i += bytes_per_pixel) {
            if (bytes_per_pixel == 1) {
                row_buf[i] = bg_color & 0xFF;
            } else if (bytes_per_pixel == 2) {
                *(uint16_t *)(row_buf + i) = bg_color & 0xFFFF;
            } else if (bytes_per_pixel == 3) {
                row_buf[i] = (bg_color >> 16) & 0xFF;
                row_buf[i + 1] = (bg_color >> 8) & 0xFF;
                row_buf[i + 2] = bg_color & 0xFF;
            } else if (bytes_per_pixel == 4) {
                *(uint32_t *)(row_buf + i) = bg_color;
            }
        }

        /* 绘制文本行 */
        for (size_t i = 0; i < len; i++) {
            char c = text[i];
            if (c < ' ' || c > '~') {
                c = ' ';
            }

            const uint8_t *font_data = font_8x8[c - ' '];
            
            for (int col = 0; col < FONT_WIDTH; col++) {
                bool is_pixel_set = (font_data[row] >> (7 - col)) & 1;
                if (is_pixel_set) {
                    size_t offset = (i * FONT_WIDTH + col) * bytes_per_pixel;
                    
                    if (bytes_per_pixel == 1) {
                        row_buf[offset] = color & 0xFF;
                    } else if (bytes_per_pixel == 2) {
                        *(uint16_t *)(row_buf + offset) = color & 0xFFFF;
                    } else if (bytes_per_pixel == 3) {
                        row_buf[offset] = (color >> 16) & 0xFF;
                        row_buf[offset + 1] = (color >> 8) & 0xFF;
                        row_buf[offset + 2] = color & 0xFF;
                    } else if (bytes_per_pixel == 4) {
                        *(uint32_t *)(row_buf + offset) = color;
                    }
                }
            }
        }

        /* 设置显示描述符 */
        desc.buf_size = row_buf_size;
        desc.width = len * FONT_WIDTH;
        desc.height = 1;
        desc.pitch = len * FONT_WIDTH;
        desc.frame_incomplete = false;

        /* 写入显示 */
        display_write(dev, x, y + row, &desc, row_buf);
    }

    k_free(row_buf);
    return 0;
}

/**
 * @brief 测试LCD的颜色显示
 */
int lcd_test_colors(const struct device *dev)
{
    LOG_INF("Testing LCD colors");

    /* 清除屏幕为黑色 */
    lcd_test_clear(dev, COLOR_BLACK);

    /* 绘制不同颜色的矩形，全屏显示 */
    uint16_t rect_width = caps.x_resolution / 4;
    uint16_t rect_height = caps.y_resolution;

    lcd_test_draw_rectangle(dev, 0, 0, rect_width, rect_height, COLOR_RED, true);
    lcd_test_draw_rectangle(dev, rect_width, 0, rect_width, rect_height, COLOR_GREEN, true);
    lcd_test_draw_rectangle(dev, 2 * rect_width, 0, rect_width, rect_height, COLOR_BLUE, true);
    lcd_test_draw_rectangle(dev, 3 * rect_width, 0, rect_width, rect_height, COLOR_YELLOW, true);

    k_sleep(K_MSEC(2000));

    /* 第二组颜色，全屏显示 */
    lcd_test_draw_rectangle(dev, 0, 0, rect_width, rect_height, COLOR_CYAN, true);
    lcd_test_draw_rectangle(dev, rect_width, 0, rect_width, rect_height, COLOR_MAGENTA, true);
    lcd_test_draw_rectangle(dev, 2 * rect_width, 0, rect_width, rect_height, COLOR_ORANGE, true);
    lcd_test_draw_rectangle(dev, 3 * rect_width, 0, rect_width, rect_height, COLOR_WHITE, true);

    k_sleep(K_MSEC(2000));

    return 0;
}

/**
 * @brief 测试LCD的图形显示
 */
int lcd_test_graphics(const struct device *dev)
{
    LOG_INF("Testing LCD graphics");

    /* 清除屏幕为黑色 */
    lcd_test_clear(dev, COLOR_BLACK);

    /* 绘制线条 - 全屏边框 */
    lcd_test_draw_line(dev, 0, 0, caps.x_resolution - 1, 0, COLOR_RED);
    lcd_test_draw_line(dev, caps.x_resolution - 1, 0, caps.x_resolution - 1, caps.y_resolution - 1, COLOR_GREEN);
    lcd_test_draw_line(dev, caps.x_resolution - 1, caps.y_resolution - 1, 0, caps.y_resolution - 1, COLOR_BLUE);
    lcd_test_draw_line(dev, 0, caps.y_resolution - 1, 0, 0, COLOR_YELLOW);

    /* 绘制对角线 */
    lcd_test_draw_line(dev, 0, 0, caps.x_resolution - 1, caps.y_resolution - 1, COLOR_WHITE);
    lcd_test_draw_line(dev, caps.x_resolution - 1, 0, 0, caps.y_resolution - 1, COLOR_CYAN);

    /* 绘制填充色块 - 全屏显示 */
    uint16_t rect_width = caps.x_resolution / 3;
    uint16_t rect_height = caps.y_resolution / 3;
    
    /* 左上角色块 */
    lcd_test_draw_rectangle(dev, 0, 0, rect_width, rect_height, COLOR_MAGENTA, true);
    
    /* 中上角色块 */
    lcd_test_draw_rectangle(dev, rect_width, 0, rect_width, rect_height, COLOR_ORANGE, true);
    
    /* 右上角色块 */
    lcd_test_draw_rectangle(dev, 2 * rect_width, 0, caps.x_resolution - 2 * rect_width, rect_height, COLOR_WHITE, true);
    
    /* 左中角色块 */
    lcd_test_draw_rectangle(dev, 0, rect_height, rect_width, rect_height, COLOR_CYAN, true);
    
    /* 中间色块 */
    lcd_test_draw_rectangle(dev, rect_width, rect_height, rect_width, rect_height, COLOR_RED, true);
    
    /* 右中角色块 */
    lcd_test_draw_rectangle(dev, 2 * rect_width, rect_height, caps.x_resolution - 2 * rect_width, rect_height, COLOR_GREEN, true);
    
    /* 左下角色块 */
    lcd_test_draw_rectangle(dev, 0, 2 * rect_height, rect_width, caps.y_resolution - 2 * rect_height, COLOR_YELLOW, true);
    
    /* 中下角色块 */
    lcd_test_draw_rectangle(dev, rect_width, 2 * rect_height, rect_width, caps.y_resolution - 2 * rect_height, COLOR_BLUE, true);
    
    /* 右下角色块 */
    lcd_test_draw_rectangle(dev, 2 * rect_width, 2 * rect_height, caps.x_resolution - 2 * rect_width, caps.y_resolution - 2 * rect_height, COLOR_RED, true);

    /* 绘制圆 - 全屏显示 */
    /* 左侧圆 */
    lcd_test_draw_circle(dev, caps.x_resolution / 4, caps.y_resolution / 2, caps.y_resolution / 6, COLOR_BLACK, false);
    
    /* 右侧圆 */
    lcd_test_draw_circle(dev, caps.x_resolution * 3 / 4, caps.y_resolution / 2, caps.y_resolution / 6, COLOR_BLACK, false);
    
    /* 底部小圆 */
    lcd_test_draw_circle(dev, caps.x_resolution / 2, caps.y_resolution - 10, 6, COLOR_BLACK, false);

    /* 添加额外的图形元素，确保整个屏幕都有内容 */
    /* 在左下角添加一个小矩形 */
    lcd_test_draw_rectangle(dev, 5, caps.y_resolution - 15, 20, 10, COLOR_WHITE, true);
    
    /* 在右下角添加一个小矩形 */
    lcd_test_draw_rectangle(dev, caps.x_resolution - 25, caps.y_resolution - 15, 20, 10, COLOR_WHITE, true);
    
    /* 在中间底部添加一条横线 */
    lcd_test_draw_line(dev, caps.x_resolution / 4, caps.y_resolution - 5, caps.x_resolution * 3 / 4, caps.y_resolution - 5, COLOR_YELLOW);

    k_sleep(K_MSEC(2000));

    return 0;
}

/**
 * @brief 测试LCD的文本显示
 */
int lcd_test_text(const struct device *dev)
{
    LOG_INF("Testing LCD text");

    /* 清除屏幕为黑色 */
    lcd_test_clear(dev, COLOR_BLACK);

    /* 显示文本 - 全屏显示 */
    lcd_test_draw_text(dev, 10, 10, "LCD Test - Full Screen", COLOR_WHITE, COLOR_BLACK);
    lcd_test_draw_text(dev, 10, 25, "STM32G431RB Motor Control", COLOR_RED, COLOR_BLACK);
    lcd_test_draw_text(dev, 10, 40, "ST7735R 160x80 Display", COLOR_GREEN, COLOR_BLACK);
    lcd_test_draw_text(dev, 10, 55, "Zephyr RTOS", COLOR_BLUE, COLOR_BLACK);
    
    /* 显示更多文本，填满屏幕 */
    lcd_test_draw_text(dev, 10, 70, "RGB565 Color Mode", COLOR_YELLOW, COLOR_BLACK);
    
    /* 在右侧显示文本 */
    lcd_test_draw_text(dev, caps.x_resolution - 120, 10, "Right Side Text", COLOR_CYAN, COLOR_BLACK);
    lcd_test_draw_text(dev, caps.x_resolution - 120, 25, "Testing Display", COLOR_MAGENTA, COLOR_BLACK);
    lcd_test_draw_text(dev, caps.x_resolution - 120, 40, "Full Screen Utilization", COLOR_ORANGE, COLOR_BLACK);
    lcd_test_draw_text(dev, caps.x_resolution - 120, 55, "160x80 Pixels", COLOR_WHITE, COLOR_BLACK);
    lcd_test_draw_text(dev, caps.x_resolution - 120, 70, "SPI Interface", COLOR_RED, COLOR_BLACK);

    k_sleep(K_MSEC(3000));

    return 0;
}

/**
 * @brief 运行LCD测试
 */
int lcd_test_run(const struct device *dev)
{
    int ret;

    LOG_INF("Starting LCD test");

    /* 初始化LCD */
    ret = lcd_test_init(dev);
    if (ret != 0) {
        LOG_ERR("Failed to initialize LCD");
        return ret;
    }

    /* 运行测试 */
    while (1) {
        /* 测试颜色 */
        ret = lcd_test_colors(dev);
        if (ret != 0) {
            LOG_ERR("Color test failed");
            return ret;
        }

        /* 测试图形 */
        ret = lcd_test_graphics(dev);
        if (ret != 0) {
            LOG_ERR("Graphics test failed");
            return ret;
        }

        /* 测试文本 */
        ret = lcd_test_text(dev);
        if (ret != 0) {
            LOG_ERR("Text test failed");
            return ret;
        }
    }

    return 0;
}