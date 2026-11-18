#include "screen_compat.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

// Only the native fields the bridge reads; deliberately use native pointers
// before them to catch accidental dependence on this host's structure layout.
struct NativeScreen {
    void* private_data = nullptr;
    int wid=0, flag=1, sx=960, sy=540, palmode=0, type=0;
    short fl_dispw=1, fl_udraw=0;
    int wx=0,wy=0,wchg=0,viewx=0,viewy=0,lx=960,ly=540;
    int cx=7,cy=9,ox=64,oy=24,py=24,texty=12,gx=32,gy=32,gmode=3;
    int color=0x123456,textspeed=0,cx2=0,cy2=0,tex=0,tey=0;
    int focflg=0,objmode=2,style=0,gfrate=128,tabmove=1,sx2=960;
    short printsizex=18,printsizey=12;
    int objstyle=0,objmax=5,objlimit=1024;
};

int main() {
    cp2::ScreenRecords screens;
    NativeScreen native;
    auto* record = &screens.refresh(native);
    const auto* bytes = reinterpret_cast<const unsigned char*>(record);
    std::uint32_t hwnd;
    std::memcpy(&hwnd, bytes+52, 4);
    assert(hwnd == 0x80000000u);
    std::int32_t object_count;
    std::memcpy(&object_count, bytes+72*4, 4);
    assert(object_count == 5);
    assert(record->sx == 960 && record->sy == 540);
    assert(record->cx == 7 && record->cy == 9);
    assert(record->color == 0x563412);
    assert(record->hdc == 0 && record->pixels == 0 && record->bitmap_info == 0);
    for (int id=1; id<100; ++id) {
        NativeScreen extra;
        extra.wid=id;
        screens.refresh(extra);
    }
    assert(screens.find(0) == record); // mref alias survives new screens.
    native.sx=1280; native.objmax=8;
    assert(&screens.refresh(native) == record);
    assert(record->sx == 1280 && record->object_count == 8);
    assert(screens.find(99)->window == (0x80000000u | 99));
    native.cx = 123;
    screens.refresh_existing([&](int id) -> NativeScreen* { return id == 0 ? &native : nullptr; });
    assert(record->cx == 123);
    assert(screens.find(99)->flag == 0 && screens.find(99)->window == 0);
    assert(screens.find(100) == nullptr);
    bool rejected=false;
    native.wid=-1;
    try { screens.refresh(native); } catch (const std::invalid_argument&) { rejected=true; }
    assert(rejected);
    std::puts("Screen ABI: offsets, native layout independence, metadata and stable aliases passed");
}
