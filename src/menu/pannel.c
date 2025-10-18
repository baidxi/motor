#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <zephyr/cache.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#include <pannel.h>

#ifdef CONFIG_FONT_8X8
#include <font_8x8.h>
#endif
#ifdef CONFIG_FONT_16X16
#include <font_16x16.h>
#endif

struct pannel_t {
    const struct device *render_dev;
    uint8_t bytes_per_pixel;
    struct display_capabilities caps;
    uint8_t font_size;
    void *buf;
    uint16_t buf_size;
};

static int draw_circle_point(struct pannel_t *pannel, uint16_t x, uint16_t y, uint32_t color)
{
    struct display_buffer_descriptor desc = {0};
    uint8_t *buf = pannel->buf;

    if (!pannel)
        return -EINVAL;

    if (x >= pannel->caps.x_resolution || y >= pannel->caps.y_resolution)
        return -EINVAL;

    switch(pannel->bytes_per_pixel)
    {
        case 1:
            buf[0] = color & 0xff;
            break;
        case 2:
            *(uint16_t *)buf = color & 0xffff;
            break;
        case 3:
            buf[0] = (color >> 16) & 0xff;
            buf[1] = (color >> 8) & 0xff;
            buf[2] = color & 0xff;
            break;
        default:
            *(uint32_t *)buf = color;

    }

    desc.buf_size = pannel->bytes_per_pixel;
    desc.width = 1;
    desc.height = 1;
    desc.pitch = 1;
    desc.frame_incomplete = false;

    display_write(pannel->render_dev, x, y, &desc, buf);

    return 0;
}

