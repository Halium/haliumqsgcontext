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

#include "gralloctexture.h"

#include <QAbstractEventDispatcher>
#include <QDebug>
#include <QtConcurrent>
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QTimer>
#include <QQuickWindow>
#include <QOpenGLExtraFunctions>

#include <exception>

#include <sys/sysinfo.h>

extern "C" {
void hybris_ui_initialize();
}

static EglImageFunctions eglImageFunctions;

EglImageFunctions::EglImageFunctions()
{
    eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    if (!eglCreateImageKHR)
        throw std::runtime_error("eglCreateImageKHR");

    eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    if (!eglDestroyImageKHR)
        throw std::runtime_error("eglDestroyImageKHR");

    glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if (!glEGLImageTargetTexture2DOES)
        throw std::runtime_error("glEGLImageTargetTexture2DOES");
}

static inline QThreadPool* initThreadPool()
{
    // Leave room for the render and main threads to be scheduled often
    // Pools 2 uploader threads at minimum.
    const int maxThreads = std::max<int>(2, get_nprocs_conf() - 2);
    QThreadPool* pool = new QThreadPool();
    pool->setMaxThreadCount(maxThreads);
    pool->setExpiryTimeout(5000);
    return pool;
}

GrallocTextureCreator::GrallocTextureCreator(QObject* parent) :
    QObject(parent), m_threadPool(initThreadPool()), m_debug(qEnvironmentVariableIsSet("HALIUMQSG_LOG_TEXTURES"))
{

}

constexpr uint32_t GrallocTextureCreator::convertUsage()
{
    return GRALLOC_USAGE_SW_READ_NEVER | GRALLOC_USAGE_SW_WRITE_NEVER | GRALLOC_USAGE_HW_TEXTURE;
}

constexpr uint32_t GrallocTextureCreator::convertLockUsage()
{
    return GRALLOC_USAGE_SW_READ_NEVER | GRALLOC_USAGE_SW_WRITE_RARELY;
}

int GrallocTextureCreator::convertFormat(const QImage& image, int& numChannels, ColorShader& conversionShader, const bool alpha)
{
    // Note: On some devices anything other than HAL_PIXEL_FORMAT_RGBA_8888 is impossible
    //       to be used together with a shader and results in a solely blank surface.
    //       This is especially apparent on older generations of hardware, ie Halium 7 and some 9 devices.

    const auto format = image.format();
    switch (format) {
    case QImage::Format_Mono:
        break;
    case QImage::Format_MonoLSB:
        break;
    case QImage::Format_Indexed8:
        break;

    case QImage::Format_RGB32:
        conversionShader = alpha ? ColorShader_Passthrough : ColorShader_RGB32ToRGBX8888;
        numChannels = 4;
        return alpha ? HAL_PIXEL_FORMAT_BGRA_8888 : HAL_PIXEL_FORMAT_RGBX_8888;

    case QImage::Format_ARGB32:
        conversionShader = ColorShader_RGB32ToRGBX8888;
        numChannels = 4;
        return HAL_PIXEL_FORMAT_RGBA_8888;

    case QImage::Format_ARGB32_Premultiplied:
        conversionShader = alpha ? ColorShader_Passthrough : ColorShader_RGB32ToRGBX8888;
        numChannels = 4;
        return alpha ? HAL_PIXEL_FORMAT_BGRA_8888 : HAL_PIXEL_FORMAT_RGBA_8888;

    case QImage::Format_RGB16:
        break;
    case QImage::Format_ARGB8565_Premultiplied:
        break;
    case QImage::Format_RGB666:
        break;
    case QImage::Format_ARGB6666_Premultiplied:
        break;
    case QImage::Format_RGB555:
        break;
    case QImage::Format_ARGB8555_Premultiplied:
        break;
    case QImage::Format_RGB888:
        conversionShader = ColorShader_RedAndBlueSwap;
        numChannels = 3;
        return HAL_PIXEL_FORMAT_RGB_888;

    case QImage::Format_RGB444:
        break;
    case QImage::Format_ARGB4444_Premultiplied:
        break;
    case QImage::Format_RGBX8888:
        conversionShader = ColorShader_RedAndBlueSwap;
        numChannels = 4;
        return HAL_PIXEL_FORMAT_RGBX_8888;

    case QImage::Format_RGBA8888:
        conversionShader = ColorShader_RedAndBlueSwap;
        numChannels = 4;
        return HAL_PIXEL_FORMAT_RGBA_8888;

    case QImage::Format_RGBA8888_Premultiplied:
        conversionShader = ColorShader_RedAndBlueSwap;
        numChannels = 4;
        return HAL_PIXEL_FORMAT_RGBA_8888;

    case QImage::Format_BGR30:
        break;
    case QImage::Format_A2BGR30_Premultiplied:
        break;
    case QImage::Format_RGB30:
        break;
    case QImage::Format_A2RGB30_Premultiplied:
        break;
    case QImage::Format_Alpha8:
        break;
    case QImage::Format_Grayscale8:
        break;
    case QImage::Format_RGBX64:
        break;
    case QImage::Format_RGBA64:
        break;
    case QImage::Format_RGBA64_Premultiplied:
        break;
    default:
        break;
    }

    return -1;
}

