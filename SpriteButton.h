#include <TFT_eSPI.h>

class SpriteButton {
  public:
    SpriteButton(void);
    bool needsInit();
    void initButton(TFT_eSprite *fb, int16_t corner_x, int16_t corner_y,
            uint16_t w, uint16_t h, uint16_t outline, uint16_t fill,
            String label, const GFXfont *font,
            void (*callback)(void) = nullptr);
    void drawButton();
    bool press(int16_t x, int16_t y, bool pressed);
    bool isPressed();
    bool justPressed();
    bool longPressed();
    bool justReleased();

  private:
    TFT_eSprite *fb;
    void (*cb)(void);
    int16_t        x;
    int16_t        y;
    uint16_t       width;
    uint16_t       height;
    uint16_t       color_outline;
    uint16_t       color_fill;
    String         text;
    const GFXfont *text_font;
    bool           state_current;
    bool           state_last;
    unsigned long  state_start;
};
