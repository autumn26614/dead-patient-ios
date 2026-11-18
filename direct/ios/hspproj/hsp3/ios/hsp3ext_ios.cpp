
//
//	HSP3 External program manager (dummy)
//	onion software/onitama 2011/11
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hsp3config.h"
#include "hsp3ext_ios.h"

#include "../../hsp3dish/hgio.h"
#include "../../hsp3dish/sysreq.h"
#include "../../hsp3embed/hsp3embed.h"
#include "../../hsp3embed/native_loader.hpp"
#include "../../hsp3embed/screen_compat.hpp"
#include "../../hsp3embed/win32_compat.hpp"
#include "../../hsp3embed/zgp_gles.hpp"
#include "../../hsp3embed/zgp_math.hpp"
#include "../../hsp3embed/legacy_memory_bridge.hpp"
#include "../../hsp3embed/asset_bridge.hpp"
#include "../../hsp3embed/csmesh.hpp"
#include "../../hsp3embed/dsound.hpp"
#include "../../hsp3embed/hspda.hpp"
#include "../../hsp3embed/zgp_device.hpp"
#include "../../hsp3embed/zgp_gl.hpp"
#include <AudioToolbox/AudioToolbox.h>
#include <CommonCrypto/CommonDigest.h>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#ifdef HSPDISHGP
#include "iOSgpBridge.h"
#else
#include "iOSBridge.h"
#endif

/*------------------------------------------------------------*/
/*
		System Information initialization
*/
/*------------------------------------------------------------*/

static HSPCTX *hspctx;		// Current Context
static HSPEXINFO *exinfo;	// Info for Plugins

static void InitSystemInformation(void)
{
	//		コマンドライン & システムフォルダ関連
	hspctx->modfilename = "";
	hspctx->homefoldername = "";
	hspctx->tvfoldername = "";

    gb_getLocale(hspctx->langcode);
    if ((hspctx->langcode[0]=='j')&&(hspctx->langcode[1]=='a')) {
        hspctx->language = HSPCTX_LANGUAGE_JP;
    }
}


/*------------------------------------------------------------*/
/*
		type function
*/
/*------------------------------------------------------------*/

static cp2::NativeLoader native_loader;
static int native_loader_result;
static cp2::GraphicsFactory graphics_factory;
static cp2::Win32Windows native_windows;
extern "C" cp2::ScreenRecord32* cp2_screen_record(int id);

extern "C" int cp2_graphics_driver_test()
{
    if (!cp2::graphics_driver_test()) return 0;
    // 3D drawing on the OpenGL ES device, before the game creates its own.
    return cp2::zgp::GlDevice::self_test_3d() ? 1 : 0;
}

/*------------------------------------------------------------*/
/*
		ZGP animation libraries (anilibload)
*/
/*------------------------------------------------------------*/

// ZGP keeps 96-byte animation-library slots at 0x1005f950; its device
// parameter block begins at 0x1005fb00, so only ids 0..3 are distinct slots.
// The slots live in ZGP's renderer state (zgp_device.hpp). A plugin receives
// the address of ZGP's device pointer at registration and reads it only when
// loading or drawing, which is why the original can register CSMesh before
// createdevm. See docs/csmesh-abi.md.
static const int kZgpAnimationLibrarySlots = 4;

// SHA-256 of the original plugin/CSMesh.dll whose exports csmesh.hpp implements.
static const char kOriginalCSMeshSha256[] = "45ee90300469a58c0ba43d8983b7ee078810198513f44da18220e63f691905e7";

static bool file_sha256(const char* path, std::string& hex)
{
    FILE* input = fopen(path, "rb");
    if (!input) return false;
    CC_SHA256_CTX hash;
    CC_SHA256_Init(&hash);
    unsigned char buffer[65536], digest[CC_SHA256_DIGEST_LENGTH];
    size_t size;
    while ((size = fread(buffer, 1, sizeof(buffer), input))) CC_SHA256_Update(&hash, buffer, (CC_LONG)size);
    bool failed = ferror(input) != 0;
    fclose(input);
    if (failed) return false;
    CC_SHA256_Final(digest, &hash);
    char text[65];
    for (int i = 0; i < 32; ++i) snprintf(text + i * 2, 3, "%02x", digest[i]);
    hex = text;
    return true;
}

// ZGP's error helper (0x10019640) shows a dialog only after zgpdebug, which the
// game never calls; otherwise it returns the negated code.
static int zgp_error(int code)
{
    return -code;
}

// _anilibload@16 (0x10020420): load the plugin into slot id, call its
// Init(&device, count) and select the slot. Returns 0, or -5 when the plugin
// file cannot be loaded.
static int zgp_anilibload(int id, const char* name, int count)
{
    if (id < 0 || id >= kZgpAnimationLibrarySlots) {
        // The original writes past its slot table into unrelated ZGP state.
        fprintf(stderr, "[CP2 ZGP] anilibload id %d is outside ZGP's %d animation-library slots\n",
                id, kZgpAnimationLibrarySlots);
        throw HSPERR_ILLEGAL_FUNCTION;
    }
    cp2::zgp::Renderer::AnimationLibrary& slot = cp2::zgp::renderer().animation_libraries[id];
    // 0x1000e580: an occupied slot is shut down (Delete) and freed first.
    if (slot.loaded) {
        slot.library.destroy();
        slot.loaded = false;
        slot.path.clear();
    }
    const char* resolved = cp2_asset_read_path(name);
    std::string digest;
    if (!resolved || !file_sha256(resolved, digest)) {
        // LoadLibraryA failed: the original shows "DLL not found" and fails with error 5.
        fprintf(stderr, "[CP2 ZGP] anilibload(%d, %s): plugin file not found; ZGP error 5\n", id, name);
        return zgp_error(5);
    }
    if (digest != kOriginalCSMeshSha256) {
        fprintf(stderr, "[CP2 ZGP] anilibload(%d, %s): no native implementation for plugin SHA256 %s\n",
                id, name, digest.c_str());
        fflush(stderr);
        throw HSPERR_UNSUPPORTED_FUNCTION;
    }
    cp2::zgp::register_animation_library(id, name, count);
    fprintf(stderr, "[CP2 ZGP] anilibload(%d, %s): native CSMesh backend registered, %d model slots\n",
            id, name, slot.library.capacity());
    return 0;
}

/*------------------------------------------------------------*/
/*
		DirectSound plugin (plugin/dsoundex.hpi)
*/
/*------------------------------------------------------------*/

