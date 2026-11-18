#pragma once
// OpenGL ES 1 implementation of the Direct3D 8 device behind ZGP (iOS).
//
// GlDevice keeps exactly the state StateDevice keeps (so recorded traces are
// unchanged) and additionally draws into an offscreen color + depth
// framebuffer of the back-buffer size on HSP's existing ES1 EAGL context.
// Present scales that image into HSP's view framebuffer and presents it
// through iOSBridge (cp2_view_framebuffer / cp2_view_present); no second view
// or context is created. See docs/zgp-renderer.md, "OpenGL ES device".
//
// Implemented: Clear (viewport-limited), viewport scissoring, Present,
// DrawPrimitiveUP with pre-transformed vertices (FVF 0x144), alpha blending
// with D3D blend factors and operations (OES_blend_subtract), alpha test,
// depth test/write, culling, texture stage 0 (color/alpha operations through
// GL_COMBINE, filters, address modes) and texture upload from StateTexture
// levels. 3D: DrawIndexedPrimitive and DrawPrimitiveUP with untransformed
// FVF vertices (position, blend weights, palette indices, normal, diffuse,
// texture coordinates), the world/view/projection transforms (Direct3D's
// row-major matrices loaded as GL's column-major transposes, clip z mapped
// from [0, w] to [-w, w]), fixed-function lighting (lights, material, global
// ambient, specular), fog, the viewport's depth range, and vertex blending
// (VERTEXBLEND with WORLDMATRIX(n) and INDEXEDVERTEXBLENDENABLE) computed on
// the CPU, since ES1's matrix palette holds fewer than the game's 30 bones.
#include "zgp_device.hpp"
#include "input_state.hpp"
#include <OpenGLES/ES1/gl.h>
#include <OpenGLES/ES1/glext.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

extern "C" void cp2_view_framebuffer(unsigned int* framebuffer, unsigned int* renderbuffer);
extern "C" void cp2_view_present(void);
// The on-screen touch controls, drawn over each presented frame (iOSBridge.mm).
extern "C" void cp2_controls_draw(int view_width, int view_height);

namespace cp2 {
namespace zgp {

class GlDevice : public StateDevice {
public:
    ~GlDevice() override {
        StateBuffer::destroy_hook() = nullptr;
        release_gl();
    }

    // With `native`, the back buffer is rendered at the view's pixel
    // resolution: the game's Direct3D coordinates (back buffer, viewport,
    // scissor, pre-transformed vertices) stay those of the 960x540 device
    // and only the GL target is larger, so the image is drawn at the screen's
    // native resolution instead of being upscaled from 960x540. The 3D
    // self-test uses the Direct3D size itself (native false).
    bool create(const d3d8::PresentParameters& p, std::string& error, bool native = false) {
        width_ = static_cast<GLsizei>(p.width);
        height_ = static_cast<GLsizei>(p.height);
        GLint max_size = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
        if (width_ <= 0 || height_ <= 0 || width_ > max_size || height_ > max_size) {
            error = "back buffer size exceeds the OpenGL ES texture limit";
            return false;
        }
        target_width_ = width_;
        target_height_ = height_;
        if (native) {
            GLuint view_framebuffer = 0, view_renderbuffer = 0;
            cp2_view_framebuffer(&view_framebuffer, &view_renderbuffer);
            GLint view_width = 0, view_height = 0, previous = 0;
            if (view_renderbuffer) {
                glGetIntegerv(GL_RENDERBUFFER_BINDING_OES, &previous);
                glBindRenderbufferOES(GL_RENDERBUFFER_OES, view_renderbuffer);
                glGetRenderbufferParameterivOES(GL_RENDERBUFFER_OES, GL_RENDERBUFFER_WIDTH_OES, &view_width);
                glGetRenderbufferParameterivOES(GL_RENDERBUFFER_OES, GL_RENDERBUFFER_HEIGHT_OES, &view_height);
                glBindRenderbufferOES(GL_RENDERBUFFER_OES, static_cast<GLuint>(previous));
            }
            // The largest aspect-preserving size that fits the view (never
            // below the Direct3D size).
            const float fit = std::min(static_cast<float>(view_width) / width_, static_cast<float>(view_height) / height_);
            if (fit > 1.0f) {
                target_width_ = std::min<GLsizei>(max_size, static_cast<GLsizei>(width_ * fit + 0.5f));
                target_height_ = std::min<GLsizei>(max_size, static_cast<GLsizei>(height_ * fit + 0.5f));
            }
        }
        scale_x_ = static_cast<float>(target_width_) / width_;
        scale_y_ = static_cast<float>(target_height_) / height_;
        const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        blend_subtract_ = extensions && std::strstr(extensions, "GL_OES_blend_subtract");
        GLint previous_framebuffer = 0, previous_renderbuffer = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING_OES, &previous_framebuffer);
        glGetIntegerv(GL_RENDERBUFFER_BINDING_OES, &previous_renderbuffer);
        glGenTextures(1, &color_);
        glBindTexture(GL_TEXTURE_2D, color_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, target_width_, target_height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffersOES(1, &framebuffer_);
        glBindFramebufferOES(GL_FRAMEBUFFER_OES, framebuffer_);
        glFramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_TEXTURE_2D, color_, 0);
        glGenRenderbuffersOES(1, &depth_);
        glBindRenderbufferOES(GL_RENDERBUFFER_OES, depth_);
        glRenderbufferStorageOES(GL_RENDERBUFFER_OES, GL_DEPTH_COMPONENT16_OES, target_width_, target_height_);
        glFramebufferRenderbufferOES(GL_FRAMEBUFFER_OES, GL_DEPTH_ATTACHMENT_OES, GL_RENDERBUFFER_OES, depth_);
        const bool complete = glCheckFramebufferStatusOES(GL_FRAMEBUFFER_OES) == GL_FRAMEBUFFER_COMPLETE_OES;
        if (complete) {
            // A new Direct3D back buffer has undefined contents; start black.
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0, 0, 0, 1);
            glClearDepthf(1.0f);
            glDepthMask(GL_TRUE);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
        glBindFramebufferOES(GL_FRAMEBUFFER_OES, static_cast<GLuint>(previous_framebuffer));
        glBindRenderbufferOES(GL_RENDERBUFFER_OES, static_cast<GLuint>(previous_renderbuffer));
        if (!complete) {
            release_gl();
            error = "offscreen framebuffer incomplete";
            return false;
        }
        StateBuffer::destroy_hook() = [this](StateBuffer* b) { buffers_.erase(b); };
        StateTexture::destroy_hook() = [](StateTexture* t) {
            if (t->platform_name) {
                GLuint name = t->platform_name;
                glDeleteTextures(1, &name);
                t->platform_name = 0;
            }
        };
        std::fprintf(stderr, "[CP2 GL] ZGP device: %dx%d back buffer rendered at %dx%d, blend subtract %s\n", width_,
                     height_, target_width_, target_height_, blend_subtract_ ? "yes" : "no");
        if (const char* dump = std::getenv("CP2_DUMP_FRAME")) dump_frame_ = static_cast<std::uint32_t>(std::atoi(dump));
        // Test-only scripted key presses for unattended simulator runs.
        std::string script_error;
        if (!input_script_.parse(std::getenv("CP2_INPUT_SCRIPT"), script_error))
            std::fprintf(stderr, "[CP2 input] %s; scripted input disabled\n", script_error.c_str());
        else if (!input_script_.empty())
            std::fprintf(stderr, "[CP2 input] test input script: %s\n", std::getenv("CP2_INPUT_SCRIPT"));
        return true;
    }

    d3d8::HResult clear(std::uint32_t count, const d3d8::Rect* rects, std::uint32_t flags, std::uint32_t color,
                        float z, std::uint32_t stencil) override {
        const d3d8::HResult r = StateDevice::clear(count, rects, flags, color, z, stencil);
        if (r != d3d8::kOk) return r;
        bind_target();
        GLbitfield mask = 0;
        if (flags & d3d8::kClearTarget) {
            glClearColor(((color >> 16) & 0xff) / 255.0f, ((color >> 8) & 0xff) / 255.0f, (color & 0xff) / 255.0f,
                         (color >> 24) / 255.0f);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            mask |= GL_COLOR_BUFFER_BIT;
        }
        if (flags & d3d8::kClearZBuffer) {
            glClearDepthf(z);
            glDepthMask(GL_TRUE);
            mask |= GL_DEPTH_BUFFER_BIT;
        }
        // Direct3D clears the viewport, or the given rectangles within it.
        glEnable(GL_SCISSOR_TEST);
        if (!count) {
            scissor(viewport.x, viewport.y, viewport.width, viewport.height);
            glClear(mask);
        } else {
            for (std::uint32_t i = 0; i < count; ++i) {
                const d3d8::Rect& rc = rects[i];
                const std::int32_t l = std::max<std::int32_t>(rc.left, static_cast<std::int32_t>(viewport.x));
                const std::int32_t t = std::max<std::int32_t>(rc.top, static_cast<std::int32_t>(viewport.y));
                const std::int32_t rr = std::min<std::int32_t>(rc.right, static_cast<std::int32_t>(viewport.x + viewport.width));
                const std::int32_t b = std::min<std::int32_t>(rc.bottom, static_cast<std::int32_t>(viewport.y + viewport.height));
                if (rr <= l || b <= t) continue;
                scissor(static_cast<std::uint32_t>(l), static_cast<std::uint32_t>(t), static_cast<std::uint32_t>(rr - l),
                        static_cast<std::uint32_t>(b - t));
                glClear(mask);
            }
        }
        return r;
    }

