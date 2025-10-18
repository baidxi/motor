#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct device;
struct display_capabilities;
struct pannel_t;

struct pannel_t *pannel_create(const struct device *render_dev);
void pannel_render_line(struct pannel_t *pannel, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t color);
void pannel_render_txt(struct pannel_t *pannel, uint8_t *txt, uint16_t x, uint16_t y, uint16_t color);
void pannel_render_rect(struct pannel_t *pannel, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color, bool fill);
void pannel_render_circle(struct pannel_t *pannel, uint16_t x, uint16_t y, uint16_t redius, uint16_t color);
void pannel_render_clear(struct pannel_t *pannel, uint32_t color);
int pannel_get_capabilities(struct pannel_t *pannel, struct display_capabilities **caps);