// The plugin's buffers, mixed by a RemoteIO output unit. If the output unit
// cannot start (for example a headless simulator), a real-time thread renders
// into a scratch buffer so play cursors and status still advance with time.
static std::unique_ptr<cp2::zgp::PortableAdapter> zgp_adapter;
static std::unique_ptr<cp2::zgp::StateDevice> zgp_state_device;
static cp2::dsound::Device ds_device;
static AudioComponentInstance ds_output = NULL;
static std::thread ds_clock;
static std::atomic<bool> ds_clock_running(false);
static const double kDsOutputRate = 44100.0;

static OSStatus ds_render(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 frames,
                          AudioBufferList* data)
{
    float* out = static_cast<float*>(data->mBuffers[0].mData);
    ds_device.render(out, frames, kDsOutputRate);
    return noErr;
}

static bool ds_start_output()
{
    AudioComponentDescription description = {};
    description.componentType = kAudioUnitType_Output;
    description.componentSubType = kAudioUnitSubType_RemoteIO;
    description.componentManufacturer = kAudioUnitManufacturer_Apple;
    AudioComponent component = AudioComponentFindNext(NULL, &description);
    if (!component || AudioComponentInstanceNew(component, &ds_output) != noErr) return false;
    AudioStreamBasicDescription format = {};
    format.mSampleRate = kDsOutputRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    format.mChannelsPerFrame = 2;
    format.mBitsPerChannel = 32;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = 8;
    format.mBytesPerPacket = 8;
    AURenderCallbackStruct callback = {ds_render, NULL};
    if (AudioUnitSetProperty(ds_output, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format,
                             sizeof(format)) != noErr ||
        AudioUnitSetProperty(ds_output, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback,
                             sizeof(callback)) != noErr ||
        AudioUnitInitialize(ds_output) != noErr || AudioOutputUnitStart(ds_output) != noErr) {
        AudioComponentInstanceDispose(ds_output);
        ds_output = NULL;
        return false;
    }
    return true;
}

static void ds_start_clock()
{
    if (ds_clock_running.exchange(true)) return;
    ds_clock = std::thread([] {
        const std::size_t frames = 441;  // 10 ms
        std::vector<float> scratch(frames * 2);
        auto next = std::chrono::steady_clock::now();
        while (ds_clock_running) {
            ds_device.render(scratch.data(), frames, kDsOutputRate);
            next += std::chrono::milliseconds(10);
            std::this_thread::sleep_until(next);
        }
    });
    ds_clock.detach();
}

// _ds_init@16 (0x10001a10): BMSCR and DPM information are automatic
// parameters; the two integers select the requested primary format.
static int ds_init_command(int frequency, int channels)
{
    int result = ds_device.init(frequency, channels);
    if (!ds_output && !ds_clock_running) {
        if (ds_start_output()) {
            fprintf(stderr, "[CP2 audio] dsoundex backend: RemoteIO output at %.0f Hz\n", kDsOutputRate);
        } else {
            fprintf(stderr, "[CP2 audio] dsoundex backend: no audio output unit; buffers advance on a real-time clock\n");
            ds_start_clock();
        }
    }
    return result;
}

static int ds_load_command(const char* name, int slot)
{
    const char* resolved = cp2_asset_read_path(name);
    std::vector<std::uint8_t> bytes;
    FILE* input = resolved ? fopen(resolved, "rb") : NULL;
    if (input) {
        unsigned char chunk[65536];
        size_t size;
        while ((size = fread(chunk, 1, sizeof(chunk), input))) bytes.insert(bytes.end(), chunk, chunk + size);
        fclose(input);
    }
    std::string error;
    int result = ds_device.load(slot, input ? bytes.data() : NULL, bytes.size(), error);
    if (!result) fprintf(stderr, "[CP2 audio] ds_load(%s, %d) failed: %s\n", name, slot, error.c_str());
    return result;
}

static bool dispatch_dsound(const char* symbol, int& result)
{
    if (strncmp(symbol, "_ds_", 4)) return false;
    if (!strcmp(symbol, "_ds_init@16")) {
        code_next();
        int frequency = code_getdi(0), channels = code_getdi(0);
        result = ds_init_command(frequency, channels);
        return true;
    }
    if (!strcmp(symbol, "_ds_load@16")) {
        code_next();
        std::string name = code_gets();
        int slot = code_getdi(0);
        code_getdi(0);
        result = ds_load_command(name.c_str(), slot);
        return true;
    }
    if (!strcmp(symbol, "_ds_memload@16")) {
        code_next();
        PVal* variable;
        int bytes;
        void* data = code_getvptr(&variable, &bytes);
        int slot = code_getdi(0);
        code_getdi(0);
        code_getdi(0);
        std::string error;
        result = ds_device.memload(slot, static_cast<const std::uint8_t*>(data), bytes > 0 ? (size_t)bytes : 0, error);
        if (result) fprintf(stderr, "[CP2 audio] ds_memload(%d) failed: %s\n", slot, error.c_str());
        return true;
    }
    static const char* const simple[] = {"_ds_play@16", "_ds_loop@16", "_ds_stop@16", "_ds_getp@16", "_ds_stat@16",
                                         "_ds_vol@16", "_ds_dup@16", "_ds_bye@16"};
    for (const char* name : simple) {
        if (strcmp(symbol, name)) continue;
        code_next();
        int a = code_getdi(0), b = code_getdi(0), c = code_getdi(0);
        code_getdi(0);
        if (!strcmp(name, "_ds_play@16")) result = ds_device.play(a, b, c);
        else if (!strcmp(name, "_ds_loop@16")) result = ds_device.play(a, b, cp2::dsound::kPlayLooping);
        else if (!strcmp(name, "_ds_stop@16")) result = ds_device.stop(a);
        else if (!strcmp(name, "_ds_getp@16")) result = ds_device.position(a);
        else if (!strcmp(name, "_ds_stat@16")) result = ds_device.status(a);
        else if (!strcmp(name, "_ds_vol@16")) result = ds_device.volume(a, b);
        else if (!strcmp(name, "_ds_dup@16")) result = ds_device.duplicate(a, b);
        else result = ds_device.stop_all();
        return true;
    }
    return false;  // other exports are not declared by the game
}

