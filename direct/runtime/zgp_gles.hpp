#pragma once

#include <OpenGLES/ES1/gl.h>
#include <OpenGLES/ES1/glext.h>
#include <cstdio>

namespace cp2 {

// The Windows createdx call constructs a D3D8 factory, before creating a device.
// Use the EAGL context owned by HSP instead of making a second unrelated view.
class GraphicsFactory {
public:
    bool initialize() {
        const GLubyte* version = glGetString(GL_VERSION);
        if (!version) return false;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum_texture_size_);
        ready_ = maximum_texture_size_ > 0;
        if (ready_) std::fprintf(stderr, "[CP2 graphics] OpenGL ES factory: %s, maximum texture %d\n",
                                 reinterpret_cast<const char*>(version), maximum_texture_size_);
        return ready_;
    }
    bool ready() const { return ready_; }
    int maximum_texture_size() const { return maximum_texture_size_; }
private:
    bool ready_ = false;
    GLint maximum_texture_size_ = 0;
};

// Real color/depth storage and drawing for the compatibility backend. Device
// presentation, textures, animation and the ZGP scene API are still separate.
class GraphicsSurface {
public:
    GraphicsSurface() = default;
    GraphicsSurface(const GraphicsSurface&) = delete;
    GraphicsSurface& operator=(const GraphicsSurface&) = delete;
    ~GraphicsSurface() { release(); }

    bool create(const GraphicsFactory& factory, int width, int height) {
        if (!factory.ready() || width <= 0 || height <= 0 ||
            width > factory.maximum_texture_size() || height > factory.maximum_texture_size()) return false;
        release();
        GLint previous_framebuffer, previous_texture, previous_renderbuffer;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING_OES, &previous_framebuffer);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
        glGetIntegerv(GL_RENDERBUFFER_BINDING_OES, &previous_renderbuffer);
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffersOES(1, &framebuffer_);
        glBindFramebufferOES(GL_FRAMEBUFFER_OES, framebuffer_);
        glFramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_TEXTURE_2D, texture_, 0);
        glGenRenderbuffersOES(1, &depth_);
        glBindRenderbufferOES(GL_RENDERBUFFER_OES, depth_);
        glRenderbufferStorageOES(GL_RENDERBUFFER_OES, GL_DEPTH_COMPONENT16_OES, width, height);
        glFramebufferRenderbufferOES(GL_FRAMEBUFFER_OES, GL_DEPTH_ATTACHMENT_OES, GL_RENDERBUFFER_OES, depth_);
        bool complete = glCheckFramebufferStatusOES(GL_FRAMEBUFFER_OES) == GL_FRAMEBUFFER_COMPLETE_OES;
        glBindFramebufferOES(GL_FRAMEBUFFER_OES, previous_framebuffer);
        glBindRenderbufferOES(GL_RENDERBUFFER_OES, previous_renderbuffer);
        glBindTexture(GL_TEXTURE_2D, previous_texture);
        if (!complete) { release(); return false; }
        width_ = width; height_ = height;
        return true;
    }

    // Call inside a compatibility drawing pass. The caller restores the HSP
    // state at the pass boundary; no UIKit/display ownership is transferred.
    void bind() const {
        glBindFramebufferOES(GL_FRAMEBUFFER_OES, framebuffer_);
        glViewport(0, 0, width_, height_);
        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        glOrthof(0, width_, height_, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
        glDisableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnableClientState(GL_VERTEX_ARRAY);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }

    void clear(float r, float g, float b, float a) const {
        glClearColor(r, g, b, a);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void fill(float left, float top, float right, float bottom,
              float r, float g, float b, float a) const {
        const GLfloat vertices[] = {left,top, right,top, left,bottom, right,bottom};
        glColor4f(r,g,b,a);
        glVertexPointer(2, GL_FLOAT, 0, vertices);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    void release() {
        if (depth_) glDeleteRenderbuffersOES(1, &depth_);
        if (framebuffer_) glDeleteFramebuffersOES(1, &framebuffer_);
        if (texture_) glDeleteTextures(1, &texture_);
        depth_ = framebuffer_ = texture_ = 0;
        width_ = height_ = 0;
    }
private:
    GLuint framebuffer_ = 0, texture_ = 0, depth_ = 0;
    int width_ = 0, height_ = 0;
};

// Run only on a fresh diagnostic launch, before HSP installs its draw state.
// glReadPixels checks the driver result, rather than just a successful compile.
inline bool graphics_driver_test() {
    GLint previous_framebuffer, viewport[4], matrix_mode;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING_OES, &previous_framebuffer);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_MATRIX_MODE, &matrix_mode);
    glMatrixMode(GL_PROJECTION); glPushMatrix();
    glMatrixMode(GL_MODELVIEW); glPushMatrix();
    while (glGetError() != GL_NO_ERROR) {}
    bool passed = false;
    {
        GraphicsFactory factory;
        GraphicsSurface surface;
        if (factory.initialize() && surface.create(factory,32,16)) {
            surface.bind();
            surface.clear(0,0,0,1);
            surface.fill(4,3,12,9,1,0,0,1);
            unsigned char inside[4]{}, outside[4]{};
            glReadPixels(5,11,1,1,GL_RGBA,GL_UNSIGNED_BYTE,inside);
            glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,outside);
            passed = inside[0]==255 && inside[1]==0 && inside[2]==0 && inside[3]==255 &&
                     outside[0]==0 && outside[1]==0 && outside[2]==0 && outside[3]==255 &&
                     glGetError()==GL_NO_ERROR;
        }
    }
    glBindFramebufferOES(GL_FRAMEBUFFER_OES, previous_framebuffer);
    glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glMatrixMode(matrix_mode);
    glDisableClientState(GL_VERTEX_ARRAY);
    glColor4f(1,1,1,1);
    std::fprintf(stderr, "[CP2 graphics test] framebuffer/depth allocation, rectangle pixels and clipping: %s\n",
                 passed ? "PASS" : "FAIL");
    return passed;
}

} // namespace cp2
