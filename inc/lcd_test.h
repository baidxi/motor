#pragma once

#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>

/**
 * @brief 初始化LCD测试
 * 
 * @param dev 显示设备指针
 * @return int 0表示成功，负值表示失败
 */
int lcd_test_init(const struct device *dev);

/**
 * @brief 运行LCD测试
 * 
 * @param dev 显示设备指针
 * @return int 0表示成功，负值表示失败
 */
int lcd_test_run(const struct device *dev);

/**
 * @brief 清除LCD屏幕
 * 
 * @param dev 显示设备指针
 * @param color 清除屏幕的颜色
 * @return int 0表示成功，负值表示失败
 */
int lcd_test_clear(const struct device *dev, uint32_t color);

/**
 * @brief 在LCD上绘制一个点
 * 
 * @param dev 显示设备指针
 * @param x X坐标
 * @param y Y坐标
 * @param color 点的颜色
 * @return int 0表示成功，负值表示失败
 */
int lcd_test_draw_pixel(const struct device *dev, uint16_t x, uint16_t y, uint32_t color);

/**
 * @brief 在LCD上绘制一条线
 * 
 * @param dev 显示设备指针
 * @param x0 起点X坐标
 * @param y0 起点Y坐标
 * @param x1 终点X坐标
 * @param y1 终点Y坐标
 * @param color 线的颜色
 * @return int 0表示成功，负值表示失败
 */
int lcd_test_draw_line(const struct device *dev, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t color);

/**
 * @brief 在LCD上绘制一个矩形
 * 
 * @param dev 显示设备指针
 * @param x 左上角X坐标
 * @param y 左上角Y坐标
 * @param width 矩形宽度
 * @param height 矩形高度
 * @param color 矩形的颜色
 * @param fill 是否填充矩形
 * @return int 0表示成功，负值表示失败
 */
int lcd_test_draw_rectangle(const struct device *dev, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color, bool fill);

/**
 * @brief 在LCD上绘制一个圆
 * 
 * @param dev 显示设备指针
 * @param x0 圆心X坐标
 * @param y0 圆心Y坐标
 * @param radius 圆的半径
 * @param color 圆的颜色
 * @param fill 是否填充圆
 * @return int 0表示成功，负值表示失败
 */
int lcd_test_draw_circle(const struct device *dev, uint16_t x0, uint16_t y0, uint16_t radius, uint32_t color, bool fill);

/**
 * @brief 在LCD上显示文本
 * 
 * @param dev 显示设备指针
 * @param x 文本起始X坐标
 * @param y 文本起始Y坐标
 * @param text 要显示的文本
 * @param color 文本的颜色
 * @param bg_color 文本背景颜色
 * @return int 0表示成功，负值表示失败
 */
int lcd_test_draw_text(const struct device *dev, uint16_t x, uint16_t y, const char *text, uint32_t color, uint32_t bg_color);

/**
 * @brief 测试LCD的颜色显示
 * 
 * @param dev 显示设备指针
 * @return int 0表示成功，负值表示失败
 */
int lcd_test_colors(const struct device *dev);

/**
 * @brief 测试LCD的图形显示
 * 
 * @param dev 显示设备指针
 * @return int 0表示成功，负值表示失败
 */
int lcd_test_graphics(const struct device *dev);

/**
 * @brief 测试LCD的文本显示
 * 
 * @param dev 显示设备指针
 * @return int 0表示成功，负值表示失败
 */
int lcd_test_text(const struct device *dev);