// hspda.dll (sortnote/sortget, the only exports the game declares). Both are
// pexinfo commands reading a variable and an int; direct/runtime/hspda.hpp
// is compared with the original plugin by direct/tests/hspda_test.cpp.
static cp2::hspda::Sorter hspda_sorter;

static bool dispatch_hspda(const char* symbol, int& result)
{
    if (!strcmp(symbol, "_sortnote@16")) {
        code_next();
        PVal* variable;
        APTR aptr = code_getva(&variable);
        int order = code_getdi(0);
        // The original reads the variable's buffer as text; the game passes a
        // string variable.
        if (variable->flag != HSPVAR_FLAG_STR) throw HSPERR_TYPE_MISMATCH;
        std::string text((const char*)HspVarCorePtrAPTR(variable, aptr));
        std::string sorted;
        result = hspda_sorter.sortnote(text, order, sorted);
        if (result == 0) code_setva(variable, aptr, HSPVAR_FLAG_STR, sorted.c_str());
        return true;
    }
    if (!strcmp(symbol, "_sortget@16")) {
        code_next();
        PVal* variable;
        APTR aptr = code_getva(&variable);
        int n = code_getdi(0);
        std::int32_t value = 0;
        result = hspda_sorter.sortget(n, value);
        if (result == 0) {
            int stored = value;
            code_setva(variable, aptr, HSPVAR_FLAG_INT, &stored);
        }
        return true;
    }
    return false;
}

// hspinet.dll: the title's update check (updata_checker) uses netinit,
// neturl, netrequest_get, netexec and netgetv. The request is a real HTTP
// GET (iOSBridge.mm, NSURLSession); netexec stores 0 while it runs, 1 when
// complete and -1 on error, as hspinet does; netgetv stores the body.
extern "C" void cp2_http_get(const char* url);
extern "C" int cp2_http_state_get(void);
extern "C" char* cp2_http_body_copy(void);
static std::string hspinet_url;

static bool dispatch_hspinet(const char* symbol, int& result)
{
    if (!strcmp(symbol, "_netinit@16")) {
        code_next();
        for (int i = 0; i < 4; ++i) code_getdi(0);
        result = 0;
        return true;
    }
    if (!strcmp(symbol, "_neturl@16")) {
        // Declared with an automatic BMSCR first; the program passes the URL.
        code_next();
        hspinet_url = code_gets();
        code_getdi(0);
        code_getdi(0);
        result = 0;
        return true;
    }
    if (!strcmp(symbol, "_netrequest_get@16")) {
        code_next();
        std::string file = code_gets();
        cp2_http_get((hspinet_url + file).c_str());
        result = 0;
        return true;
    }
    if (!strcmp(symbol, "_netexec@16")) {
        code_next();
        PVal* variable;
        APTR aptr = code_getva(&variable);
        for (int i = 0; i < 3; ++i) code_getdi(0);
        int state = cp2_http_state_get();
        code_setva(variable, aptr, HSPVAR_FLAG_INT, &state);
        result = 0;
        return true;
    }
    if (!strcmp(symbol, "_netgetv@16")) {
        code_next();
        PVal* variable;
        APTR aptr = code_getva(&variable);
        char* body = cp2_http_body_copy();
        code_setva(variable, aptr, HSPVAR_FLAG_STR, body ? body : "");
        free(body);
        result = 0;
        return true;
    }
    return false;
}

static const char* native_symbol(int cmd)
{
    if (!hspctx || !hspctx->hsphed || !hspctx->mem_finfo || !hspctx->mem_mds ||
        cmd < 0 || cmd >= hspctx->hsphed->max_finfo / (int)sizeof(STRUCTDAT)) return NULL;
    return hspctx->mem_mds + hspctx->mem_finfo[cmd].nameidx;
}

static bool refresh_native_window(unsigned int handle)
{
    if (!(handle & 0x80000000u)) return false;
    cp2::ScreenRecord32* screen = cp2_screen_record((int)(handle & 0x7fffffffu));
    if (!screen || screen->window != handle) return false;
    if (!native_windows.update_surface(handle, screen->sx, screen->sy))
        return native_windows.register_window(handle, screen->sx, screen->sy, screen->style);
    return true;
}

