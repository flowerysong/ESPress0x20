/* This file is part of ESPress0x20 and distributed under the terms of the
 * MIT license. See COPYING.
 */

#include "SpriteButton.h"

SpriteButton::SpriteButton(void) {
    fb = nullptr;
    text[ 0 ] = '\0';
}

bool
SpriteButton::needsInit(void) {
    return (!fb);
}

void
SpriteButton::initButton(TFT_eSprite *sprite, int16_t corner_x,
        int16_t corner_y, uint16_t w, uint16_t h, uint16_t outline,
        uint16_t fill, String label, const GFXfont *font,
        void (*callback)(void)) {
    fb = sprite;
    cb = callback;
    x = corner_x;
    y = corner_y;
    width = w;
    height = h;
    color_outline = outline;
    color_fill = fill;
    text = label;
    text_font = font;
}

void
SpriteButton::drawButton(void) {
    uint16_t outline;
    uint16_t fill;
    uint8_t  radius;
    uint8_t  old_datum;

    radius = min(width, height) / 4;

    if (isPressed()) {
        // Button is pressed, invert colours
        outline = color_fill;
        fill = color_outline;
    } else {
        outline = color_outline;
        fill = color_fill;
    }

    fb->fillRoundRect(x, y, width, height, radius, fill);
    fb->drawRoundRect(x, y, width, height, radius, outline);

    fb->setFreeFont(text_font);
    fb->setTextSize(1);
    fb->setTextColor(outline, fill);
    old_datum = fb->getTextDatum();
    fb->setTextDatum(TC_DATUM);
    fb->drawString(text, x + (width / 2), y + height - fb->fontHeight(1), 1);
    fb->setTextDatum(old_datum);
}

bool
SpriteButton::press(int16_t press_x, int16_t press_y, bool pressed) {
    state_last = state_current;
    if (pressed && (press_x >= x) && (press_x < (x + width)) &&
            (press_y >= y) && (press_y < (y + height))) {
        state_current = true;
    } else {
        state_current = false;
    }
    if (state_current != state_last) {
        state_start = millis();
    }
    if (cb && justPressed()) {
        cb();
    }
    return state_current;
}

bool
SpriteButton::isPressed() {
    return state_current;
}

bool
SpriteButton::justPressed() {
    return (state_current && !state_last);
}

bool
SpriteButton::longPressed() {
    return (state_current && (millis() - state_start > 500));
}

bool
SpriteButton::justReleased() {
    return (!state_current && state_last);
}