    d3d8::HResult present(const d3d8::Rect* source, const d3d8::Rect* destination) override {
        const d3d8::HResult r = StateDevice::present(source, destination);
        report_frame();
        GLuint view_framebuffer = 0, view_renderbuffer = 0;
        cp2_view_framebuffer(&view_framebuffer, &view_renderbuffer);
        if (!view_framebuffer || !view_renderbuffer) return r;
        glBindFramebufferOES(GL_FRAMEBUFFER_OES, view_framebuffer);
        glBindRenderbufferOES(GL_RENDERBUFFER_OES, view_renderbuffer);
        GLint view_width = 0, view_height = 0;
        glGetRenderbufferParameterivOES(GL_RENDERBUFFER_OES, GL_RENDERBUFFER_WIDTH_OES, &view_width);
        glGetRenderbufferParameterivOES(GL_RENDERBUFFER_OES, GL_RENDERBUFFER_HEIGHT_OES, &view_height);
        glViewport(0, 0, view_width, view_height);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 1);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT);
        // The whole back buffer, aspect-fitted (letterboxed) into the view.
        // Direct3D's COPY swap effect also keeps the back buffer, as here.
        // At native resolution the target maps 1:1 onto whole view pixels.
        const float scale = std::min(static_cast<float>(view_width) / target_width_,
                                     static_cast<float>(view_height) / target_height_);
        const float w = target_width_ * scale, h = target_height_ * scale;
        const float x0 = std::floor((view_width - w) * 0.5f), y0 = std::floor((view_height - h) * 0.5f);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrthof(0, static_cast<GLfloat>(view_width), 0, static_cast<GLfloat>(view_height), -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glDisable(GL_ALPHA_TEST);
        glDisable(GL_LIGHTING);
        glDisable(GL_FOG);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, color_);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        const GLfloat positions[] = {x0, y0, x0 + w, y0, x0, y0 + h, x0 + w, y0 + h};
        const GLfloat coordinates[] = {0, 0, 1, 0, 0, 1, 1, 1};
        glDisableClientState(GL_COLOR_ARRAY);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, positions);
        glTexCoordPointer(2, GL_FLOAT, 0, coordinates);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        cp2_controls_draw(view_width, view_height);
        cp2_view_present();
        ++frames_;
        if (!input_script_.empty()) input_script_.apply(input::keys(), static_cast<int>(frames_));
        if (frames_ == 1) std::fprintf(stderr, "[CP2 GL] first frame presented (%dx%d view)\n", view_width, view_height);
        return r;
    }

    d3d8::HResult draw_primitive_up(std::uint32_t type, std::uint32_t primitives, const void* data,
                                    std::uint32_t stride) override {
        const d3d8::HResult r = StateDevice::draw_primitive_up(type, primitives, data, stride);
        if (r != d3d8::kOk) return r;
        ++frame_draws_;
        if (vertex_shader != d3d8::kFvfScreen) {
            // Untransformed vertices (objdraw's boards, line3d): the 3D path.
            if ((vertex_shader & 0xe) == 4 || stride < d3d8::fvf_stride(vertex_shader)) {
                ++frame_skipped_;
                report_once("vertex format", vertex_shader);
                return r;
            }
            const std::uint32_t count = d3d8::vertex_count(type, primitives);
            decode(static_cast<const unsigned char*>(data), count, vertex_shader, stride, user_vertices_);
            draw_3d(type, user_vertices_, count, nullptr);
            return r;
        }
        if (stride != sizeof(d3d8::ScreenVertex)) {
            ++frame_skipped_;
            report_once("screen vertex stride", stride);
            return r;
        }
        const std::uint32_t count = d3d8::vertex_count(type, primitives);
        GLenum mode;
        switch (type) {
        case d3d8::kPointList: mode = GL_POINTS; break;
        case d3d8::kLineList: mode = GL_LINES; break;
        case d3d8::kLineStrip: mode = GL_LINE_STRIP; break;
        case d3d8::kTriangleList: mode = GL_TRIANGLES; break;
        case d3d8::kTriangleStrip: mode = GL_TRIANGLE_STRIP; break;
        default: mode = GL_TRIANGLE_FAN; break;
        }
        const d3d8::ScreenVertex* v = static_cast<const d3d8::ScreenVertex*>(data);
        positions_.resize(count * 3);
        colors_.resize(count * 4);
        coordinates_.resize(count * 2);
        for (std::uint32_t i = 0; i < count; ++i) {
            positions_[i * 3 + 0] = v[i].x;
            positions_[i * 3 + 1] = v[i].y;
            positions_[i * 3 + 2] = v[i].z;
            colors_[i * 4 + 0] = static_cast<GLubyte>(v[i].color >> 16);  // D3DCOLOR is ARGB
            colors_[i * 4 + 1] = static_cast<GLubyte>(v[i].color >> 8);
            colors_[i * 4 + 2] = static_cast<GLubyte>(v[i].color);
            colors_[i * 4 + 3] = static_cast<GLubyte>(v[i].color >> 24);
            std::memcpy(&coordinates_[i * 2 + 0], &v[i].u, 4);
            std::memcpy(&coordinates_[i * 2 + 1], &v[i].v, 4);
        }
        bind_target();
        apply_screen_state();
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, positions_.data());
        glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors_.data());
        if (textured_) {
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glTexCoordPointer(2, GL_FLOAT, 0, coordinates_.data());
        } else {
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        }
        if (textured_) ++frame_textured_;
        if (frames_ == 1 && frame_draws_ <= 4) report_draw(type, count, v);
        if (dump_frame_ && frames_ + 1 == dump_frame_) {
            dump_2d(type, count, v);
            dump_before();
        }
        glDrawArrays(mode, 0, static_cast<GLsizei>(count));
        if (dump_frame_ && frames_ + 1 == dump_frame_) dump_after();
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        return r;
    }


    // Indexed triangles from the bound stream and indices (objdraw meshes,
    // CSMesh subsets). Vertices are decoded from the vertex buffer's FVF;
    // vertex blending (VERTEXBLEND, INDEXEDVERTEXBLENDENABLE) runs on the CPU.
    d3d8::HResult draw_indexed_primitive(std::uint32_t type, std::uint32_t min_index, std::uint32_t vertices,
                                         std::uint32_t start_index, std::uint32_t primitives) override {
        const d3d8::HResult r =
            StateDevice::draw_indexed_primitive(type, min_index, vertices, start_index, primitives);
        if (r != d3d8::kOk) return r;
        ++frame_draws_;
        StateBuffer* vb = static_cast<StateBuffer*>(stream);
        StateBuffer* ib = static_cast<StateBuffer*>(index_buffer);
        const std::uint32_t fvf = vertex_shader;
        if ((fvf & 0xe) == 4 || !stream_stride || stream_stride < d3d8::fvf_stride(fvf)) {
            ++frame_skipped_;
            report_once("indexed vertex format", fvf);
            return r;
        }
        const std::uint32_t count = d3d8::vertex_count(type, primitives);
        const std::size_t index_size = ib->format() == d3d8::kFormatIndex32 ? 4 : 2;
        if ((static_cast<std::size_t>(start_index) + count) * index_size > ib->bytes().size()) {
            ++frame_skipped_;
            report_once("index range beyond the index buffer, start", start_index);
            return r;
        }
        const std::size_t available = vb->bytes().size() / stream_stride;
        Decoded& d = decoded_buffer(*vb, fvf, stream_stride, available);
        // Indices relative to the base vertex, checked against the buffer.
        indices_.resize(count);
        const unsigned char* raw = ib->bytes().data() + static_cast<std::size_t>(start_index) * index_size;
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t index;
            if (index_size == 4) std::memcpy(&index, raw + 4 * i, 4);
            else {
                std::uint16_t s;
                std::memcpy(&s, raw + 2 * i, 2);
                index = s;
            }
            index += base_vertex;
            if (index >= available || index > 0xffff) {
                ++frame_skipped_;
                report_once("vertex index beyond the vertex buffer", index);
                return r;
            }
            indices_[i] = static_cast<GLushort>(index);
        }
        draw_3d(type, d, count, indices_.data());
        return r;
    }

    std::uint32_t frames() const { return frames_; }

    // The GL target's pixels (RGBA rows, bottom row first; the Direct3D size
    // unless the device was created with `native`).
    void read_back(std::vector<unsigned char>& pixels) {
        GLint previous = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING_OES, &previous);
        glBindFramebufferOES(GL_FRAMEBUFFER_OES, framebuffer_);
        pixels.resize(static_cast<std::size_t>(target_width_) * target_height_ * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, target_width_, target_height_, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindFramebufferOES(GL_FRAMEBUFFER_OES, static_cast<GLuint>(previous));
    }

    // Renders known 3D scenes and checks the pixels (CP2_GRAPHICS_SELFTEST).
    static bool self_test_3d();