void pannel_render_line(struct pannel_t *pannel, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t color)
{
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    int e2;

    while(true)
    {
        draw_circle_point(pannel, x0, y0, color);

        if (x0 == x1 && y0 == y1)
            break;

        e2 = 2 * err;
        if (e2 > -dy)
        {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void pannel_render_txt(struct pannel_t *pannel, uint8_t *txt, uint16_t x, uint16_t y, uint16_t color)
{
    if (!pannel || !txt) {
        return;
    }

    uint16_t current_x = x;
    char c;
    const uint8_t *font_data;

    while (*txt) {
        c = *txt;
        if (c < ' ' || c > '~') {
            c = ' ';
        }

        if (pannel->font_size == 16) {
            #ifdef CONFIG_FONT_16X16
            font_data = font_16x16[c - ' '];
            for (int i = 0; i < 16; i++) {
                uint8_t byte1 = font_data[i * 2];
                uint8_t byte2 = font_data[i * 2 + 1];
                for (int j = 0; j < 8; j++) {
                    if ((byte1 >> (7 - j)) & 1) {
                        draw_circle_point(pannel, current_x + j, y + i, color);
                    }
                }
                for (int j = 0; j < 8; j++) {
                    if ((byte2 >> (7 - j)) & 1) {
                        draw_circle_point(pannel, current_x + 8 + j, y + i, color);
                    }
                }
            }
            current_x += 16;
            #endif
        } else { // Default to 8x8
            #ifdef CONFIG_FONT_8X8
            font_data = font_8x8[c - ' '];
            for (int i = 0; i < 8; i++) {
                for (int j = 0; j < 8; j++) {
                    if ((font_data[i] >> (7 - j)) & 1) {
                        draw_circle_point(pannel, current_x + j, y + i, color);
                    }
                }
            }
            current_x += 8;
            #endif
        }
        txt++;
    }
}

void pannel_render_rect(struct pannel_t *pannel, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color, bool fill)
{
    if (!pannel) {
        return;
    }

    if (fill) {
        struct display_buffer_descriptor desc;
        size_t buf_size = w * pannel->bytes_per_pixel;
        uint8_t *buf = pannel->buf;

        for (size_t i = 0; i < buf_size; i += pannel->bytes_per_pixel) {
            switch (pannel->bytes_per_pixel) {
                case 1:
                    buf[i] = color & 0xFF;
                    break;
                case 2:
                    *(uint16_t *)(buf + i) = color & 0xFFFF;
                    break;
                case 3:
                    buf[i] = (color >> 16) & 0xFF;
                    buf[i + 1] = (color >> 8) & 0xFF;
                    buf[i + 2] = color & 0xFF;
                    break;
                case 4:
                    *(uint32_t *)(buf + i) = color;
                    break;
            }
        }

        desc.buf_size = buf_size;
        desc.width = w;
        desc.height = 1;
        desc.pitch = w;
        desc.frame_incomplete = false;

        for (uint16_t i = 0; i < h; i++) {
            sys_cache_data_flush_range(buf, buf_size);
            display_write(pannel->render_dev, x, y + i, &desc, buf);
        }
    } else {
        pannel_render_line(pannel, x, y, x + w - 1, y, color);
        pannel_render_line(pannel, x, y + h - 1, x + w - 1, y + h - 1, color);
        pannel_render_line(pannel, x, y, x, y + h - 1, color);
        pannel_render_line(pannel, x + w - 1, y, x + w - 1, y + h - 1, color);
    }
}

void pannel_render_circle(struct pannel_t *pannel, uint16_t x, uint16_t y, uint16_t radius, uint16_t color)
{
    if (!pannel) {
        return;
    }

    int x_pos = radius;
    int y_pos = 0;
    int err = 0;

    while (x_pos >= y_pos) {
        draw_circle_point(pannel, x + x_pos, y + y_pos, color);
        draw_circle_point(pannel, x + y_pos, y + x_pos, color);
        draw_circle_point(pannel, x - y_pos, y + x_pos, color);
        draw_circle_point(pannel, x - x_pos, y + y_pos, color);
        draw_circle_point(pannel, x - x_pos, y - y_pos, color);
        draw_circle_point(pannel, x - y_pos, y - x_pos, color);
        draw_circle_point(pannel, x + y_pos, y - x_pos, color);
        draw_circle_point(pannel, x + x_pos, y - y_pos, color);

        if (err <= 0) {
            y_pos += 1;
            err += 2 * y_pos + 1;
        }
        if (err > 0) {
            x_pos -= 1;
            err -= 2 * x_pos + 1;
        }
    }
}

struct pannel_t *pannel_create(const struct device *render_dev)
{
    struct pannel_t *pannel;
    uint16_t buf_size;

    if (!device_is_ready(render_dev))
    {
        return NULL;
    }

    pannel = k_malloc(sizeof(*pannel));

    if (pannel)
    {
        pannel->render_dev = render_dev;

#ifndef CONFIG_FONT_SIZE
#define CONFIG_FONT_SIZE 16
#endif
        pannel->font_size = CONFIG_FONT_SIZE;

        display_get_capabilities(render_dev, &pannel->caps);

        switch(pannel->caps.current_pixel_format)
        {
            case PIXEL_FORMAT_ARGB_8888:
                pannel->bytes_per_pixel = 4;
                break;
            case PIXEL_FORMAT_RGB_888:
                pannel->bytes_per_pixel = 3;
                break;
            case PIXEL_FORMAT_BGR_565:
            case PIXEL_FORMAT_RGB_565:
            case PIXEL_FORMAT_AL_88:
                pannel->bytes_per_pixel = 2;
                break;
            case PIXEL_FORMAT_L_8:
            case PIXEL_FORMAT_MONO01:
            case PIXEL_FORMAT_MONO10:
                pannel->bytes_per_pixel = 1;
                break;
            default:
                k_free(pannel);
                return NULL;

        }

        buf_size = pannel->caps.x_resolution * pannel->bytes_per_pixel;
        pannel->buf_size = buf_size;
        pannel->buf = k_malloc(buf_size);
        if (!pannel->buf)
        {
            k_free(pannel);
            return NULL;
        }
    }

    return pannel;
}

int pannel_get_capabilities(struct pannel_t *pannel, struct display_capabilities **caps)
{
    if (!pannel || !pannel->render_dev || !caps) {
        return -EINVAL;
    }
    
    *caps = &pannel->caps;
    
    return 0;
}

void pannel_render_clear(struct pannel_t *pannel, uint32_t color)
{
    size_t row_buf_size = pannel->buf_size;
    uint8_t *row_buf = pannel->buf;
    struct display_buffer_descriptor desc = {0};

    for (size_t i = 0; i < row_buf_size; i += pannel->bytes_per_pixel)
    {
        switch(pannel->bytes_per_pixel)
        {
            case 1:
                row_buf[i] = color & 0xff;
                break;
            case 2:
                *(uint16_t *)(row_buf + i) = color & 0xffff;
                break;
            case 3:
                row_buf[i] = (color >> 16) & 0xff;
                row_buf[i + 1] = (color >> 8) & 0xff;
                row_buf[i + 2] = color & 0xff;
                break;
            default:
                *(uint32_t *)(row_buf + i) = color;
        }
    }

    desc.buf_size = row_buf_size;
    desc.width = pannel->caps.x_resolution;
    desc.height = 1;
    desc.pitch = pannel->caps.x_resolution;
    desc.frame_incomplete = false;

    for (uint16_t y = 0; y < pannel->caps.y_resolution; y++) {
        display_write(pannel->render_dev, 0, y, &desc, row_buf);
    }
}