// After the pixels have arrived at GPU memory, turn them into an EGLImage for easy consumption from within GL.
void GrallocTextureCreator::signalUploadComplete(const GrallocTexture* texture, struct graphic_buffer* handle, const int textureSize)
{
    EGLImageKHR image = EGL_NO_IMAGE_KHR;

    if (handle) {
        const EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        const EGLContext context = EGL_NO_CONTEXT;
        static const EGLint attrs[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };

        void* native_buffer = graphic_buffer_get_native_buffer(handle);
        image = eglImageFunctions.eglCreateImageKHR(dpy, context, EGL_NATIVE_BUFFER_ANDROID, native_buffer, attrs);
        graphic_buffer_free(handle);
    }

    // Here we indicate upload progression/completeness through enqueueing a signal into the main loop.
    // This allows us to allocate GrallocTextures quickly while a separate thread uploads the pixels to the GPU.
    // Should the GrallocTexture disappear before the upload thread finishes then it won't result in invalid accesses
    // as Qt doesn't forward signals to deleted objects, safely.
    Q_EMIT uploadComplete(texture, image, textureSize);
}

GrallocTexture* GrallocTextureCreator::createTexture(const QImage& image, ShaderCache& cachedShaders, const int maxTextureSize, const uint flags, const bool async, QOpenGLContext* gl)
{
    int numChannels = 0;
    ColorShader conversionShader = ColorShader_None;

    const bool hasAlphaChannel = image.hasAlphaChannel() && (flags & QQuickWindow::TextureHasAlphaChannel); 
    const int format = convertFormat(image, numChannels, conversionShader, hasAlphaChannel);
    if (format < 0 || conversionShader == ColorShader_None) {
        qDebug() << "Unknown color format" << image.format();
        return nullptr;
    }

    GrallocTexture* texture = nullptr;

    {
        std::shared_ptr<ShaderBundle> shaderBundle {nullptr};
        if (cachedShaders.find(conversionShader) != cachedShaders.end())
            shaderBundle = cachedShaders[conversionShader];

        // Fall back to Qt-based uploading of textures in case no shaders are available
        if (conversionShader != ColorShader_None && !shaderBundle)
            return nullptr;

        try {
            const bool threadPoolCongested = m_threadPool->activeThreadCount() >= m_threadPool->maxThreadCount();
            texture = new GrallocTexture(this, hasAlphaChannel, shaderBundle, eglImageFunctions, (async && !threadPoolCongested), gl);

            if (m_debug) {
                qInfo() << QThread::currentThread() << "Texture created" << texture << "async & not congested:" << (async && !threadPoolCongested)
                         << "image:" << image << "with alpha channel:" << hasAlphaChannel << "shader" << conversionShader;
            }

            if (texture) {
                QSize size = image.size();
                float scaleFactor = 1.0;
                if (size.width() > maxTextureSize)
                    scaleFactor = (float)maxTextureSize / (float)size.width();
                if (size.height() > maxTextureSize)
                    scaleFactor = (float)maxTextureSize / (float)size.height();

                size = QSize(size.width() * scaleFactor, size.height() * scaleFactor);
                texture->provideSizeInfo(size);

                // Mediate texture uploads from the concurrent thread through the creator up to the GrallocTexture
                // as a means to only guarantee access to valid, undeleted QSG/GrallocTextures
                QObject::connect(this, &GrallocTextureCreator::uploadComplete, texture, &GrallocTexture::createdEglImage, Qt::DirectConnection);

                //auto uploadFunc = [ this, image, texture, numChannels, format, size, scaleFactor ]() {
                auto uploadFunc = [=]() {
                    const QImage toUpload = (size != image.size()) ? image.transformed(QTransform::fromScale(scaleFactor, scaleFactor)) : image;
                    const uint32_t usage = convertUsage();
                    struct graphic_buffer* handle = graphic_buffer_new_sized(toUpload.width(), toUpload.height(), format, usage);
                    if (!handle) {
                        qWarning() << "No buffer allocated";
                        signalUploadComplete(texture, handle, 0);
                        return;
                    }

                    const int stride = graphic_buffer_get_stride(handle);
                    const int lockUsage = convertLockUsage();
                    const int bytesPerLine = toUpload.bytesPerLine();
                    const int grallocBytesPerLine = stride * numChannels;
                    const int copyBytesPerLine = qMin(bytesPerLine, grallocBytesPerLine);  
                    const int textureSize = (bytesPerLine != grallocBytesPerLine) ?
                        copyBytesPerLine * toUpload.height() :
                        toUpload.sizeInBytes();

                    void* vmemAddr = nullptr;
                    graphic_buffer_lock(handle, lockUsage, &vmemAddr);

                    if (vmemAddr) {
                        if (bytesPerLine == grallocBytesPerLine) {
                            const uchar* src = toUpload.constBits();
                            memcpy(vmemAddr, (const void*)src, textureSize);
                        } else {
                            for (int i = 0; i < toUpload.height(); i++) {
                                void* dst = (vmemAddr + (grallocBytesPerLine * i));
                                const void* src = toUpload.constScanLine(i);
                                memcpy(dst, src, copyBytesPerLine);
                            }
                        }
                    }

                    graphic_buffer_unlock(handle);
                    signalUploadComplete(texture, handle, textureSize);
                };

                if (async && !threadPoolCongested) {
                    QtConcurrent::run(m_threadPool, std::move(uploadFunc));
                } else {
                    uploadFunc();
                }
            }
        } catch (const std::exception& ex) {
            texture = nullptr;
        }
    }

    return texture;
}

