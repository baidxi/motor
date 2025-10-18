#pragma once

#include <stdint.h>

// This font table contains 95 printable ASCII characters from 0x20 (' ') to 0x7E ('~').
// To get the font data for a character 'c', use font_16x16[c - 0x20].
extern const uint8_t font_16x16[95][32];