static bool dispatch_native_compat(int cmd, int& result)
{
    const char* symbol = native_symbol(cmd);
    if (!symbol) return false;
    if (dispatch_dsound(symbol, result)) return true;
    if (dispatch_hspda(symbol, result)) return true;
    if (dispatch_hspinet(symbol, result)) return true;
    // ZGP startup state (docs/zgp-renderer.md); all four-int old-DLL commands
    // except setbmscr, whose first parameter is the automatic active BMSCR.
    if (!strcmp(symbol, "_setaspect@16") || !strcmp(symbol, "_setmaxsurf@16") ||
        !strcmp(symbol, "_setmaxunit@16") || !strcmp(symbol, "_setmaxangle@16")) {
        code_next();
        int a = code_getdi(0), b = code_getdi(0), c = code_getdi(0), d = code_getdi(0);
        if (!strcmp(symbol, "_setaspect@16")) result = cp2::zgp::setaspect(a);
        else if (!strcmp(symbol, "_setmaxsurf@16")) result = cp2::zgp::setmaxsurf(a, b);
        else if (!strcmp(symbol, "_setmaxunit@16")) result = cp2::zgp::setmaxunit(a, b, c, d);
        else result = cp2::zgp::setmaxangle(a, b);
        return true;
    }
    if (!strcmp(symbol, "_setbmscr@16")) {
        code_next();
        for (int i = 0; i < 3; ++i) code_getdi(0);
        cp2::ScreenRecord32* screen = cp2_screen_record(-1);
        if (!screen) throw HSPERR_ILLEGAL_FUNCTION;
        result = cp2::zgp::setbmscr(screen->window);
        return true;
    }
    if (!strcmp(symbol, "_createdx@16")) {
        code_next();
        // Four optional legacy integer arguments; createdx's body ignores them.
        for (int i=0; i<4; ++i) code_getdi(0);
        if (!graphics_factory.initialize()) throw HSPERR_UNSUPPORTED_FUNCTION;
        // Direct3DCreate8: the portable adapter answers ZGP's capability queries
        // and creates the platform device (state only until drawing is ported).
        if (!zgp_adapter) {
            // D3DX opens texture files with CreateFileA; the asset resolver
            // applies the same Windows path and case rules to the originals.
            cp2::zgp::renderer().read_file = [](const std::string& path, std::vector<unsigned char>& data) {
                const char* resolved = cp2_asset_read_path(path.c_str());
                FILE* input = resolved ? fopen(resolved, "rb") : NULL;
                if (!input) return false;
                data.clear();
                unsigned char chunk[65536];
                size_t size;
                while ((size = fread(chunk, 1, sizeof(chunk), input))) data.insert(data.end(), chunk, chunk + size);
                fclose(input);
                return true;
            };
            zgp_adapter.reset(new cp2::zgp::PortableAdapter(
                (std::uint32_t)hgio_getWidth(), (std::uint32_t)hgio_getHeight(),
                [](const cp2::d3d8::PresentParameters& p) -> cp2::d3d8::Device* {
                    // The OpenGL ES device on HSP's EAGL context (zgp_gl.hpp).
                    std::unique_ptr<cp2::zgp::GlDevice> device(new cp2::zgp::GlDevice());
                    std::string error;
                    // Rendered at the view's native pixel resolution, unless
                    // CP2_RENDER_NATIVE=0 (the CI simulator, whose software
                    // rasterizer is fill-rate bound, renders at 960x540).
                    const char* native_setting = getenv("CP2_RENDER_NATIVE");
                    const bool native = !(native_setting && !strcmp(native_setting, "0"));
                    if (!device->create(p, error, native)) {
                        fprintf(stderr, "[CP2 GL] ZGP device creation failed: %s\n", error.c_str());
                        return NULL;
                    }
                    zgp_state_device.reset(device.release());
                    return zgp_state_device.get();
                }));
        }
        result = cp2::zgp::createdx(zgp_adapter.get());
        return true;
    }
    if (!strcmp(symbol, "_createdevm@16")) {
        code_next();
        PVal* variable;
        int bytes;
        void* data = code_getvptr(&variable, &bytes);
        for (int i = 0; i < 3; ++i) code_getdi(0);
        if (variable->flag != HSPVAR_FLAG_INT || bytes < 11 * (int)sizeof(int)) throw HSPERR_TYPE_MISMATCH;
        std::int32_t a[11];
        memcpy(a, data, sizeof(a));
        const char* failure = "";
        result = cp2::zgp::createdevm(a, failure);
        memcpy(data, a, sizeof(a));
        if (*failure) fprintf(stderr, "[CP2 ZGP] createdevm failed: %s\n", failure);
        else fprintf(stderr, "[CP2 ZGP] createdevm: %ux%u device, back format %u, depth format %u\n",
                     cp2::zgp::renderer().present.width, cp2::zgp::renderer().present.height,
                     cp2::zgp::renderer().present.format, cp2::zgp::renderer().present.depth_format);
        return true;
    }
    {
        static const char* const camera[] = {"_set3d@16", "_cat@16", "_setfovy@16", "_setview@16", "_lightdir@16",
                                              "_lightamb@16"};
        for (const char* name : camera) {
            if (strcmp(symbol, name)) continue;
            code_next();
            int a = code_getdi(0), b = code_getdi(0), c = code_getdi(0), d = code_getdi(0);
            if (!strcmp(name, "_set3d@16")) result = cp2::zgp::set3d(a, b, c, d);
            else if (!strcmp(name, "_cat@16")) result = cp2::zgp::cat(a, b, c);
            else if (!strcmp(name, "_setfovy@16")) result = cp2::zgp::setfovy(a);
            else if (!strcmp(name, "_setview@16")) result = cp2::zgp::setview(a, b, c, d);
            else if (!strcmp(name, "_lightdir@16")) result = cp2::zgp::lightdir(a, b, c);
            else result = cp2::zgp::lightamb(a);
            return true;
        }
    }
    // Textures and the D3DX font (docs/zgp-renderer.md, "Texture loading").
    if (!strcmp(symbol, "_textrans@16") || !strcmp(symbol, "_texture@16")) {
        code_next();
        int a = code_getdi(0), b = code_getdi(0), c = code_getdi(0), d = code_getdi(0);
        if (!strcmp(symbol, "_textrans@16")) result = cp2::zgp::textrans(a);
        else result = cp2::zgp::texture(a, b, c, d);
        return true;
    }
    if (!strcmp(symbol, "_texload@16") || !strcmp(symbol, "_setfont@16")) {
        // Both are declared int, str, int, int; the fourth argument is unused.
        code_next();
        int a = code_getdi(0);
        std::string text = code_gets();
        int c = code_getdi(0);
        code_getdi(0);
        if (!strcmp(symbol, "_texload@16")) {
            result = cp2::zgp::texload(a, text, c);
            if (result) fprintf(stderr, "[CP2 ZGP] texload(%d, %s, %d) = %d\n", a, text.c_str(), c, result);
        } else {
            result = cp2::zgp::setfont(a, text, c);
        }
        return true;
    }
    {
        // Frames, colors and 2D drawing (docs/zgp-renderer.md, "Frames and 2D drawing").
        static const char* const frame[] = {"_zclear@16", "_bscene@16", "_escene@16", "_fill2d@16",
                                             "_line2d@16", "_setcolor4@16", "_setblend@16", "_set2d@16"};
        for (const char* name : frame) {
            if (strcmp(symbol, name)) continue;
            code_next();
            int a = code_getdi(0), b = code_getdi(0), c = code_getdi(0), d = code_getdi(0);
            if (!strcmp(name, "_zclear@16")) result = cp2::zgp::zclear();
            else if (!strcmp(name, "_bscene@16")) result = cp2::zgp::bscene(a, b);
            else if (!strcmp(name, "_escene@16")) result = cp2::zgp::escene(a, b, c, d);
            else if (!strcmp(name, "_fill2d@16")) result = cp2::zgp::fill2d(a, b, c, d);
            else if (!strcmp(name, "_line2d@16")) result = cp2::zgp::line2d(a, b, c, d);
            else if (!strcmp(name, "_setcolor4@16")) result = cp2::zgp::setcolor4(a, b, c, d);
            else if (!strcmp(name, "_setblend@16")) result = cp2::zgp::setblend(a);
            else result = cp2::zgp::set2d(a);
            return true;
        }
    }
    if (!strcmp(symbol, "_chrset@16") || !strcmp(symbol, "_sprset@16")) {
        code_next();
        int a = code_getdi(0), b = code_getdi(0), c = code_getdi(0), d = code_getdi(0);
        // The original reads cx, cy (+0x6c, +0x70) of the BMSCR given to
        // setbmscr, live; without one it would dereference null.
        const cp2::zgp::State& zs = cp2::zgp::state();
        cp2::ScreenRecord32* screen =
            zs.bmscr_set ? cp2_screen_record((int)(zs.bmscr_window & 0x7fffffffu)) : NULL;
        if (!screen) throw HSPERR_ILLEGAL_FUNCTION;
        if (!strcmp(symbol, "_chrset@16")) result = cp2::zgp::chrset(a, b, c, d, screen->cx, screen->cy);
        else result = cp2::zgp::sprset(a, b, c, d, screen->cx, screen->cy);
        return true;
    }
    {
        // Sprites (docs/zgp-renderer.md, "Sprites").
        static const char* const sprites[] = {"_sprflag@16", "_sprpos@16", "_sprzoom@16", "_sprang@16",
                                               "_sprlate@16", "_sprblend@16", "_sprdel@16", "_sprdraw@16"};
        for (const char* name : sprites) {
            if (strcmp(symbol, name)) continue;
            code_next();
            int a = code_getdi(0), b = code_getdi(0), c = code_getdi(0), d = code_getdi(0);
            if (!strcmp(name, "_sprflag@16")) result = cp2::zgp::sprflag(a, b);
            else if (!strcmp(name, "_sprpos@16")) result = cp2::zgp::sprpos(a, b, c, d);
            else if (!strcmp(name, "_sprzoom@16")) result = cp2::zgp::sprzoom(a, b, c);
            else if (!strcmp(name, "_sprang@16")) result = cp2::zgp::sprang(a, b, c);
            else if (!strcmp(name, "_sprlate@16")) result = cp2::zgp::sprlate(a, b, c, d);
            else if (!strcmp(name, "_sprblend@16")) result = cp2::zgp::sprblend(a, b);
            else if (!strcmp(name, "_sprdel@16")) result = cp2::zgp::sprdel(a);
            else result = cp2::zgp::sprdraw(a, b, c);
            return true;
        }
    }
    if (!strcmp(symbol, "_meshboard@16") || !strcmp(symbol, "_objang@16")) {
        // docs/zgp-renderer.md, "Meshes" and "Objects".
        code_next();
        int a = code_getdi(0), b = code_getdi(0), c = code_getdi(0), d = code_getdi(0);
        result = !strcmp(symbol, "_meshboard@16") ? cp2::zgp::meshboard(a, b) : cp2::zgp::objang(a, b, c, d);
        return true;
    }
    if (!strcmp(symbol, "_meshsz@16") || !strcmp(symbol, "_meshbox@16")) {
        // Generated meshes (docs/zgp-renderer.md, "Meshes").
        code_next();
        int a = code_getdi(0), b = code_getdi(0), c = code_getdi(0), d = code_getdi(0);
        if (!strcmp(symbol, "_meshsz@16")) result = cp2::zgp::meshsz(a, b, c);
        else result = cp2::zgp::meshbox(a, b, c, d);
        return true;
    }
    if (!strcmp(symbol, "_loadax@16")) {
        // Declared with the legacy type 36: int, str, int, then an automatic
        // DPM pointer that is not part of the call (docs/recompilation.md).
        code_next();
        int model = code_getdi(0);
        std::string file = code_gets();
        int unused = code_getdi(0);
        result = cp2::zgp::loadax(model, file, unused);
        return true;
    }
    if (!strcmp(symbol, "_meshmemset@16")) {
        // A mesh from the game's int array (docs/zgp-renderer.md, "Meshes").
        code_next();
        PVal* variable;
        int bytes;
        void* data = code_getvptr(&variable, &bytes);
        int mesh = code_getdi(0);
        code_getdi(0);
        code_getdi(0);
        if (variable->flag != HSPVAR_FLAG_INT) throw HSPERR_TYPE_MISMATCH;
        result = cp2::zgp::meshmemset(static_cast<const std::int32_t*>(data),
                                      bytes > 0 ? (size_t)bytes / sizeof(std::int32_t) : 0, mesh);
        return true;
    }
    if (!strcmp(symbol, "_loadx@16")) {
        // Static models (docs/zgp-renderer.md, "Static models"): mesh, file,
        // mip levels for the material textures; the fourth int is unused.
        code_next();
        int mesh = code_getdi(0);
        std::string file = code_gets();
        int mips = code_getdi(0);
        code_getdi(0);
        result = cp2::zgp::loadx(mesh, file, mips);
        return true;
    }
    if (!strcmp(symbol, "_anirate@16") || !strcmp(symbol, "_objameshset@16")) {
        // Animated models (docs/csmesh-abi.md, "Meshes").
        code_next();
        int a = code_getdi(0), b = code_getdi(0);
        code_getdi(0);
        code_getdi(0);
        if (!strcmp(symbol, "_anirate@16")) result = cp2::zgp::anirate(a, b);
        else result = cp2::zgp::objameshset(a, b);
        return true;
    }
    if (!strcmp(symbol, "_objlate@16")) {
        // docs/zgp-renderer.md, "Objects".
        code_next();
        int a = code_getdi(0), b = code_getdi(0), c = code_getdi(0), d = code_getdi(0);
        result = cp2::zgp::objlate(a, b, c, d);
        return true;
    }
    if (!strcmp(symbol, "_objtype@16")) {
        code_next();
        int a = code_getdi(0), b = code_getdi(0);
        code_getdi(0);
        code_getdi(0);
        result = cp2::zgp::objtype(a, b);
        return true;
    }
    if (!strcmp(symbol, "_objclno@16")) {
        code_next();
        int a = code_getdi(0), b = code_getdi(0);
        code_getdi(0);
        code_getdi(0);
        result = cp2::zgp::objclno(a, b);
        return true;
    }
    if (!strcmp(symbol, "_objcat@16")) {
        code_next();
        int a = code_getdi(0), b = code_getdi(0), c = code_getdi(0), d = code_getdi(0);
        result = cp2::zgp::objcat(a, b, c, d);
        return true;
    }
    if (!strcmp(symbol, "_objdraw@16") || !strcmp(symbol, "_objflag@16") || !strcmp(symbol, "_objrot@16") ||
        !strcmp(symbol, "_objzoom@16") || !strcmp(symbol, "_objemi@16") || !strcmp(symbol, "_objplay@16") ||
        !strcmp(symbol, "_objpause@16") || !strcmp(symbol, "_anilist@16")) {
        // Object pass and object commands (docs/zgp-renderer.md, "objdraw").
        code_next();
        int a = code_getdi(0), b = code_getdi(0), c = code_getdi(0), d = code_getdi(0);
        if (!strcmp(symbol, "_objdraw@16")) result = cp2::zgp::objdraw(a, b, c);
        else if (!strcmp(symbol, "_objflag@16")) result = cp2::zgp::objflag(a, b);
        else if (!strcmp(symbol, "_objrot@16")) result = cp2::zgp::objrot(a, b, c, d);
        else if (!strcmp(symbol, "_objzoom@16")) result = cp2::zgp::objzoom(a, b, c, d);
        else if (!strcmp(symbol, "_objemi@16")) result = cp2::zgp::objemi(a, b, c, d);
        else if (!strcmp(symbol, "_objplay@16")) result = cp2::zgp::objplay(a, b, c);
        else if (!strcmp(symbol, "_objpause@16")) result = cp2::zgp::objpause(a);
        else result = cp2::zgp::anilist(a, b);
        return true;
    }
    if (!strcmp(symbol, "_z_mat@16")) {
        // Rotates an int vector by yaw-pitch-roll angles (docs/zgp-renderer.md).
        code_next();
        PVal* variable;
        int bytes;
        void* data = code_getvptr(&variable, &bytes);
        int x = code_getdi(0), y = code_getdi(0), z = code_getdi(0);
        if (variable->flag != HSPVAR_FLAG_INT) throw HSPERR_TYPE_MISMATCH;
        result = cp2::zgp::z_mat(static_cast<std::int32_t*>(data), bytes > 0 ? (size_t)bytes / sizeof(std::int32_t) : 0,
                                 x, y, z);
        return true;
    }
    if (!strcmp(symbol, "_meshzoom@16") || !strcmp(symbol, "_pos3@16") || !strcmp(symbol, "_line3d@16") ||
        !strcmp(symbol, "_cursor@16")) {
        // Mesh scaling, 3D lines and the mouse cursor (docs/zgp-renderer.md).
        code_next();
        int a = code_getdi(0), b = code_getdi(0), c = code_getdi(0), d = code_getdi(0);
        if (!strcmp(symbol, "_meshzoom@16")) result = cp2::zgp::meshzoom(a, b, c, d);
        else if (!strcmp(symbol, "_pos3@16")) result = cp2::zgp::pos3(a, b, c);
        else if (!strcmp(symbol, "_line3d@16")) result = cp2::zgp::line3d(a, b, c);
        else result = cp2::zgp::cursor(a);
        return true;
    }
    if (!strcmp(symbol, "_meshmaterial@16") || !strcmp(symbol, "_proje@16") || !strcmp(symbol, "_getfrt@16")) {
        // Int-array commands: a mesh material, a projected point, the frame
        // rate (docs/zgp-renderer.md).
        code_next();
        PVal* variable;
        int bytes;
        void* data = code_getvptr(&variable, &bytes);
        int a = code_getdi(0), b = code_getdi(0), c = code_getdi(0);
        if (variable->flag != HSPVAR_FLAG_INT) throw HSPERR_TYPE_MISMATCH;
        std::int32_t* ints = static_cast<std::int32_t*>(data);
        const size_t count = bytes > 0 ? (size_t)bytes / sizeof(std::int32_t) : 0;
        if (!strcmp(symbol, "_meshmaterial@16")) result = cp2::zgp::meshmaterial(ints, count, a, b);
        else if (!strcmp(symbol, "_proje@16")) result = cp2::zgp::proje(ints, count, a, b, c);
        else result = cp2::zgp_negate(cp2::zgp::getfrt(ints, count));  // stat = -return (1 before a report)
        return true;
    }
    if (!strcmp(symbol, "_texwrite24@16")) {
        // 24-bit pixels into a surface's texture (lightmapdraw's buffer 10,
        // through mref 66).
        code_next();
        PVal* variable;
        int bytes;
        void* data = code_getvptr(&variable, &bytes);
        int slot = code_getdi(0), alpha = code_getdi(0), mode = code_getdi(0);
        result = cp2::zgp::texwrite24(static_cast<const unsigned char*>(data), bytes > 0 ? (size_t)bytes : 0, slot,
                                      alpha, mode);
        return true;
    }
    if (!strcmp(symbol, "_getobjang@16") || !strcmp(symbol, "_getobjsize@16") ||
        !strcmp(symbol, "_getanitime@16") || !strcmp(symbol, "_objfreeno@16")) {
        code_next();
        PVal* variable;
        int bytes;
        void* data = code_getvptr(&variable, &bytes);
        int a = code_getdi(0), b = code_getdi(0), c = code_getdi(0);
        if (variable->flag != HSPVAR_FLAG_INT) throw HSPERR_TYPE_MISMATCH;
        std::int32_t* out = static_cast<std::int32_t*>(data);
        const size_t count = bytes > 0 ? (size_t)bytes / sizeof(std::int32_t) : 0;
        if (!strcmp(symbol, "_getobjang@16")) result = cp2::zgp::getobjang(out, count, a);
        else if (!strcmp(symbol, "_getobjsize@16")) result = cp2::zgp::getobjsize(out, count, a);
        else if (!strcmp(symbol, "_getanitime@16")) result = cp2::zgp::getanitime(out, count, a);
        else result = cp2::zgp::objfreeno(out, count, a, b, c);
        return true;
    }
    if (!strcmp(symbol, "_clink@16")) {
        code_next();
        int a = code_getdi(0), b = code_getdi(0);
        code_getdi(0);
        code_getdi(0);
        result = cp2::zgp::clink(a, b);
        return true;
    }
    if (!strcmp(symbol, "_unproje@16")) {
        // The pick ray (docs/zgp-renderer.md, "Objects"); nine ints out.
        code_next();
        PVal* variable;
        int bytes;
        void* data = code_getvptr(&variable, &bytes);
        int x = code_getdi(0), y = code_getdi(0);
        code_getdi(0);
        if (variable->flag != HSPVAR_FLAG_INT) throw HSPERR_TYPE_MISMATCH;
        result = cp2::zgp::unproje(static_cast<std::int32_t*>(data), bytes > 0 ? (size_t)bytes / sizeof(std::int32_t) : 0, x, y);
        return true;
    }
    if (!strcmp(symbol, "_objinst@16")) {
        // The nearest object hit by a ray (docs/zgp-renderer.md, "objinst").
        // Like every old-style DLL entry after binding (OLDDLLINIT in
        // hsp3extlib.cpp), Windows HSP sets stat to the negated return: the
        // object index, 0 for no hit, 52 for a bad first object.
        code_next();
        PVal* variable;
        int bytes;
        void* data = code_getvptr(&variable, &bytes);
        int first = code_getdi(0), count = code_getdi(0), mask = code_getdi(0);
        if (variable->flag != HSPVAR_FLAG_INT) throw HSPERR_TYPE_MISMATCH;
        result = cp2::zgp_negate(cp2::zgp::objinst(static_cast<std::int32_t*>(data),
                                                   bytes > 0 ? (size_t)bytes / sizeof(std::int32_t) : 0,
                                                   first, count, mask));
        return true;
    }
    if (!strcmp(symbol, "_objdel@16")) {
        code_next();
        int a = code_getdi(0);
        for (int i = 0; i < 3; ++i) code_getdi(0);
        result = cp2::zgp::objdel(a);
        return true;
    }
    if (!strcmp(symbol, "_objblend@16") || !strcmp(symbol, "_objhittype@16")) {
        code_next();
        int a = code_getdi(0), b = code_getdi(0);
        code_getdi(0);
        code_getdi(0);
        result = !strcmp(symbol, "_objblend@16") ? cp2::zgp::objblend(a, b) : cp2::zgp::objhittype(a, b);
        return true;
    }
    if (!strcmp(symbol, "_objmeshset@16") || !strcmp(symbol, "_objpos@16")) {
        // Objects (docs/zgp-renderer.md, "Objects").
        code_next();
        int a = code_getdi(0), b = code_getdi(0), c = code_getdi(0), d = code_getdi(0);
        if (!strcmp(symbol, "_objmeshset@16")) result = cp2::zgp::objmeshset(a, b);
        else result = cp2::zgp::objpos(a, b, c, d);
        return true;
    }
    if (!strcmp(symbol, "_texinfoex@16")) {
        code_next();
        PVal* variable;
        int bytes;
        void* data = code_getvptr(&variable, &bytes);
        int index = code_getdi(0);
        code_getdi(0);
        code_getdi(0);
        // The original writes six DWORDs at the variable's address.
        if (variable->flag != HSPVAR_FLAG_INT || bytes < 6 * (int)sizeof(int)) throw HSPERR_TYPE_MISMATCH;
        std::int32_t info[6];
        memcpy(info, data, sizeof(info));
        result = cp2::zgp::texinfoex(info, index);
        memcpy(data, info, sizeof(info));
        return true;
    }
    if (!strcmp(symbol, "_anilibload@16")) {
        // Declared as int, str, int, int; the fourth argument is unused by ZGP.
        code_next();
        int id = code_getdi(0);
        std::string name = code_gets();
        int count = code_getdi(0);
        code_getdi(0);
        result = zgp_anilibload(id, name.c_str(), count);
        return true;
    }
    if (!strcmp(symbol, "GetWindowLongA") || !strcmp(symbol, "SetWindowLongA")) {
        code_next();
        unsigned int handle = (unsigned int)code_geti();
        int index = code_geti();
        bool setting = !strcmp(symbol, "SetWindowLongA");
        int value = setting ? code_geti() : 0;
        result = 0;
        if (refresh_native_window(handle)) {
            if (setting) native_windows.set_window_long(handle, index, value, result);
            else native_windows.get_window_long(handle, index, result);
        }
        return true;
    }
    if (!strcmp(symbol, "joyGetPosEx")) {
        code_next();
        unsigned int id = (unsigned int)code_geti();
        PVal* variable;
        int bytes;
        void* output = code_getvptr(&variable, &bytes);
        result = (int)cp2::joy_get_pos_ex(id, output, bytes);
        return true;
    }
    if (!strcmp(symbol, "GetClientRect") || !strcmp(symbol, "GetWindowRect")) {
        code_next();
        unsigned int handle = (unsigned int)code_geti();
        unsigned int address = (unsigned int)code_geti();
        result = 0;
        if (!refresh_native_window(handle)) return true;
        cp2::Rect32 rectangle{};
        bool valid = !strcmp(symbol, "GetClientRect") ? native_windows.get_client_rect(handle, &rectangle)
                                                       : native_windows.get_window_rect(handle, &rectangle);
        if (valid) {
            try { memcpy(cp2::legacy_resolve(address, sizeof(rectangle)), &rectangle, sizeof(rectangle)); }
            catch (const std::exception&) { throw HSPERR_ILLEGAL_FUNCTION; }
            result = 1;
        }
        return true;
    }
    if (!strcmp(symbol, "_z_len@16")) {
        code_next();
        int x1=code_getdi(0), y1=code_getdi(0), x2=code_getdi(0), y2=code_getdi(0);
        result = cp2::zgp_negate(cp2::zgp_len(x1,y1,x2,y2));
        return true;
    }
    if (!strcmp(symbol, "_z_dir@16")) {
        // The period is setmaxangle's second value (0x1005bd24, default 256),
        // which the original z_dir multiplies by (docs/zgp-compatibility.md).
        code_next();
        int x1=code_getdi(0), y1=code_getdi(0), x2=code_getdi(0), y2=code_getdi(0);
        result = cp2::zgp_negate(cp2::zgp_dir(x1,y1,x2,y2,cp2::zgp::state().angle_period_b));
        return true;
    }
    if (!strcmp(symbol, "_z_len3@16")) {
        code_next();
        PVal* variable;
        int bytes;
        void* input = code_getvptr(&variable, &bytes);
        if (variable->flag != HSPVAR_FLAG_INT || bytes < 3*(int)sizeof(int)) throw HSPERR_TYPE_MISMATCH;
        std::int32_t origin[3];
        memcpy(origin,input,sizeof(origin));
        int x=code_getdi(0), y=code_getdi(0), z=code_getdi(0);
        result = cp2::zgp_negate(cp2::zgp_len3(origin,x,y,z));
        return true;
    }
    bool sine = !strcmp(symbol, "_z_sin@16");
    bool cosine = !strcmp(symbol, "_z_cos@16");
    bool pair = !strcmp(symbol, "_z_scos@16");
    bool difference = !strcmp(symbol, "_z_adif@16");
    if (sine || cosine || pair || difference) {
        code_next();
        PVal* variable;
        int bytes;
        void* output = code_getvptr(&variable, &bytes);
        int count = pair || difference ? 2 : 1;
        // The original writes `count` DWORDs at the variable's address without
        // a size check. A plain int variable (the game's z_adif tempang@m204)
        // holds one; the second DWORD then lands in the 64-byte allocator slot
        // that sbAlloc reserves, which the program can never read. The port
        // stores what the variable holds and drops the rest.
        if (variable->flag != HSPVAR_FLAG_INT || bytes < (int)sizeof(int))
            throw HSPERR_TYPE_MISMATCH;
        int first = code_getdi(0), second = code_getdi(0), turn = code_getdi(0);
        std::int32_t values[2]{};
        if (sine) values[0] = cp2::zgp_sin(first, second, turn);
        if (cosine) values[0] = cp2::zgp_cos(first, second, turn);
        if (pair) cp2::zgp_scos(values, first, second, turn);
        if (difference) {
            if (turn < 0) throw HSPERR_ILLEGAL_FUNCTION;
            cp2::zgp_adif(values, first, second, turn);
        }
        const int stored = bytes / (int)sizeof(int) < count ? bytes / (int)sizeof(int) : count;
        memcpy(output, values, stored*sizeof(int));
        result = 0;
        return true;
    }
    return false;
}

