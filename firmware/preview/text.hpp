#pragma once
// Text for the preview's own panels, drawn with the firmware's compiled Manrope fonts so
// the chrome needs no font library and matches the device's type.
#include <SDL.h>

#include <map>
#include <string>
#include <string_view>

typedef struct _lv_font_t lv_font_t;

namespace mosaic::preview {

class Text {
public:
    enum class Align : uint8_t { left, center, right };
    // Draws text with its top at y, scaled from the font's size; returns its width. Window
    // thread only.
    int draw(SDL_Renderer* renderer, const lv_font_t* font, std::string_view text, int x, int y, SDL_Color color,
        Align align = Align::left, float scale = 1.0F);
    int width(const lv_font_t* font, std::string_view text, float scale = 1.0F);
    void clear();

private:
    struct Rendered {
        SDL_Texture* texture;
        int width;
        int height;
    };
    Rendered render(SDL_Renderer* renderer, const lv_font_t* font, std::string_view text);
    std::map<std::string, Rendered> cache_;
};

} // namespace mosaic::preview
