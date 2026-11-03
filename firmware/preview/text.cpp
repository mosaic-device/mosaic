#include "text.hpp"

#include "drivers/display.hpp"
#include "lvgl.h"

#include <vector>

namespace mosaic::preview {
namespace {
    // Decodes one UTF-8 code point, advancing at.
    uint32_t next_letter(std::string_view text, size_t& at) {
        const auto byte = [&](size_t i) { return static_cast<unsigned char>(text[i]); };
        const unsigned char first = byte(at);
        size_t length = first < 0x80 ? 1 : (first >> 5) == 0x6 ? 2 : (first >> 4) == 0xE ? 3 : 4;
        if (at + length > text.size())
            length = 1;
        uint32_t letter = length == 1 ? first : first & (0xFF >> (length + 1));
        for (size_t i = 1; i < length; ++i)
            letter = (letter << 6) | (byte(at + i) & 0x3F);
        at += length;
        return letter;
    }

    // Glyph lookups touch LVGL's font cache, which the firmware's LVGL task also uses.
    struct FontLock {
        bool locked = drivers::display::lock();
        ~FontLock() {
            if (locked)
                drivers::display::unlock();
        }
    };

    struct Layout {
        int width = 0;
        template <typename Glyph> void each(const lv_font_t* font, std::string_view text, Glyph glyph) {
            int x = 0;
            for (size_t at = 0; at < text.size();) {
                const uint32_t letter = next_letter(text, at);
                size_t peek = at;
                const uint32_t next = at < text.size() ? next_letter(text, peek) : 0;
                lv_font_glyph_dsc_t dsc { };
                if (!lv_font_get_glyph_dsc(font, &dsc, letter, next))
                    lv_font_get_glyph_dsc(font, &dsc, '?', next);
                glyph(dsc, x);
                x += dsc.adv_w;
            }
            width = x;
        }
    };
} // namespace

int Text::width(const lv_font_t* font, std::string_view text, float scale) {
    const FontLock lock;
    Layout layout;
    layout.each(font, text, [](const lv_font_glyph_dsc_t&, int) { });
    return static_cast<int>(static_cast<float>(layout.width) * scale);
}

Text::Rendered Text::render(SDL_Renderer* renderer, const lv_font_t* font, std::string_view text) {
    const FontLock lock;
    Layout layout;
    layout.each(font, text, [](const lv_font_glyph_dsc_t&, int) { });
    const int width = std::max(layout.width, 1);
    const int height = font->line_height;
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height, 0);
    const auto* format = static_cast<const lv_font_fmt_txt_dsc_t*>(font->dsc);
    layout.each(font, text, [&](const lv_font_glyph_dsc_t& dsc, int x0) {
        if (!dsc.box_w || !dsc.box_h || format->bpp != 4 || format->bitmap_format != LV_FONT_FMT_TXT_PLAIN)
            return;
        const auto& glyph = format->glyph_dsc[dsc.gid.index];
        const uint8_t* bits = format->glyph_bitmap + glyph.bitmap_index;
        const int top = font->line_height - font->base_line - dsc.box_h - dsc.ofs_y;
        // 4-bit coverage, packed row after row with no padding.
        for (int y = 0; y < dsc.box_h; ++y)
            for (int x = 0; x < dsc.box_w; ++x) {
                const size_t i = static_cast<size_t>(y) * dsc.box_w + x;
                const uint8_t nibble = (bits[i / 2] >> (i % 2 ? 0 : 4)) & 0xF;
                const int px = x0 + dsc.ofs_x + x, py = top + y;
                if (nibble && px >= 0 && px < width && py >= 0 && py < height)
                    pixels[static_cast<size_t>(py) * width + px] = 0x00FFFFFFu | (uint32_t { nibble } * 17) << 24;
            }
    });
    auto* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, width, height);
    if (texture) {
        SDL_UpdateTexture(texture, nullptr, pixels.data(), width * 4);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    }
    return { texture, width, height };
}

int Text::draw(SDL_Renderer* renderer, const lv_font_t* font, std::string_view text, int x, int y, SDL_Color color,
    Align align, float scale) {
    if (text.empty())
        return 0;
    // Readouts change every frame; keep the cache from growing without bound.
    if (cache_.size() > 400)
        clear();
    const std::string key = std::to_string(reinterpret_cast<uintptr_t>(font)) + '\x1f' + std::string(text);
    auto found = cache_.find(key);
    if (found == cache_.end())
        found = cache_.emplace(key, render(renderer, font, text)).first;
    const Rendered& rendered = found->second;
    if (!rendered.texture)
        return 0;
    const int width = static_cast<int>(static_cast<float>(rendered.width) * scale);
    const int height = static_cast<int>(static_cast<float>(rendered.height) * scale);
    if (align == Align::center)
        x -= width / 2;
    else if (align == Align::right)
        x -= width;
    SDL_SetTextureColorMod(rendered.texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(rendered.texture, color.a);
    const SDL_Rect target { x, y, width, height };
    SDL_RenderCopy(renderer, rendered.texture, nullptr, &target);
    return width;
}

void Text::clear() {
    for (auto& [key, rendered] : cache_)
        if (rendered.texture)
            SDL_DestroyTexture(rendered.texture);
    cache_.clear();
}

} // namespace mosaic::preview