static bool dispatch_native_loader(int cmd, int& result)
{
    const char* symbol = native_symbol(cmd);
    if (!symbol) return false;
    if (!strcmp(symbol, "LoadLibraryA")) {
        code_next();
        const char* name = code_gets();
        result = (int)native_loader.load(name);
        fprintf(stderr, "[CP2 loader] LoadLibraryA(%s) -> %d\n", name, result);
        return true;
    }
    if (!strcmp(symbol, "GetProcAddress")) {
        code_next();
        int handle = code_geti();
        const char* name = code_gets();
        result = (int)native_loader.symbol((cp2::NativeLoader::Handle)handle, name);
        return true;
    }
    if (!strcmp(symbol, "FreeLibrary")) {
        code_next();
        result = native_loader.close((cp2::NativeLoader::Handle)code_geti()) ? 1 : 0;
        return true;
    }
    return false;
}

static void fail_native_import(int cmd)
{
    if (hspctx && hspctx->hsphed && hspctx->mem_finfo && hspctx->mem_mds &&
        cmd >= 0 && cmd < hspctx->hsphed->max_finfo / (int)sizeof(STRUCTDAT)) {
        STRUCTDAT *function = &hspctx->mem_finfo[cmd];
        const char *symbol = hspctx->mem_mds + function->nameidx;
        const char *library = "unknown library";
        if (function->index >= 0 && function->index < hspctx->hsphed->max_linfo / (int)sizeof(LIBDAT)) {
            library = hspctx->mem_mds + hspctx->mem_linfo[function->index].nameidx;
        }
        fprintf(stderr, "[CP2 native import] %s :: %s (index %d) requires an iOS implementation\n", library, symbol, cmd);
    } else {
        fprintf(stderr, "[CP2 native import] unresolved command index %d\n", cmd);
    }
    fflush(stderr);
    throw HSPERR_UNSUPPORTED_FUNCTION;
}

