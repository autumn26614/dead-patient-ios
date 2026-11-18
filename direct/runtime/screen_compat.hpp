#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>

namespace cp2 {

// The Win32 HSP3 BMSCR prefix, through objlimit. Never put native pointers in
// this record: recovered code indexes it as an array of four-byte integers.
// Pointer/Windows GDI slots stay null until their corresponding service exists.
struct ScreenRecord32 {
    std::int32_t flag, sx, sy, palmode;
    std::uint32_t hdc, pixels, bitmap_info, dib, old_bitmap, palette;
    std::uint32_t palette_handle, old_palette_handle;
    std::int32_t palette_entries;
    std::uint32_t window, instance;
    std::int32_t info_bytes, bitmap_bytes, type, id;
    std::int16_t displayed, update;
    std::int32_t wx, wy, changed, viewx, viewy, lx, ly;
    std::int32_t cx, cy, ox, oy, py, texty, gx, gy, copy_mode;
    std::uint32_t brush, pen, font, old_font, color;
    std::int32_t text_speed, cx2, cy2, tex, tey;
    std::uint32_t message;
    std::int32_t focus, object_mode;
    std::uint32_t logfont;
    std::int32_t log_options[14];
    std::int32_t style, blend_rate, tab_move, stride_pixels;
    std::int32_t print_width, print_height, object_style;
    std::uint32_t objects;
    std::int32_t object_count, object_limit;
};

static_assert(sizeof(ScreenRecord32) == 296, "Win32 BMSCR prefix size");
static_assert(offsetof(ScreenRecord32, window) == 52, "ZGP reads HWND at +52");
static_assert(offsetof(ScreenRecord32, id) == 72, "Win32 BMSCR window ID");
static_assert(offsetof(ScreenRecord32, object_count) == 288, "llmod reads bmscr(72)");

// std::map keeps the address stable while HSP mref variables alias a record.
// This is a metadata bridge, not a GDI bitmap or a native Bmscr cast.
class ScreenRecords {
public:
    template<class NativeScreen>
    ScreenRecord32& refresh(const NativeScreen& native) {
        if (native.wid < 0) throw std::invalid_argument("Negative screen ID");
        auto& record = records_[native.wid];
        record.flag = native.flag;
        record.sx = native.sx;
        record.sy = native.sy;
        record.palmode = native.palmode;
        // A typed compatibility handle identifies the actual HSP surface. It
        // is deliberately not a process pointer or an invented GDI HDC.
        record.window = 0x80000000u | static_cast<std::uint32_t>(native.wid);
        record.type = native.type;
        record.id = native.wid;
        record.displayed = native.fl_dispw;
        record.update = native.fl_udraw;
        record.wx = native.wx; record.wy = native.wy; record.changed = native.wchg;
        record.viewx = native.viewx; record.viewy = native.viewy;
        record.lx = native.lx; record.ly = native.ly;
        record.cx = native.cx; record.cy = native.cy;
        record.ox = native.ox; record.oy = native.oy; record.py = native.py;
        record.texty = native.texty;
        record.gx = native.gx; record.gy = native.gy; record.copy_mode = native.gmode;
        const auto rgba = static_cast<std::uint32_t>(native.color);
        // HSPDish uses 0xAARRGGBB; Win32 COLORREF is 0x00BBGGRR.
        record.color = ((rgba & 0xff) << 16) | (rgba & 0xff00) | ((rgba >> 16) & 0xff);
        record.text_speed = native.textspeed;
        record.cx2 = native.cx2; record.cy2 = native.cy2;
        record.tex = native.tex; record.tey = native.tey;
        record.focus = native.focflg; record.object_mode = native.objmode;
        record.style = native.style; record.blend_rate = native.gfrate;
        record.tab_move = native.tabmove; record.stride_pixels = native.sx2;
        record.print_width = native.printsizex; record.print_height = native.printsizey;
        record.object_style = native.objstyle;
        record.object_count = native.objmax; record.object_limit = native.objlimit;
        return record;
    }

    ScreenRecord32* find(int id) {
        auto entry = records_.find(id);
        return entry == records_.end() ? nullptr : &entry->second;
    }

    template<class Lookup>
    void refresh_existing(Lookup lookup) {
        for (auto& entry : records_) {
            auto* native = lookup(entry.first);
            if (native) refresh(*native);
            else { entry.second.flag = 0; entry.second.window = 0; }
        }
    }

private:
    std::map<int, ScreenRecord32> records_;
};

} // namespace cp2