private:
    // Diagnostics for the first draws of the second frame: primitive, vertices
    // (position, ARGB color, UV), the viewport and GL scissor, the depth,
    // alpha-test and blend states, texture stage 0 and the bound texture
    // (format, size, levels, and how many level-0 texels have non-zero alpha).
    void report_draw(std::uint32_t type, std::uint32_t count, const d3d8::ScreenVertex* v) {
        GLint box[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_SCISSOR_BOX, box);
        std::fprintf(stderr,
                     "[CP2 GL] draw %u: type %u, %u vertices; viewport %u,%u %ux%u, scissor %d,%d %dx%d; "
                     "Z %u/%u/%u, alpha test %u ref %u func %u, blend %u %u/%u op %u, cull %u\n",
                     frame_draws_, type, count, viewport.x, viewport.y, viewport.width, viewport.height, box[0], box[1],
                     box[2], box[3], render_state[7], render_state[14], render_state[23], render_state[15],
                     render_state[24], render_state[25], render_state[27], render_state[19], render_state[20],
                     render_state[171], render_state[22]);
        for (std::uint32_t i = 0; i < count && i < 4; ++i) {
            float u, w;
            std::memcpy(&u, &v[i].u, 4);
            std::memcpy(&w, &v[i].v, 4);
            std::fprintf(stderr, "[CP2 GL]   v%u: %.2f %.2f z %.3f rhw %.3f color %08x uv %.4f %.4f\n", i, v[i].x, v[i].y,
                         v[i].z, v[i].rhw, v[i].color, u, w);
        }
        const std::uint32_t* s = stage_state[0];
        std::fprintf(stderr, "[CP2 GL]   stage 0: color %u(%u,%u) alpha %u(%u,%u) filter %u/%u/%u address %u/%u\n", s[1],
                     s[2], s[3], s[4], s[5], s[6], s[16], s[17], s[18], s[13], s[14]);
        StateTexture* t = static_cast<StateTexture*>(texture[0]);
        if (!t) {
            std::fprintf(stderr, "[CP2 GL]   no texture\n");
            return;
        }
        const StateTexture::Level& l0 = t->levels()[0];
        std::size_t alpha = 0, texels = 0;
        if (t->format() == 21) {
            for (std::size_t i = 3; i < l0.bytes.size(); i += 4) {
                ++texels;
                if (l0.bytes[i]) ++alpha;
            }
        }
        std::fprintf(stderr, "[CP2 GL]   texture #%u format %u %ux%u, %zu levels, uploaded %s, %zu/%zu texels with alpha\n",
                     t->trace_id, t->format(), l0.width, l0.height, t->levels().size(), t->platform_name ? "yes" : "no",
                     alpha, texels);
    }

    // Diagnostics for frames 1-3, 10, 30, 60, then every 120th: the frame's
    // draw calls (textured, skipped for an unimplemented vertex format), the
    // GL error state and a scan of the back buffer (pixels that are not black
    // and their bounding box), read before the frame is presented.
    void report_frame() {
        const std::uint32_t n = frames_ + 1;
        if (n <= 3 || n == 10 || n == 30 || n == 60 || n % 120 == 0) {
            GLint previous = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING_OES, &previous);
            glBindFramebufferOES(GL_FRAMEBUFFER_OES, framebuffer_);
            const GLsizei tw = target_width_, th = target_height_;
            std::vector<unsigned char> pixels(static_cast<std::size_t>(tw) * th * 4);
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            glReadPixels(0, 0, tw, th, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            const GLenum error = glGetError();
            glBindFramebufferOES(GL_FRAMEBUFFER_OES, static_cast<GLuint>(previous));
            // Counted and bounded in Direct3D back-buffer pixels: each one is
            // sampled at its centre in the (possibly larger) GL target.
            std::size_t lit = 0;
            GLsizei x0 = width_, y0 = height_, x1 = -1, y1 = -1;
            for (GLsizei y = 0; y < height_; ++y)
                for (GLsizei x = 0; x < width_; ++x) {
                    const GLsizei gx = std::min<GLsizei>(tw - 1, static_cast<GLsizei>((x + 0.5f) * scale_x_));
                    // GL rows start at the bottom; y is Direct3D's top-left row.
                    const GLsizei gy = std::min<GLsizei>(th - 1, static_cast<GLsizei>((height_ - 1 - y + 0.5f) * scale_y_));
                    const unsigned char* p = &pixels[(static_cast<std::size_t>(gy) * tw + gx) * 4];
                    if (p[0] < 8 && p[1] < 8 && p[2] < 8) continue;
                    ++lit;
                    x0 = std::min(x0, x); x1 = std::max(x1, x);
                    y0 = std::min(y0, y); y1 = std::max(y1, y);
                }
            std::fprintf(stderr,
                         "[CP2 GL] frame %u: %u draws (%u 3D, %u textured, %u skipped), GL error 0x%x, back buffer %zu "
                         "non-black pixels",
                         n, frame_draws_, frame_3d_, frame_textured_, frame_skipped_, static_cast<unsigned>(error), lit);
            if (lit) std::fprintf(stderr, " in (%d,%d)-(%d,%d)", x0, y0, x1, y1);
            // Frames per second since the previous report.
            const auto now = std::chrono::steady_clock::now();
            if (reported_frame_)
                std::fprintf(stderr, ", %.1f fps",
                             (n - reported_frame_) / std::chrono::duration<double>(now - reported_at_).count());
            reported_frame_ = n;
            reported_at_ = now;
            std::fprintf(stderr, "\n");
        }
        frame_draws_ = frame_textured_ = frame_skipped_ = frame_3d_ = 0;
    }

    void release_gl() {
        if (depth_) glDeleteRenderbuffersOES(1, &depth_);
        if (framebuffer_) glDeleteFramebuffersOES(1, &framebuffer_);
        if (color_) glDeleteTextures(1, &color_);
        depth_ = framebuffer_ = color_ = 0;
    }

    void report_once(const char* what, std::uint32_t value) {
        const std::string key = std::string(what) + std::to_string(value);
        if (reported_.insert(key).second)
            std::fprintf(stderr, "[CP2 GL] drawing with %s %u is not implemented; the draw is skipped\n", what,
                         static_cast<unsigned>(value));
    }

    // Direct3D rectangles have a top-left origin; the GL target a bottom-left one.
    void scissor(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) {
        const TargetRect b = target_box(x, y, w, h);
        glScissor(b.x, b.y, b.w, b.h);
    }

    void bind_target() {
        glBindFramebufferOES(GL_FRAMEBUFFER_OES, framebuffer_);
        glViewport(0, 0, target_width_, target_height_);
    }

    // A Direct3D rectangle (top-left origin, back-buffer pixels) as a GL
    // rectangle of the target (bottom-left origin, scaled, edges rounded).
    struct TargetRect {
        GLint x, y;
        GLsizei w, h;
    };
    TargetRect target_box(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) const {
        const GLint left = static_cast<GLint>(x * scale_x_ + 0.5f), right = static_cast<GLint>((x + w) * scale_x_ + 0.5f);
        const GLint top = static_cast<GLint>(y * scale_y_ + 0.5f), bottom = static_cast<GLint>((y + h) * scale_y_ + 0.5f);
        return TargetRect{left, target_height_ - bottom, right - left, bottom - top};
    }

    static GLenum blend_factor(std::uint32_t f) {
        switch (f) {
        case 1: return GL_ZERO;
        case 2: return GL_ONE;
        case 3: return GL_SRC_COLOR;
        case 4: return GL_ONE_MINUS_SRC_COLOR;
        case 5: return GL_SRC_ALPHA;
        case 6: return GL_ONE_MINUS_SRC_ALPHA;
        case 7: return GL_DST_ALPHA;
        case 8: return GL_ONE_MINUS_DST_ALPHA;
        case 9: return GL_DST_COLOR;
        case 10: return GL_ONE_MINUS_DST_COLOR;
        case 11: return GL_SRC_ALPHA_SATURATE;
        default: return GL_ONE;
        }
    }
    static GLenum compare(std::uint32_t f) {
        switch (f) {
        case 1: return GL_NEVER;
        case 2: return GL_LESS;
        case 3: return GL_EQUAL;
        case 4: return GL_LEQUAL;
        case 5: return GL_GREATER;
        case 6: return GL_NOTEQUAL;
        case 7: return GL_GEQUAL;
        default: return GL_ALWAYS;
        }
    }

    // State for pre-transformed vertices: Direct3D screen coordinates have
    // pixel centers at integers and a top-left origin; z maps to depth 0..1.
    void apply_screen_state() {
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrthof(-0.5f, width_ - 0.5f, height_ - 0.5f, -0.5f, 0.0f, -1.0f);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glDisable(GL_LIGHTING);  // pre-transformed vertices are not lit
        glDisable(GL_FOG);
        glShadeModel(render_state[9] == 1 ? GL_FLAT : GL_SMOOTH);
        apply_pixel_state();
    }

    // State shared by 2D and 3D draws: the viewport scissor, depth, culling,
    // alpha test, blending and texture stage 0.
    void apply_pixel_state() {
        glEnable(GL_SCISSOR_TEST);
        scissor(viewport.x, viewport.y, viewport.width, viewport.height);
        // Direct3D 8 lines and points are one back-buffer pixel wide.
        glLineWidth(std::max(1.0f, scale_y_));
        glPointSize(std::max(1.0f, scale_y_));
        // Depth (ZENABLE 7, ZWRITEENABLE 14, ZFUNC 23).
        if (render_state[7]) {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(compare(render_state[23] ? render_state[23] : 4));
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        glDepthMask(render_state[14] ? GL_TRUE : GL_FALSE);
        // Culling (CULLMODE 22: 2 = CW, 3 = CCW culled). Both APIs judge the
        // winding as the triangle appears on screen; the projection's y flip
        // only maps Direct3D's top-left origin to GL's bottom-left one and the
        // image stays upright, so a clockwise screen triangle is clockwise for
        // GL too. CULL_CCW keeps clockwise faces: front = CW. (Mapping it the
        // other way culled every sprite: the game's quads are clockwise.)
        if (render_state[22] == 2 || render_state[22] == 3) {
            glEnable(GL_CULL_FACE);
            glFrontFace(render_state[22] == 3 ? GL_CW : GL_CCW);
            glCullFace(GL_BACK);
        } else {
            glDisable(GL_CULL_FACE);
        }
        // Alpha test (ALPHATESTENABLE 15, ALPHAREF 24, ALPHAFUNC 25).
        if (render_state[15]) {
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(compare(render_state[25]), (render_state[24] & 0xff) / 255.0f);
        } else {
            glDisable(GL_ALPHA_TEST);
        }
        // Blending (ALPHABLENDENABLE 27, SRCBLEND 19, DESTBLEND 20, BLENDOP 171).
        if (render_state[27]) {
            glEnable(GL_BLEND);
            glBlendFunc(blend_factor(render_state[19]), blend_factor(render_state[20]));
            if (blend_subtract_) {
                const std::uint32_t op = render_state[171];
                glBlendEquationOES(op == 2 ? GL_FUNC_SUBTRACT_OES
                                           : op == 3 ? GL_FUNC_REVERSE_SUBTRACT_OES : GL_FUNC_ADD_OES);
            } else if (render_state[171] == 2 || render_state[171] == 3) {
                report_once("blend operation", render_state[171]);
            }
        } else {
            glDisable(GL_BLEND);
        }
        apply_texture_stage();
    }

    static GLenum combine_operation(std::uint32_t op) {
        switch (op) {
        case 2: return GL_REPLACE;  // SELECTARG1
        case 3: return GL_REPLACE;  // SELECTARG2 (source chosen below)
        case 7: return GL_ADD;
        case 8: return GL_ADD_SIGNED;
        case 10: return GL_SUBTRACT;
        default: return GL_MODULATE;  // MODULATE, MODULATE2X, MODULATE4X
        }
    }
    static GLenum combine_source(std::uint32_t argument) {
        switch (argument & 0xf) {
        case 0: return GL_PRIMARY_COLOR;  // D3DTA_DIFFUSE
        case 1: return GL_PREVIOUS;       // D3DTA_CURRENT (stage 0: diffuse)
        case 3: return GL_CONSTANT;       // D3DTA_TFACTOR
        default: return GL_TEXTURE;       // D3DTA_TEXTURE
        }
    }

    // Texture stage 0: COLOROP 1, COLORARG1 2, COLORARG2 3, ALPHAOP 4,
    // ALPHAARG1 5, ALPHAARG2 6, ADDRESSU 13, ADDRESSV 14, MAGFILTER 16,
    // MINFILTER 17, MIPFILTER 18.
    void apply_texture_stage() {
        StateTexture* t = static_cast<StateTexture*>(texture[0]);
        const std::uint32_t* s = stage_state[0];
        textured_ = t && s[1] != 1 && upload(*t);
        if (!textured_) {
            glDisable(GL_TEXTURE_2D);
            return;
        }
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, t->platform_name);
        const bool mipmapped = t->levels().size() > 1;
        const bool power_of_two = !(t->levels()[0].width & (t->levels()[0].width - 1)) &&
                                  !(t->levels()[0].height & (t->levels()[0].height - 1));
        const bool mag_linear = s[16] >= 2, min_linear = s[17] >= 2;
        GLenum min_filter = min_linear ? GL_LINEAR : GL_NEAREST;
        if (mipmapped && s[18] == 1) min_filter = min_linear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
        if (mipmapped && s[18] >= 2) min_filter = min_linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_linear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
        // Non-power-of-two textures can only clamp in OpenGL ES 1.
        auto wrap = [&](std::uint32_t address) {
            return (!power_of_two || address == 3 || address == 4) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
        };
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap(s[13]));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap(s[14]));
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        const std::uint32_t color_op = s[1] ? s[1] : 4, alpha_op = s[4] ? s[4] : 2;
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, combine_operation(color_op));
        glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, combine_source(color_op == 3 ? s[3] : s[2]));
        glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, combine_source(s[3]));
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
        glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, color_op == 5 ? 2.0f : color_op == 6 ? 4.0f : 1.0f);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, combine_operation(alpha_op));
        glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, combine_source(alpha_op == 3 ? s[6] : s[5]));
        glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_ALPHA, combine_source(s[6]));
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
        glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, alpha_op == 5 ? 2.0f : alpha_op == 6 ? 4.0f : 1.0f);
        if (render_state[60]) {  // D3DRS_TEXTUREFACTOR
            const std::uint32_t f = render_state[60];
            const GLfloat factor[4] = {((f >> 16) & 0xff) / 255.0f, ((f >> 8) & 0xff) / 255.0f, (f & 0xff) / 255.0f,
                                       (f >> 24) / 255.0f};
            glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, factor);
        }
    }

    // Uploads every level when the texture's contents changed. Formats map to
    // GL as: A8R8G8B8 -> BGRA, X8R8G8B8 -> BGRA with alpha forced to 1,
    // L8 -> LUMINANCE, A8L8 -> LUMINANCE_ALPHA, A8 -> ALPHA, R5G6B5 -> RGB 565.
    bool upload(StateTexture& t) {
        if (t.platform_name && t.platform_revision == t.revision) return true;
        GLenum format, type = GL_UNSIGNED_BYTE, internal;
        switch (t.format()) {
        case 21: case 22: format = GL_BGRA_EXT; internal = GL_RGBA; break;
        case 50: format = internal = GL_LUMINANCE; break;
        case 51: format = internal = GL_LUMINANCE_ALPHA; break;
        case 28: format = internal = GL_ALPHA; break;
        case 23: format = internal = GL_RGB; type = GL_UNSIGNED_SHORT_5_6_5; break;
        default:
            report_once("texture format", t.format());
            return false;
        }
        if (!t.platform_name) {
            GLuint name = 0;
            glGenTextures(1, &name);
            t.platform_name = name;
        }
        glBindTexture(GL_TEXTURE_2D, t.platform_name);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);  // rows are DWORD-padded
        std::vector<unsigned char> opaque;
        for (std::size_t l = 0; l < t.levels().size(); ++l) {
            const StateTexture::Level& level = t.levels()[l];
            const void* pixels = level.bytes.data();
            if (t.format() == 22) {
                opaque = level.bytes;
                for (std::size_t i = 3; i < opaque.size(); i += 4) opaque[i] = 0xff;
                pixels = opaque.data();
            }
            glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(l), static_cast<GLint>(internal),
                         static_cast<GLsizei>(level.width), static_cast<GLsizei>(level.height), 0, format, type,
                         pixels);
        }
        t.platform_revision = t.revision;
        return true;
    }


    // -----------------------------------------------------------------------
    // 3D drawing (docs/zgp-renderer.md, "OpenGL ES device").

    // Vertices decoded from an FVF: position, optional normal, diffuse color
    // (RGBA bytes), first texture coordinate set, blend weights and palette
    // indices.
    struct Decoded {
        std::size_t count = 0;
        std::vector<GLfloat> positions, normals, coordinates;
        std::vector<GLubyte> colors;
        std::vector<float> weights;           // `stored` per vertex
        std::vector<std::uint8_t> palette;    // 4 per vertex (LASTBETA_UBYTE4)
        std::uint32_t stored = 0;             // blend weights per vertex
        bool indexed = false;
        StateBuffer* buffer = nullptr;
        std::uint32_t revision = ~0u, fvf = 0, stride = 0;
    };

    static void decode(const unsigned char* data, std::size_t count, std::uint32_t fvf, std::uint32_t stride,
                       Decoded& d) {
        d.count = count;
        const std::uint32_t position = fvf & 0xe;
        const std::uint32_t betas = position >= 6 ? (position - 4) / 2 : 0;
        d.indexed = betas && (fvf & 0x1000);
        d.stored = d.indexed ? betas - 1 : betas;
        const std::size_t position_bytes = position == 2 ? 12 : 12 + 4 * betas;
        d.positions.resize(count * 3);
        d.normals.assign((fvf & 0x10) ? count * 3 : 0, 0.0f);
        d.colors.assign((fvf & 0x40) ? count * 4 : 0, 0);
        const std::uint32_t sets = (fvf >> 8) & 0xf;
        d.coordinates.assign(sets ? count * 2 : 0, 0.0f);
        d.weights.assign(count * d.stored, 0.0f);
        d.palette.assign(d.indexed ? count * 4 : 0, 0);
        for (std::size_t i = 0; i < count; ++i) {
            const unsigned char* v = data + i * stride;
            std::memcpy(&d.positions[i * 3], v, 12);
            if (d.stored) std::memcpy(&d.weights[i * d.stored], v + 12, 4 * d.stored);
            if (d.indexed) std::memcpy(&d.palette[i * 4], v + 12 + 4 * d.stored, 4);
            std::size_t offset = position_bytes;
            if (fvf & 0x10) {
                std::memcpy(&d.normals[i * 3], v + offset, 12);
                offset += 12;
            }
            if (fvf & 0x20) offset += 4;  // point size
            if (fvf & 0x40) {
                std::uint32_t c;
                std::memcpy(&c, v + offset, 4);
                d.colors[i * 4 + 0] = static_cast<GLubyte>(c >> 16);  // D3DCOLOR is ARGB
                d.colors[i * 4 + 1] = static_cast<GLubyte>(c >> 8);
                d.colors[i * 4 + 2] = static_cast<GLubyte>(c);
                d.colors[i * 4 + 3] = static_cast<GLubyte>(c >> 24);
                offset += 4;
            }
            if (fvf & 0x80) offset += 4;  // specular
            if (sets) std::memcpy(&d.coordinates[i * 2], v + offset, 8);
        }
    }

    // A vertex buffer's decoded vertices, kept while its contents and format
    // do not change.
    Decoded& decoded_buffer(StateBuffer& b, std::uint32_t fvf, std::uint32_t stride, std::size_t count) {
        Decoded& d = buffers_[&b];
        if (d.buffer != &b || d.revision != b.revision || d.fvf != fvf || d.stride != stride) {
            decode(b.bytes().data(), count, fvf, stride, d);
            d.buffer = &b;
            d.revision = b.revision;
            d.fvf = fvf;
            d.stride = stride;
        }
        return d;
    }

    static void transform_point(const d3d8::Matrix& m, const float* v, float* out) {
        for (int j = 0; j < 3; ++j) out[j] = v[0] * m.m[j] + v[1] * m.m[4 + j] + v[2] * m.m[8 + j] + m.m[12 + j];
    }
    static void transform_normal(const d3d8::Matrix& m, const float* v, float* out) {
        for (int j = 0; j < 3; ++j) out[j] = v[0] * m.m[j] + v[1] * m.m[4 + j] + v[2] * m.m[8 + j];
    }

    // Direct3D's vertex blending: VERTEXBLEND n blends n + 1 world matrices
    // (WORLDMATRIX(k), or the palette index with INDEXEDVERTEXBLENDENABLE),
    // the last weight being 1 minus the stored ones. Results are in world
    // space.
    void blend(const Decoded& d, std::uint32_t matrices, bool indexed) {
        blended_positions_.assign(d.count * 3, 0.0f);
        blended_normals_.assign(d.normals.empty() ? 0 : d.count * 3, 0.0f);
        for (std::size_t i = 0; i < d.count; ++i) {
            float rest = 1.0f;
            for (std::uint32_t k = 0; k < matrices; ++k) {
                float w;
                if (k + 1 < matrices) {
                    w = k < d.stored ? d.weights[i * d.stored + k] : 0.0f;
                    rest -= w;
                } else {
                    w = rest;
                }
                if (w == 0.0f) continue;
                const std::uint32_t slot = indexed && !d.palette.empty() ? d.palette[i * 4 + k] : k;
                const d3d8::Matrix& m = transforms[256 + slot];
                float p[3];
                transform_point(m, &d.positions[i * 3], p);
                for (int j = 0; j < 3; ++j) blended_positions_[i * 3 + j] += w * p[j];
                if (!d.normals.empty()) {
                    float n[3];
                    transform_normal(m, &d.normals[i * 3], n);
                    for (int j = 0; j < 3; ++j) blended_normals_[i * 3 + j] += w * n[j];
                }
            }
        }
    }

    // Projection (Direct3D clip z in [0, w] becomes GL's [-w, w]), the view,
    // lights, material and fog. The modelview is left as the view.
    void apply_3d_state(bool has_normals, bool has_colors) {
        static const GLfloat depth_fix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 2, 0, 0, 0, -1, 1};
        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(depth_fix);
        glMultMatrixf(transforms[3].m);
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(transforms[2].m);
        // The Direct3D viewport (top-left origin) and its depth range.
        const TargetRect box = target_box(viewport.x, viewport.y, viewport.width, viewport.height);
        glViewport(box.x, box.y, box.w, box.h);
        glDepthRangef(viewport.min_z, viewport.max_z);
        const bool lighting = render_state[137] != 0;
        if (lighting && has_normals) {
            glEnable(GL_LIGHTING);
            const std::uint32_t a = render_state[139];  // AMBIENT
            const GLfloat ambient[4] = {((a >> 16) & 0xff) / 255.0f, ((a >> 8) & 0xff) / 255.0f, (a & 0xff) / 255.0f,
                                        (a >> 24) / 255.0f};
            glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
            for (int i = 0; i < 8; ++i) apply_light(i);
            if (render_state[143]) glEnable(GL_NORMALIZE);  // NORMALIZENORMALS
            else glDisable(GL_NORMALIZE);
            const float* m = material;
            glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, m);
            glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, m + 4);
            const GLfloat none[4] = {0, 0, 0, 0};
            glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, render_state[29] ? m + 8 : none);  // SPECULARENABLE
            glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, m + 12);
            glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, std::max(0.0f, std::min(128.0f, m[16])));
            // A vertex diffuse color replaces the material's (COLORVERTEX with
            // the default DIFFUSEMATERIALSOURCE COLOR1).
            if (has_colors && render_state[141]) glEnable(GL_COLOR_MATERIAL);
            else glDisable(GL_COLOR_MATERIAL);
        } else {
            // Unlit: the vertex diffuse color, or white without one.
            glDisable(GL_LIGHTING);
            glDisable(GL_COLOR_MATERIAL);
            glColor4f(1, 1, 1, 1);
        }
        apply_fog();
        glShadeModel(render_state[9] == 1 ? GL_FLAT : GL_SMOOTH);
        apply_pixel_state();
    }

    // One Direct3D light (D3DLIGHT8 words: type, diffuse, specular, ambient,
    // position, direction, range, falloff, attenuation 0-2, theta, phi), in
    // world space with the view loaded.
    void apply_light(int i) {
        const GLenum name = GL_LIGHT0 + static_cast<GLenum>(i);
        if (!light_enabled[i]) {
            glDisable(name);
            return;
        }
        float w[26];
        std::memcpy(w, lights[i].words, sizeof(w));
        const std::uint32_t type = lights[i].words[0];
        glEnable(name);
        glLightfv(name, GL_DIFFUSE, w + 1);
        glLightfv(name, GL_SPECULAR, w + 5);
        glLightfv(name, GL_AMBIENT, w + 9);
        if (type == 3) {  // directional: GL's position points toward the light
            const GLfloat direction[4] = {-w[16], -w[17], -w[18], 0.0f};
            glLightfv(name, GL_POSITION, direction);
            glLightf(name, GL_SPOT_CUTOFF, 180.0f);
            glLightf(name, GL_CONSTANT_ATTENUATION, 1.0f);
            glLightf(name, GL_LINEAR_ATTENUATION, 0.0f);
            glLightf(name, GL_QUADRATIC_ATTENUATION, 0.0f);
            return;
        }
        const GLfloat position[4] = {w[13], w[14], w[15], 1.0f};
        glLightfv(name, GL_POSITION, position);
        glLightf(name, GL_CONSTANT_ATTENUATION, w[21]);
        glLightf(name, GL_LINEAR_ATTENUATION, w[22]);
        glLightf(name, GL_QUADRATIC_ATTENUATION, w[23]);
        if (type == 2) {  // spot: the outer cone (phi) and its falloff
            const GLfloat direction[3] = {w[16], w[17], w[18]};
            glLightfv(name, GL_SPOT_DIRECTION, direction);
            glLightf(name, GL_SPOT_CUTOFF, std::min(90.0f, w[25] * 0.5f * 57.2957795f));
            glLightf(name, GL_SPOT_EXPONENT, std::max(0.0f, std::min(128.0f, w[20])));
        } else {
            glLightf(name, GL_SPOT_CUTOFF, 180.0f);
        }
    }

    // FOGENABLE 28, FOGCOLOR 34, FOGTABLEMODE 35, FOGSTART 36, FOGEND 37,
    // FOGDENSITY 38, FOGVERTEXMODE 140 (both evaluated per eye distance).
    void apply_fog() {
        const std::uint32_t mode = render_state[35] ? render_state[35] : render_state[140];
        if (!render_state[28] || !mode) {
            glDisable(GL_FOG);
            return;
        }
        glEnable(GL_FOG);
        glFogf(GL_FOG_MODE, mode == 1 ? GL_EXP : mode == 2 ? GL_EXP2 : GL_LINEAR);
        float start, end, density;
        std::memcpy(&start, &render_state[36], 4);
        std::memcpy(&end, &render_state[37], 4);
        std::memcpy(&density, &render_state[38], 4);
        glFogf(GL_FOG_START, start);
        glFogf(GL_FOG_END, end);
        glFogf(GL_FOG_DENSITY, density);
        const std::uint32_t c = render_state[34];
        const GLfloat color[4] = {((c >> 16) & 0xff) / 255.0f, ((c >> 8) & 0xff) / 255.0f, (c & 0xff) / 255.0f, 1.0f};
        glFogfv(GL_FOG_COLOR, color);
    }

    static GLenum primitive_mode(std::uint32_t type) {
        switch (type) {
        case d3d8::kPointList: return GL_POINTS;
        case d3d8::kLineList: return GL_LINES;
        case d3d8::kLineStrip: return GL_LINE_STRIP;
        case d3d8::kTriangleList: return GL_TRIANGLES;
        case d3d8::kTriangleStrip: return GL_TRIANGLE_STRIP;
        default: return GL_TRIANGLE_FAN;
        }
    }

    // Draws decoded vertices (indexed when `indices` is given). Without
    // blending the world matrix goes on the modelview; with it, blended
    // world-space vertices are drawn under the view.
    void draw_3d(std::uint32_t type, const Decoded& d, std::uint32_t count, const GLushort* indices) {
        bind_target();
        const std::uint32_t vertex_blend = render_state[151];
        const bool blending = vertex_blend >= 1 && vertex_blend <= 3;
        apply_3d_state(!d.normals.empty(), !d.colors.empty());
        const GLfloat* positions = d.positions.data();
        const GLfloat* normals = d.normals.empty() ? nullptr : d.normals.data();
        if (blending) {
            blend(d, vertex_blend + 1, render_state[167] != 0);
            positions = blended_positions_.data();
            normals = normals ? blended_normals_.data() : nullptr;
        } else {
            glMultMatrixf(transforms[256].m);
        }
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, positions);
        if (normals) {
            glEnableClientState(GL_NORMAL_ARRAY);
            glNormalPointer(GL_FLOAT, 0, normals);
        } else {
            glDisableClientState(GL_NORMAL_ARRAY);
        }
        if (!d.colors.empty()) {
            glEnableClientState(GL_COLOR_ARRAY);
            glColorPointer(4, GL_UNSIGNED_BYTE, 0, d.colors.data());
        } else {
            glDisableClientState(GL_COLOR_ARRAY);
        }
        if (textured_ && !d.coordinates.empty()) {
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glTexCoordPointer(2, GL_FLOAT, 0, d.coordinates.data());
            ++frame_textured_;
        } else {
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        }
        ++frame_3d_;
        if (dump_frame_ && frames_ + 1 == dump_frame_) {
            dump_3d(type, d, count, indices, positions, blending);
            dump_before();
        }
        if (indices) glDrawElements(primitive_mode(type), static_cast<GLsizei>(count), GL_UNSIGNED_SHORT, indices);
        else glDrawArrays(primitive_mode(type), 0, static_cast<GLsizei>(count));
        if (dump_frame_ && frames_ + 1 == dump_frame_) dump_after();
        glDisableClientState(GL_NORMAL_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisable(GL_LIGHTING);
        glDisable(GL_COLOR_MATERIAL);
        glDisable(GL_FOG);
        glDepthRangef(0.0f, 1.0f);
    }

    // Diagnostics (CP2_DUMP_FRAME=N): every draw of frame N with its states,
    // texture and screen-space extent (3D vertices projected through world,
    // view, projection and the viewport, as Direct3D maps them).
    void dump_states(const char* kind, std::uint32_t draw) {
        StateTexture* t = static_cast<StateTexture*>(texture[0]);
        std::fprintf(stderr,
                     "[CP2 dump] %s draw %u: fvf 0x%x, blend %u %u/%u op %u, alpha test %u ref %u, Z %u/%u, cull %u, "
                     "lighting %u, viewport %u,%u %ux%u, texture %s",
                     kind, draw, vertex_shader, render_state[27], render_state[19], render_state[20], render_state[171],
                     render_state[15], render_state[24], render_state[7], render_state[14], render_state[22],
                     render_state[137], viewport.x, viewport.y, viewport.width, viewport.height, t ? "" : "none");
        if (t) std::fprintf(stderr, "#%u %ux%u fmt %u, address %u/%u, filter %u/%u", t->trace_id, t->levels()[0].width,
                            t->levels()[0].height, t->format(), stage_state[0][13], stage_state[0][14],
                            stage_state[0][16], stage_state[0][17]);
        std::fprintf(stderr, "\n");
    }

    // The pixels one draw changed: the target read before and after it.
    void dump_before() {
        dump_pixels_.resize(static_cast<std::size_t>(target_width_) * target_height_ * 4);
        glReadPixels(0, 0, target_width_, target_height_, GL_RGBA, GL_UNSIGNED_BYTE, dump_pixels_.data());
    }
    void dump_after() {
        std::vector<unsigned char> after(dump_pixels_.size());
        glReadPixels(0, 0, target_width_, target_height_, GL_RGBA, GL_UNSIGNED_BYTE, after.data());
        std::size_t changed = 0;
        GLsizei x0 = target_width_, y0 = target_height_, x1 = -1, y1 = -1;
        long sum[3] = {0, 0, 0};
        for (GLsizei y = 0; y < target_height_; ++y)
            for (GLsizei x = 0; x < target_width_; ++x) {
                const std::size_t o = (static_cast<std::size_t>(y) * target_width_ + x) * 4;
                if (!std::memcmp(&after[o], &dump_pixels_[o], 3)) continue;
                ++changed;
                for (int c = 0; c < 3; ++c) sum[c] += static_cast<long>(after[o + c]) - dump_pixels_[o + c];
                const GLsizei top = target_height_ - 1 - y;
                x0 = std::min(x0, x); x1 = std::max(x1, x);
                y0 = std::min(y0, top); y1 = std::max(y1, top);
            }
        if (!changed) std::fprintf(stderr, "[CP2 dump]   changed no pixels\n");
        else
            std::fprintf(stderr, "[CP2 dump]   changed %zu pixels in (%d,%d)-(%d,%d) (target pixels), mean change %+.1f %+.1f %+.1f\n",
                         changed, x0, y0, x1, y1, static_cast<double>(sum[0]) / changed,
                         static_cast<double>(sum[1]) / changed, static_cast<double>(sum[2]) / changed);
    }

    void dump_2d(std::uint32_t type, std::uint32_t count, const d3d8::ScreenVertex* v) {
        dump_states("2D", frame_draws_);
        float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
        for (std::uint32_t i = 0; i < count; ++i) {
            x0 = std::min(x0, v[i].x); x1 = std::max(x1, v[i].x);
            y0 = std::min(y0, v[i].y); y1 = std::max(y1, v[i].y);
        }
        std::fprintf(stderr, "[CP2 dump]   type %u, %u vertices, x %.1f..%.1f y %.1f..%.1f, color %08x\n", type, count, x0,
                     x1, y0, y1, count ? v[0].color : 0);
    }

    void dump_3d(std::uint32_t type, const Decoded& d, std::uint32_t count, const GLushort* indices,
                 const GLfloat* positions, bool blended) {
        dump_states("3D", frame_draws_);
        const d3d8::Matrix& world = transforms[256];
        float sx0 = 1e30f, sy0 = 1e30f, sx1 = -1e30f, sy1 = -1e30f, u0 = 1e30f, u1 = -1e30f, v0 = 1e30f, v1 = -1e30f;
        std::uint32_t behind = 0;
        for (std::uint32_t k = 0; k < count; ++k) {
            const std::uint32_t i = indices ? indices[k] : k;
            float p[4] = {positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2], 1.0f}, q[4], r[4];
            auto mul = [](const d3d8::Matrix& m, const float* in, float* out) {
                for (int j = 0; j < 4; ++j)
                    out[j] = in[0] * m.m[j] + in[1] * m.m[4 + j] + in[2] * m.m[8 + j] + in[3] * m.m[12 + j];
            };
            if (!blended) {
                mul(world, p, q);
                std::memcpy(p, q, sizeof(p));
            }
            mul(transforms[2], p, q);
            mul(transforms[3], q, r);
            if (r[3] <= 0) {
                ++behind;
                continue;
            }
            const float sx = viewport.x + (r[0] / r[3] + 1) * 0.5f * viewport.width;
            const float sy = viewport.y + (1 - r[1] / r[3]) * 0.5f * viewport.height;
            sx0 = std::min(sx0, sx); sx1 = std::max(sx1, sx);
            sy0 = std::min(sy0, sy); sy1 = std::max(sy1, sy);
            if (!d.coordinates.empty()) {
                u0 = std::min(u0, d.coordinates[i * 2]); u1 = std::max(u1, d.coordinates[i * 2]);
                v0 = std::min(v0, d.coordinates[i * 2 + 1]); v1 = std::max(v1, d.coordinates[i * 2 + 1]);
            }
        }
        std::fprintf(stderr,
                     "[CP2 dump]   type %u, %u vertices%s, %u behind the camera; screen x %.1f..%.1f y %.1f..%.1f; "
                     "uv %.3f..%.3f, %.3f..%.3f; world row 3 %.1f %.1f %.1f\n",
                     type, count, blended ? " (blended)" : "", behind, sx0, sx1, sy0, sy1, u0, u1, v0, v1, world.m[12],
                     world.m[13], world.m[14]);
        if (count <= 6) {
            float clip[6][4];
            for (std::uint32_t k = 0; k < count; ++k) {
                const std::uint32_t i = indices ? indices[k] : k;
                float p[4] = {positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2], 1.0f}, q[4];
                auto mul = [](const d3d8::Matrix& m, const float* in, float* out) {
                    for (int j = 0; j < 4; ++j)
                        out[j] = in[0] * m.m[j] + in[1] * m.m[4 + j] + in[2] * m.m[8 + j] + in[3] * m.m[12 + j];
                };
                if (!blended) {
                    mul(world, p, q);
                    std::memcpy(p, q, sizeof(p));
                }
                mul(transforms[2], p, q);
                mul(transforms[3], q, clip[k]);
                std::fprintf(stderr, "[CP2 dump]     v%u: %.1f %.1f %.1f uv %.3f %.3f clip %.1f %.1f %.1f %.1f\n", k,
                             positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2],
                             d.coordinates.empty() ? 0.0f : d.coordinates[i * 2],
                             d.coordinates.empty() ? 0.0f : d.coordinates[i * 2 + 1], clip[k][0], clip[k][1], clip[k][2],
                             clip[k][3]);
            }
            // Facing per triangle: the naive screen area (after dividing by
            // w) and the homogeneous determinant of (x, y, w) rows.
            for (std::uint32_t k = 0; k + 2 < count && type == d3d8::kTriangleList; k += 3) {
                const float* a = clip[k];
                const float* b = clip[k + 1];
                const float* c = clip[k + 2];
                const float naive = (b[0] / b[3] - a[0] / a[3]) * (c[1] / c[3] - a[1] / a[3]) -
                                    (b[1] / b[3] - a[1] / a[3]) * (c[0] / c[3] - a[0] / a[3]);
                const float det = a[0] * (b[1] * c[3] - b[3] * c[1]) - a[1] * (b[0] * c[3] - b[3] * c[0]) +
                                  a[3] * (b[0] * c[1] - b[1] * c[0]);
                std::fprintf(stderr, "[CP2 dump]     triangle %u: naive area %g, homogeneous det %g\n", k / 3, naive, det);
            }
        }
    }

    std::map<StateBuffer*, Decoded> buffers_;
    std::vector<GLushort> indices_;
    std::vector<GLfloat> blended_positions_, blended_normals_;
    Decoded user_vertices_;
    std::uint32_t frame_3d_ = 0;

    GLuint framebuffer_ = 0, color_ = 0, depth_ = 0;
    GLsizei width_ = 0, height_ = 0;                // the Direct3D back buffer
    GLsizei target_width_ = 0, target_height_ = 0;  // the GL target (native resolution)
    float scale_x_ = 1.0f, scale_y_ = 1.0f;
    bool blend_subtract_ = false;
    bool textured_ = false;
    std::uint32_t frames_ = 0;
    std::uint32_t frame_draws_ = 0, frame_textured_ = 0, frame_skipped_ = 0;
    std::uint32_t reported_frame_ = 0;
    std::uint32_t dump_frame_ = 0;  // CP2_DUMP_FRAME: log every draw of that frame
    std::vector<unsigned char> dump_pixels_;
    std::chrono::steady_clock::time_point reported_at_;
    input::InputScript input_script_;
    std::set<std::string> reported_;
    std::vector<GLfloat> positions_, coordinates_;
    std::vector<GLubyte> colors_;
};