static void *reffunc_dllcmd(int *type_res, int cmd)
{
    if (dispatch_native_loader(cmd, native_loader_result) || dispatch_native_compat(cmd, native_loader_result)) {
        *type_res = HSPVAR_FLAG_INT;
        return &native_loader_result;
    }
    fail_native_import(cmd);
    return NULL;
}

static int cmdfunc_dllcmd( int cmd )
{
	//		cmdfunc : TYPE_DLLCTRL
	//		(拡張DLLコントロールコマンド)
	//
	if (dispatch_native_loader(cmd, native_loader_result) || dispatch_native_compat(cmd, native_loader_result)) {
		hspctx->stat = native_loader_result;
		return RUNMODE_RUN;
	}
	fail_native_import(cmd);
	return RUNMODE_RUN;
}


static int termfunc_dllcmd( int option )
{
	//		termfunc : TYPE_DLLCMD
	//
	return 0;
}

void hsp3typeinit_dllcmd( HSP3TYPEINFO *info )
{
	hspctx = info->hspctx;
	exinfo = info->hspexinfo;
	//type = exinfo->nptype;
	//val = exinfo->npval;
	//exflg = exinfo->npexflg;

	info->cmdfunc = cmdfunc_dllcmd;
	info->reffunc = reffunc_dllcmd;
	info->termfunc = termfunc_dllcmd;

	InitSystemInformation();
}

