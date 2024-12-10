/*
 * Copyright (C) 2022 UBports Foundation
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 3, as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranties of MERCHANTABILITY,
 * SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef GRALLOCTEXTURE_H
#define GRALLOCTEXTURE_H

#include <QObject>
#include <QImage>
#include <QSize>
#include <QSGTexture>
#include <QMutex>
#include <QThread>
#include <QThreadPool>
#include <QWaitCondition>

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLFramebufferObject>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QOpenGLTexture>

#include <atomic>
#include <functional>
#include <memory>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <hybris/ui/ui_compatibility_layer.h>
#include <hybris/gralloc/gralloc.h>
#include <hardware/gralloc.h>

enum ColorShader {
    ColorShader_None = 0,
    ColorShader_Passthrough,
    ColorShader_FlipColorChannels,
    ColorShader_FlipColorChannelsWithAlpha,
    ColorShader_RGB32ToRGBX8888,
    ColorShader_RGB32ToRGBX8888_Premult,
    ColorShader_RedAndBlueSwap,

    ColorShader_First = ColorShader_Passthrough,
    ColorShader_Last = ColorShader_RedAndBlueSwap,
    ColorShader_Count = ColorShader_Last + 1
};

struct ShaderBundle {
    ShaderBundle(std::shared_ptr<QOpenGLShaderProgram> program, int vertexCoord, int textureCoord, int textureSampler, int hasAlpha) :
        program(program), vertexCoord(vertexCoord), textureCoord(textureCoord), texture(textureSampler), alpha(hasAlpha) {}
    std::shared_ptr<QOpenGLShaderProgram> program;
    const int vertexCoord = -1;
    const int textureCoord = -1;
    const int texture = -1;
    const int alpha = -1;
};

typedef std::map<ColorShader, std::shared_ptr<ShaderBundle>> ShaderCache;

struct EglImageFunctions {
    EglImageFunctions();
    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
    PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
};

struct EGLImagePtrDeleter {
    EGLImagePtrDeleter(EglImageFunctions& eglProcs) : m_eglProcs{eglProcs} {};
    void operator()(EGLImageKHR* p) const
    {
        if (*p != EGL_NO_IMAGE_KHR) {
            EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
            m_eglProcs.eglDestroyImageKHR(dpy, *p);
        }
        delete p;
    };
private:
    EglImageFunctions m_eglProcs;
};
typedef std::shared_ptr<EGLImageKHR> EGLImagePtr;
Q_DECLARE_METATYPE(EGLImagePtr);

struct GLState {
    GLint prevProgram = 0;
    GLint prevFbo = 0;
    GLint prevTexture = 0;
    GLint prevActiveTexture = 0;
    GLint prevArrayBuf = 0;
    GLint prevElementArrayBuf = 0;
    GLint prevViewport[4];
    GLint prevScissor[4];
    GLint prevColorClear[4];
};

class GrallocTexture;
class GrallocTextureCreator : public QObject
{
    Q_OBJECT
public:
    GrallocTextureCreator(QObject* parent = nullptr);

    GrallocTexture* createTexture(const QImage& image, ShaderCache& cachedShaders, const int maxTextureSize, const uint flags, const bool async, QOpenGLContext* gl);
    static int convertFormat(const QImage& image, int& numChannels, ColorShader& conversionShader, const bool alpha);

public Q_SLOTS:
    void signalUploadComplete(const GrallocTexture* texture, struct graphic_buffer* handle, const int textureSize);

Q_SIGNALS:
    void uploadComplete(const GrallocTexture* texture, EGLImagePtr image, const int textureSize);

private:
    bool m_debug;
    int m_nthreads;
    QThreadPool* m_threadPool;
    static constexpr uint32_t convertUsage();
    static constexpr uint32_t convertLockUsage();
};

class GrallocTexture : public QSGTexture
{
    Q_OBJECT

public:
    GrallocTexture();

    virtual int textureId() const override;
    virtual QSize textureSize() const override;
    virtual bool hasAlphaChannel() const override;
    virtual bool hasMipmaps() const override;
    virtual void bind() override;

    int textureByteCount() const;

public Q_SLOTS:
    void provideSizeInfo(const QSize& size);
    void createdEglImage(const GrallocTexture* texture, EGLImagePtr image, const int textureSize);

private Q_SLOTS:
    bool drawTexture(QOpenGLFunctions* gl) const;

private:
    GrallocTexture(GrallocTextureCreator* creator, const bool hasAlphaChannel,
                   std::shared_ptr<ShaderBundle> conversionShader,
                   EglImageFunctions eglImageFunctions, const bool async,
                   QOpenGLContext* gl);
    ~GrallocTexture();

    void ensureBoundTexture(QOpenGLFunctions* gl) const;
    void ensureFbo(QOpenGLFunctions* gl) const;

    void renderWithShader(QOpenGLFunctions* gl) const;
    bool dumpImageOnly(QOpenGLFunctions* gl) const;
    bool renderTexture(QOpenGLFunctions* gl) const;

    void awaitUpload() const;

    const GLState storeGlState(QOpenGLFunctions* gl) const;
    void restoreGlState(QOpenGLFunctions* gl, const GLState& state) const;

    bool m_hasAlphaChannel;
    std::shared_ptr<ShaderBundle> m_shaderCode;

    mutable std::unique_ptr<QOpenGLFramebufferObject> m_fbo;

    mutable EGLImagePtr m_image;
    mutable int m_textureSize;
    mutable QSize m_size;
    mutable GLuint m_texture;
    mutable bool m_bound;
    mutable bool m_valid;
    mutable bool m_rendered;

    mutable QWaitCondition m_uploadCondition;
    mutable QMutex m_uploadMutex;

    bool m_async;
    bool m_uploaded;

    EglImageFunctions m_eglImageFunctions;

    GrallocTextureCreator* m_creator;
    QOpenGLContext* m_gl;
    friend class GrallocTextureCreator;
};

#endif