// ---------------------------------------------------------------------------
// 3D self-test (CP2_GRAPHICS_SELFTEST): GlDevice renders known scenes into a
// 64x64 back buffer and the pixels are checked against Direct3D math done on
// the CPU: projected bounds of an indexed lit quad (transform, viewport, y
// orientation), its lit color (material diffuse times a directional light,
// light direction sign), counter-clockwise culling, depth ordering, and
// vertex blending (WORLDMATRIX(0) with VERTEXBLEND 1, and a palette index
// with INDEXEDVERTEXBLENDENABLE).
namespace gl3d_test {

struct Box {
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
    int pixels = 0;
};

// The screen bounds (Direct3D top-left pixels) of world points under the
// device's view and projection, as Direct3D maps them into the viewport.
inline Box projected(const d3d8::Matrix& view, const d3d8::Matrix& projection, const float (*p)[3], int n, int size) {
    const d3d8::Matrix vp = matrix_multiply(view, projection);
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < n; ++i) {
        float c[4];
        for (int j = 0; j < 4; ++j)
            c[j] = p[i][0] * vp.m[j] + p[i][1] * vp.m[4 + j] + p[i][2] * vp.m[8 + j] + vp.m[12 + j];
        const float sx = (c[0] / c[3] + 1.0f) * 0.5f * size, sy = (1.0f - c[1] / c[3]) * 0.5f * size;
        x0 = std::min(x0, sx); x1 = std::max(x1, sx);
        y0 = std::min(y0, sy); y1 = std::max(y1, sy);
    }
    Box b;
    b.x0 = static_cast<int>(std::lround(x0)); b.x1 = static_cast<int>(std::lround(x1)) - 1;
    b.y0 = static_cast<int>(std::lround(y0)); b.y1 = static_cast<int>(std::lround(y1)) - 1;
    return b;
}

}  // namespace gl3d_test