void hsp3typeinit_dllctrl( HSP3TYPEINFO *info )
{
	info->cmdfunc = cmdfunc_dllcmd;
	info->reffunc = reffunc_dllcmd;
}


/*------------------------------------------------------------*/
/*
		Sysinfo, getdir service
*/
/*------------------------------------------------------------*/

char* hsp3ext_getdir(int id)
{
	char *p = "";
	switch (id) {
	case 0:				//    カレント(現在の)ディレクトリ
        p = const_cast<char*>(cp2_asset_current_directory());
        break;
	case 1:				//    HSPの実行ファイルがあるディレクトリ
	case 2:				//    Windowsディレクトリ
	case 3:				//    Windowsのシステムディレクトリ
	case 4:				//    コマンドライン文字列
	case 5:				//    HSPTV素材があるディレクトリ
	case 0x10005:			//    マイドキュメント
		break;
	case 6:				//    ランゲージコード
		p = hspctx->langcode;
		break;
	default:
		throw HSPERR_ILLEGAL_FUNCTION;
	}
	return p;
}



char *hsp3ext_sysinfo(int p2, int* res, char* outbuf)
{
	//		System strings get
	//
	int fl;
	char *p1;
	fl = HSPVAR_FLAG_STR;
	p1 = outbuf;
	*p1=0;

	switch(p2) {
	case 0:
#ifdef HSPIOS
        gb_getSysVer( p1 );
#endif
        break;
	case 1:
		break;
	case 2:
#ifdef HSPIOS
        gb_getSysModel( p1 );
#endif
		break;
	case 3:
		fl = HSPVAR_FLAG_INT;
		*(int*)p1 = hspctx->language;
		break;
	default:
		return NULL;
	}
	*res = fl;
	return p1;
}


void hsp3ext_execfile(char* msg, char* option, int mode)
{
#ifdef HSPNDK
	j_callActivity( msg, option, mode );
#endif
#ifdef HSPIOS
    gb_exec( mode, msg );
#endif
}