GrallocTexture::GrallocTexture(GrallocTextureCreator* creator, const bool hasAlphaChannel, std::shared_ptr<ShaderBundle> conversionShader,
                               EglImageFunctions eglImageFunctions, const bool async, QOpenGLContext* gl) :
    QSGTexture(), m_image(EGL_NO_IMAGE_KHR), m_texture(0), m_textureSize(0),
    m_hasAlphaChannel(hasAlphaChannel), m_shaderCode(conversionShader), m_bound(false), m_valid(true),
    m_rendered(false), m_async(async), m_uploaded(!async), m_eglImageFunctions(eglImageFunctions), m_creator(creator), m_gl(gl)
{
}

GrallocTexture::GrallocTexture() : m_valid(false)
{
}

GrallocTexture::~GrallocTexture()
{
    releaseResources();

    if (m_fbo) {
        m_fbo.reset(nullptr);
    }

    if (m_texture != 0) {
        QOpenGLFunctions* gl = nullptr;
        if (m_gl)
            gl = m_gl->functions();

        if (gl)
            gl->glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
}

int GrallocTexture::textureId() const
{
    // Default to expectancy of synchronous uploads, we check m_async later anyway. 
    bool would_wait = false;
    QOpenGLFunctions* gl = nullptr;

    if (m_gl)
        gl = m_gl->functions();

    if (!gl) {
        qWarning() << "Cannot get texture id, GL context is null";
        return 0;
    }

    if (!m_shaderCode || !m_shaderCode->program) {
        ensureBoundTexture(gl);
    } else {
        ensureFbo(gl);
    }

    if (m_async) {
        QMutexLocker locker(&m_uploadMutex);
        would_wait = (m_image == EGL_NO_IMAGE_KHR);
    }

    // We can safely call ::drawTexture() again until successfully rendered.
    // Also should speed up getting texture contents rendered in case of a synchronous upload.
    if (!would_wait)
        drawTexture(gl);

    if (!m_shaderCode || !m_shaderCode->program) {
        return m_texture;
    } else {
        return m_fbo->texture();
    }
}

QSize GrallocTexture::textureSize() const
{
    return m_size;
}

bool GrallocTexture::hasAlphaChannel() const
{
    return m_hasAlphaChannel;
}

bool GrallocTexture::hasMipmaps() const
{
    return false;
}

int GrallocTexture::textureByteCount() const
{
    return m_textureSize;
}

void GrallocTexture::provideSizeInfo(const QSize& size)
{
    m_size = size;
}

void GrallocTexture::createdEglImage(const GrallocTexture* texture, EGLImageKHR image, const int textureSize)
{
    // GrallocTextureCreator "broadcasts" EGLImage readyness to every GrallocTexture it is currently uploading pixels for.
    // Just make sure this slot call is actually meant for us and disconnect when done.
    if (texture != this)
        return;

    QObject::disconnect(m_creator, &GrallocTextureCreator::uploadComplete, this, &GrallocTexture::createdEglImage);

    qDebug() << QThread::currentThread() << "EGLImage created";

    {
        QMutexLocker locker(&m_uploadMutex);
        m_textureSize = textureSize;
        m_image = image;
        m_uploaded = true;
        m_uploadCondition.wakeOne();
    }
}

void GrallocTexture::ensureBoundTexture(QOpenGLFunctions* gl) const
{
    if (m_texture == 0) {
        gl->glGenTextures(1, &m_texture);
    }
}

bool GrallocTexture::dumpImageOnly(QOpenGLFunctions* gl) const
{
    if (m_rendered)
        return false;

    const auto state = storeGlState(gl);

    ensureBoundTexture(gl);
    gl->glBindTexture(GL_TEXTURE_2D, m_texture);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_eglImageFunctions.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, m_image);

    restoreGlState(gl, state);

    m_rendered = true;
    return true;
}