inline bool GlDevice::self_test_3d() {
    using namespace gl3d_test;
    const int size = 64;
    d3d8::PresentParameters p;
    p.width = size;
    p.height = size;
    std::string error;
    GlDevice device;
    if (!device.create(p, error)) {
        std::fprintf(stderr, "[CP2 GL 3D test] device: %s\n", error.c_str());
        return false;
    }
    d3d8::Viewport viewport;
    viewport.width = viewport.height = size;
    device.set_viewport(viewport);
    const d3d8::Matrix view = look_at(Vec3{0, 0, -10}, Vec3{0, 0, 0}, Vec3{0, 1, 0});
    const d3d8::Matrix projection = perspective(0.785398163f, 1.0f, 1.0f, 100.0f);
    device.set_transform(2, view);
    device.set_transform(3, projection);
    d3d8::Matrix identity;
    for (int k = 0; k < 16; ++k) identity.m[k] = k % 5 == 0 ? 1.0f : 0.0f;
    device.set_transform(256, identity);
    // A directional light shining along +z (toward the scene), white.
    d3d8::Light light;
    light.words[0] = 3;
    for (int k = 0; k < 4; ++k) light.set(1 + k, 1.0f);
    light.set(18, 1.0f);
    device.set_light(0, light);
    device.light_enable(0, 1);
    // Quad corners at z, normal toward the camera (-z), clockwise on screen.
    auto quad = [&](float half, float z, float* out) {
        const float corners[4][2] = {{-half, -half}, {half, -half}, {half, half}, {-half, half}};
        for (int i = 0; i < 4; ++i) {
            const float v[8] = {corners[i][0], corners[i][1], z, 0, 0, -1, 0, 0};
            std::memcpy(out + 8 * i, v, sizeof(v));
        }
    };
    d3d8::Buffer* vb = nullptr;
    d3d8::Buffer* ib = nullptr;
    device.create_vertex_buffer(4 * 32, 0, 0x112, 1, vb);
    device.create_index_buffer(12, 0, d3d8::kFormatIndex16, 1, ib);
    unsigned char* data = nullptr;
    vb->lock(data, 0);
    quad(1.0f, 0.0f, reinterpret_cast<float*>(data));
    vb->unlock();
    const std::uint16_t clockwise[6] = {0, 2, 1, 0, 3, 2};
    ib->lock(data, 0);
    std::memcpy(data, clockwise, sizeof(clockwise));
    ib->unlock();
    auto material = [&](float r, float g, float b) {
        float m[17] = {r, g, b, 1};
        device.set_material(m);
    };
    auto read = [&](std::vector<unsigned char>& pixels) { device.read_back(pixels); };
    auto at = [&](const std::vector<unsigned char>& pixels, int x, int y) {
        return &pixels[(static_cast<std::size_t>(size - 1 - y) * size + x) * 4];  // Direct3D top-left
    };
    auto bounds = [&](const std::vector<unsigned char>& pixels) {
        Box b;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const unsigned char* c = at(pixels, x, y);
                if (c[0] < 8 && c[1] < 8 && c[2] < 8) continue;
                ++b.pixels;
                b.x0 = std::min(b.x0, x); b.x1 = std::max(b.x1, x);
                b.y0 = std::min(b.y0, y); b.y1 = std::max(b.y1, y);
            }
        return b;
    };
    auto near_box = [](const Box& a, const Box& e) {
        return std::abs(a.x0 - e.x0) <= 1 && std::abs(a.x1 - e.x1) <= 1 && std::abs(a.y0 - e.y0) <= 1 &&
               std::abs(a.y1 - e.y1) <= 1;
    };
    auto draw = [&](std::uint32_t fvf, std::uint32_t stride) {
        device.set_vertex_shader(fvf);
        device.set_stream_source(0, vb, stride);
        device.set_indices(ib, 0);
        device.draw_indexed_primitive(4, 0, 4, 0, 2);
    };
    bool ok = true;
    std::vector<unsigned char> pixels;
    // 1. A lit quad: bounds and color (diffuse (0.5, 0.25, 1) * N.L 1).
    device.clear(0, nullptr, 3, 0xff000000u, 1.0f, 0);
    material(0.5f, 0.25f, 1.0f);
    draw(0x112, 32);
    read(pixels);
    const float corners[4][3] = {{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
    const Box expected = projected(view, projection, corners, 4, size);
    const Box drawn = bounds(pixels);
    const unsigned char* center = at(pixels, size / 2, size / 2);
    const bool lit = std::abs(center[0] - 128) <= 3 && std::abs(center[1] - 64) <= 3 && center[2] >= 252;
    if (!near_box(drawn, expected) || !lit) {
        std::fprintf(stderr,
                     "[CP2 GL 3D test] quad bounds (%d,%d)-(%d,%d), expected (%d,%d)-(%d,%d); center %u %u %u, "
                     "expected 128 64 255\n",
                     drawn.x0, drawn.y0, drawn.x1, drawn.y1, expected.x0, expected.y0, expected.x1, expected.y1,
                     center[0], center[1], center[2]);
        ok = false;
    }
    // 2. Counter-clockwise triangles are culled (CULLMODE CCW).
    const std::uint16_t counter[6] = {0, 1, 2, 0, 2, 3};
    ib->lock(data, 0);
    std::memcpy(data, counter, sizeof(counter));
    ib->unlock();
    device.clear(0, nullptr, 3, 0xff000000u, 1.0f, 0);
    draw(0x112, 32);
    read(pixels);
    if (bounds(pixels).pixels) {
        std::fprintf(stderr, "[CP2 GL 3D test] a counter-clockwise quad was not culled\n");
        ok = false;
    }
    ib->lock(data, 0);
    std::memcpy(data, clockwise, sizeof(clockwise));
    ib->unlock();
    // 3. Depth: a near red quad stays in front of a larger far green one.
    device.clear(0, nullptr, 3, 0xff000000u, 1.0f, 0);
    vb->lock(data, 0);
    quad(1.0f, -2.0f, reinterpret_cast<float*>(data));
    vb->unlock();
    material(1, 0, 0);
    draw(0x112, 32);
    vb->lock(data, 0);
    quad(3.0f, 2.0f, reinterpret_cast<float*>(data));
    vb->unlock();
    material(0, 1, 0);
    draw(0x112, 32);
    read(pixels);
    center = at(pixels, size / 2, size / 2);
    const unsigned char* edge = at(pixels, 2 + size / 2 - 16, size / 2);
    if (!(center[0] > 240 && center[1] < 16) || !(edge[1] > 240 && edge[0] < 16)) {
        std::fprintf(stderr, "[CP2 GL 3D test] depth order: center %u %u %u, outside %u %u %u\n", center[0], center[1],
                     center[2], edge[0], edge[1], edge[2]);
        ok = false;
    }
    // 4. Vertex blending: XYZB2 with weight 1 on WORLDMATRIX(0) (+3 in x).
    vb->release();
    vb = nullptr;
    device.create_vertex_buffer(4 * 40, 0, 0x1118, 1, vb);
    auto blended = [&](std::uint32_t fvf, std::uint8_t palette) {
        float base[32];
        quad(1.0f, 0.0f, base);
        vb->lock(data, 0);
        for (int i = 0; i < 4; ++i) {
            unsigned char* v = data + 40 * i;
            std::memcpy(v, base + 8 * i, 12);  // position
            const float weight = 1.0f;
            std::memcpy(v + 12, &weight, 4);
            const std::uint8_t indices[4] = {palette, 0, 0, 0};
            std::memcpy(v + 16, indices, 4);   // second beta: indices (or an unused weight)
            std::memcpy(v + 20, base + 8 * i + 3, 20);  // normal, UV
        }
        vb->unlock();
        draw(fvf, 40);
    };
    d3d8::Matrix shift = identity;
    shift.m[12] = 3.0f;
    device.set_transform(256, shift);
    device.set_render_state(151, 1);  // VERTEXBLEND 1 weight
    device.clear(0, nullptr, 3, 0xff000000u, 1.0f, 0);
    material(1, 1, 1);
    blended(0x118, 0);
    read(pixels);
    const float shifted[4][3] = {{2, -1, 0}, {4, -1, 0}, {4, 1, 0}, {2, 1, 0}};
    Box want = projected(view, projection, shifted, 4, size);
    Box got = bounds(pixels);
    if (!near_box(got, want)) {
        std::fprintf(stderr, "[CP2 GL 3D test] vertex blending bounds (%d,%d)-(%d,%d), expected (%d,%d)-(%d,%d)\n",
                     got.x0, got.y0, got.x1, got.y1, want.x0, want.y0, want.x1, want.y1);
        ok = false;
    }
    // 5. Indexed blending: palette index 1 selects WORLDMATRIX(1) (-3 in x).
    d3d8::Matrix left = identity;
    left.m[12] = -3.0f;
    device.set_transform(257, left);
    device.set_render_state(167, 1);  // INDEXEDVERTEXBLENDENABLE
    device.clear(0, nullptr, 3, 0xff000000u, 1.0f, 0);
    blended(0x1118, 1);
    read(pixels);
    const float moved[4][3] = {{-4, -1, 0}, {-2, -1, 0}, {-2, 1, 0}, {-4, 1, 0}};
    want = projected(view, projection, moved, 4, size);
    got = bounds(pixels);
    if (!near_box(got, want)) {
        std::fprintf(stderr, "[CP2 GL 3D test] indexed blending bounds (%d,%d)-(%d,%d), expected (%d,%d)-(%d,%d)\n",
                     got.x0, got.y0, got.x1, got.y1, want.x0, want.y0, want.x1, want.y1);
        ok = false;
    }
    device.set_render_state(151, 0);
    device.set_render_state(167, 0);
    vb->release();
    ib->release();
    std::fprintf(stderr, "[CP2 GL 3D test] transform, lighting, culling, depth and vertex blending: %s\n",
                 ok ? "PASS" : "FAIL");
    return ok;
}

}  // namespace zgp
}  // namespace cp2