void GrallocTexture::ensureFbo(QOpenGLFunctions* gl) const
{
    if (m_fbo)
        return;

    const auto state = storeGlState(gl);
    m_fbo = std::make_unique<QOpenGLFramebufferObject>(m_size);
    restoreGlState(gl, state);
}

const GLState GrallocTexture::storeGlState(QOpenGLFunctions* gl) const
{
    // Could be called at arbitrary points in time with various OpenGL states,
    // better store and reset them after we're done with OpenGL calls.
    GLState state;

    gl->glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.prevTexture);

    // That's enough for the bind-only usecase
    if (!m_shaderCode || !m_shaderCode->program) {
        return state;
    }

    // For the render-to-texture usecase, store a few more details
    gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &state.prevFbo);
    gl->glGetIntegerv(GL_ACTIVE_TEXTURE, &state.prevActiveTexture);
    gl->glGetIntegerv(GL_CURRENT_PROGRAM, &state.prevProgram);
    gl->glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state.prevArrayBuf);
    gl->glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &state.prevElementArrayBuf);
    gl->glGetIntegerv(GL_VIEWPORT, state.prevViewport);
    gl->glGetIntegerv(GL_SCISSOR_BOX, state.prevScissor);
    gl->glGetIntegerv(GL_COLOR_CLEAR_VALUE, state.prevColorClear);

    qDebug() << "prevFbo:" << state.prevFbo << "prevTexture:" << state.prevTexture << "prevActiveTexture:" << state.prevActiveTexture
             << "prevProgram:" << state.prevProgram << "prevArrayBuf:" << state.prevArrayBuf << "prevElementArrayBuf:" << state.prevElementArrayBuf;

    return state;
}

void GrallocTexture::restoreGlState(QOpenGLFunctions* gl, const GLState& state) const
{
    // Reset OpenGL state that we messed with

    // That's enough for the bind-only usecase
    if (!m_shaderCode || !m_shaderCode->program) {
        gl->glBindTexture(GL_TEXTURE_2D, state.prevTexture);
        return;
    }

    gl->glBindFramebuffer(GL_FRAMEBUFFER, state.prevFbo);
    gl->glClearColor(state.prevColorClear[0], state.prevColorClear[1], state.prevColorClear[2], state.prevColorClear[3]);
    gl->glViewport(state.prevViewport[0], state.prevViewport[1], state.prevViewport[2], state.prevViewport[3]);
    gl->glScissor(state.prevScissor[0], state.prevScissor[1], state.prevScissor[2], state.prevScissor[3]);
    gl->glActiveTexture(state.prevActiveTexture);
    gl->glBindTexture(GL_TEXTURE_2D, state.prevTexture);
    gl->glUseProgram(state.prevProgram);
    gl->glBindBuffer(GL_ARRAY_BUFFER, state.prevArrayBuf);
    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, state.prevElementArrayBuf);
}

void GrallocTexture::renderWithShader(QOpenGLFunctions* gl) const
{
    const auto& size = m_size;
    const auto width = size.width();
    const auto height = size.height();
    const int textureUnit = 1;
    GLuint tmpTexture = 0;

    static const GLfloat vertex_buffer_data[] = {
        -1,-1, 0,
        -1, 1, 0,
         1,-1, 0,
        -1, 1, 0,
         1,-1, 0,
         1, 1, 0
    };

    static const GLfloat texture_buffer_data[] = {
        0, 0,
        0, 1,
        1, 0,
        0, 1,
        1, 0,
        1, 1
    };

    const GLState state = storeGlState(gl);

    ensureFbo(gl);
    if (!m_fbo || !m_fbo->isValid()) {
        qWarning() << "Failed to set up FBO";
        restoreGlState(gl, state);
        return;
    }

    m_fbo->bind();
    gl->glViewport(0, 0, width, height);
    gl->glScissor(0, 0, width, height);

#if 0 // GLES 3.0 only
    const GLenum attachments[2] = { GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT };
    m_gl->extraFunctions()->glInvalidateFramebuffer(GL_FRAMEBUFFER, 2, attachments);
#endif

    gl->glClearColor(0.0, 0.0, 0.0, m_hasAlphaChannel ? 0.0 : 1.0);
    gl->glClear(GL_COLOR_BUFFER_BIT);

    // Now apply the shader
    QOpenGLVertexArrayObject vao;
    QOpenGLBuffer vertexBuffer;
    QOpenGLBuffer textureBuffer;

    vao.create();
    vao.bind();

    vertexBuffer.create();
    vertexBuffer.bind();
    vertexBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    vertexBuffer.allocate(vertex_buffer_data, 18 * sizeof(GLfloat));
    m_shaderCode->program->enableAttributeArray(m_shaderCode->vertexCoord);
    m_shaderCode->program->setAttributeBuffer(m_shaderCode->vertexCoord, GL_FLOAT, 0, 3, 0);
    vertexBuffer.release();

    textureBuffer.create();
    textureBuffer.bind();
    textureBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    textureBuffer.allocate(texture_buffer_data, 12 * sizeof(GLfloat));
    m_shaderCode->program->enableAttributeArray(m_shaderCode->textureCoord);
    m_shaderCode->program->setAttributeBuffer(m_shaderCode->textureCoord, GL_FLOAT, 0, 2, 0);
    textureBuffer.release();

    // Make use of the swizzle shader or otherwise color-changing OpenGL program
    m_shaderCode->program->bind();

    // Generate and bind the temporary texture to the shader's sampler
    gl->glGenTextures(1, &tmpTexture);
    gl->glActiveTexture(GL_TEXTURE0 + textureUnit);
    gl->glBindTexture(GL_TEXTURE_2D, tmpTexture);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_eglImageFunctions.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, m_image);

    m_shaderCode->program->setUniformValue(m_shaderCode->texture, textureUnit);
    m_shaderCode->program->setUniformValue(m_shaderCode->alpha, m_hasAlphaChannel);

    // Render the temporary texture through the shader into the color attachment
    gl->glDrawArrays(GL_TRIANGLES, 0, 6);

    // We're done, reset the use of the shader
    vao.release();
    vertexBuffer.destroy();
    textureBuffer.destroy();
    vao.destroy();
    m_shaderCode->program->release();

    // Release the FBO and clean up
    m_fbo->release();

    gl->glDeleteTextures(1, &tmpTexture);

    restoreGlState(gl, state);
}

bool GrallocTexture::renderTexture(QOpenGLFunctions* gl) const
{
    if (m_rendered)
        return false;

    renderWithShader(gl);
    m_rendered = true;
    return true;
}

bool GrallocTexture::drawTexture(QOpenGLFunctions* gl) const
{
    bool ret = false;
    bool wait = false;

    // Usual preparations (waiting for EGLImage to arrive) in case we're certain
    // no actual rendering has happened yet.
    if (!m_rendered) {
        if (m_async) {
            QMutexLocker locker(&m_uploadMutex);
            wait = !m_uploaded;
        }

        if (wait) {
            awaitUpload();
        }
    } else {
        // No update to the texture supported, this is not a QSGDynamicTexture
        return false;
    }

    if (!m_shaderCode || !m_shaderCode->program) {
        ret = dumpImageOnly(gl);
    } else {
        ret = renderTexture(gl);
    }

    return ret;
}

void GrallocTexture::bind()
{
    QOpenGLFunctions* gl = nullptr;
    if (m_gl)
        gl = m_gl->functions();

    if (!gl) {
        qWarning() << "Cannot bind texture, GL context is null";
        return;
    }

    // Will block until EGLImage is received from the uploader machinery.
    drawTexture(gl);

    if (!m_shaderCode || !m_shaderCode->program) {
        gl->glBindTexture(GL_TEXTURE_2D, m_texture);
    } else {
        gl->glBindTexture(GL_TEXTURE_2D, m_fbo->texture());
    }
}

void GrallocTexture::awaitUpload() const
{
    if (!m_async)
        return;

    if (m_rendered)
        return;

    QMutexLocker locker(&m_uploadMutex);
    while (!m_uploaded) {
        m_uploadCondition.wait(&m_uploadMutex);
    }
    qDebug() << "Upload complete";
}

void GrallocTexture::releaseResources() const
{
    if (m_image != EGL_NO_IMAGE_KHR) {
        EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        m_eglImageFunctions.eglDestroyImageKHR(dpy, m_image);
        m_image = EGL_NO_IMAGE_KHR;
    }
}
