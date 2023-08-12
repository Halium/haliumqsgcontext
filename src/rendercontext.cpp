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

#include "rendercontext.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QThread>
#include <QtMath>
#include <QMutex>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QQuickWindow>

#include <QtQuick/private/qsgrenderloop_p.h>

#include <dlfcn.h>
#include <hybris/common/dlfcn.h>

// Clashes with deviceinfo
#undef None

static const GLchar* COLOR_CONVERSION_VERTEX = {
    "#version 100\n"
    "attribute highp vec3 vertexCoord;\n"
    "attribute highp vec2 textureCoord;\n"
    "varying highp vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    uv = textureCoord.xy;\n"
    "    gl_Position = vec4(vertexCoord,1.0);\n"
    "}\n"
};

static const GLchar* PASSTHROUGH_SHADER = {
    "#version 100\n"
    "precision mediump float;\n"
    "uniform sampler2D textureSampler;\n"
    "uniform bool hasAlpha;\n"
    "varying highp vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    vec3 color = texture2D(textureSampler, uv).rgb;\n"
    "    float alpha = hasAlpha ? texture2D(textureSampler, uv).a : 1.0;\n"
    "    gl_FragColor = vec4(color, alpha);\n"
    "}\n"
};

static const GLchar* FLIP_COLOR_CHANNELS_SHADER = {
    "#version 100\n"
    "precision mediump float;\n"
    "uniform sampler2D textureSampler;\n"
    "varying highp vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    gl_FragColor = vec4(texture2D(textureSampler, uv).bgr, 1.0);\n"
    "}\n"
};

static const GLchar* FLIP_COLOR_CHANNELS_WITH_ALPHA_SHADER = {
    "#version 100\n"
    "precision mediump float;\n"
    "uniform sampler2D textureSampler;\n"
    "varying highp vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(textureSampler, uv).bgra;\n"
    "}\n"
};

static const GLchar* RGB32_TO_RGBA8888_SHADER = {
    "#version 100\n"
    "precision mediump float;\n"
    "uniform sampler2D textureSampler;\n"
    "uniform bool hasAlpha;\n"
    "varying highp vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    vec4 sampledColor = texture2D(textureSampler, uv);\n"
    "    vec3 color = sampledColor.bgr;\n"
    "    float alpha = hasAlpha ? sampledColor.a : 1.0;\n"
    "    if (hasAlpha) {\n"
    "        color = vec3(color.r * alpha, color.g * alpha, color.b * alpha);\n"
    "    }\n"
    "    gl_FragColor = vec4(color, alpha);\n"
    "}\n"
};

static const GLchar* RGB32_TO_RGBA8888_PREMULT_SHADER = {
    "#version 100\n"
    "precision mediump float;\n"
    "uniform sampler2D textureSampler;\n"
    "uniform bool hasAlpha;\n"
    "varying highp vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    vec4 sampledColor = texture2D(textureSampler, uv);\n"
    "    vec3 color = sampledColor.bgr;\n"
    "    float alpha = hasAlpha ? sampledColor.a : 1.0;\n"
    "    if (hasAlpha) {\n"
    "        if (alpha == 0.0) { color = vec3(0.0, 0.0, 0.0); }"
    "        else { }\n"
    "    }\n"
    "    gl_FragColor = vec4(color, alpha);\n"
    "}\n"
};

static const GLchar* RED_AND_BLUE_SWAP_SHADER = {
    "#version 100\n"
    "precision mediump float;\n"
    "uniform sampler2D textureSampler;\n"
    "uniform bool hasAlpha;\n"
    "varying highp vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    vec3 color = texture2D(textureSampler, uv).bgr;\n"
    "    float alpha = hasAlpha ? texture2D(textureSampler, uv).a : 1.0;\n"
    "    gl_FragColor = vec4(color, alpha);\n"
    "}\n"
};


RenderContext::RenderContext(QSGContext* context) : QSGDefaultRenderContext(context),
    m_logging(false), m_quirks(RenderContext::NoQuirk), m_libuiFound(false), m_deviceInfo(DeviceInfo::None),
    m_textureCreator(new GrallocTextureCreator(this)), m_initialized(false), m_colorShadersBuilt(false)
{
    // Disable use of color correction shaders if explicitly requested
    if (m_deviceInfo.get("HaliumQsgUseShaders", "true") == "false") {
        m_quirks |= RenderContext::DisableConversionShaders;
    }
    if (m_deviceInfo.get("HaliumQsgUseRtScheduling", "false") == "true") {
        m_quirks |= RenderContext::UseRtScheduling;
    }
}

void RenderContext::messageReceived(const QOpenGLDebugMessage &debugMessage)
{
    qWarning() << "OpenGL log:" << debugMessage.message();
}

bool RenderContext::init() const
{
    if (qEnvironmentVariableIsSet("HALIUMQSG_OPENGL_LOG")) {
        m_logging = true;
        connect(&m_glLogger, &QOpenGLDebugLogger::messageLogged, this, &RenderContext::messageReceived);

        m_glLogger.initialize();
        m_glLogger.startLogging(QOpenGLDebugLogger::SynchronousLogging);
    }

#if 0
    // Attempt to get FIFO scheduling from rtkit-daemon
    if (m_quirks & RenderContext::UseRtScheduling) {
        const QString connName = QStringLiteral("haliumqsgcontext");

        QDBusConnection shortConnection = QDBusConnection::connectToBus(QDBusConnection::SystemBus, connName);
        QDBusInterface interface(QStringLiteral("org.freedesktop.RealtimeKit1"),
                                 QStringLiteral("/org/freedesktop/RealtimeKit1"),
                                 QStringLiteral("org.freedesktop.RealtimeKit1"),
                                 shortConnection);

        QDBusReply<int> reply = interface.call(QStringLiteral("MakeThreadRealtime"), QVariant((qulonglong)gettid()), QVariant((uint)10));
        if (!reply.isValid()) {
            qDebug() << "Failed to acquire realtime scheduling on render thread" << reply.error().message();
        }
        QDBusConnection::disconnectFromBus(connName);
    }
#endif
#if 0
    // Have mechanicd place us in an appropriate schedtune cgroup
    {
        const QString connName = QStringLiteral("haliumqsgcontext");

        QDBusConnection shortConnection = QDBusConnection::connectToBus(QDBusConnection::SystemBus, connName);
        QDBusInterface interface(QStringLiteral("org.halium.mechanicd"),
                                 QStringLiteral("/org/halium/mechanicd/Scheduling"),
                                 QStringLiteral("org.halium.mechanicd.Scheduling"),
                                 shortConnection);

        QDBusReply<void> reply = interface.call(QStringLiteral("requestSchedulingChange"));
        if (!reply.isValid()) {
            qDebug() << "Failed to acquire schedtune scheduling on render thread" << reply.error().message();
        }
        QDBusConnection::disconnectFromBus(connName);
    }
#endif

    // Check whether the prerequisite library can be dlopened
    {
#ifdef __LP64__
        const char* ldpath = "/system/lib64/libui_compat_layer.so";
#else
        const char* ldpath = "/system/lib/libui_compat_layer.so";
#endif
        void* handle = hybris_dlopen(ldpath, RTLD_LAZY);
        if (!handle)
            return false;
        else
            m_libuiFound = true;

        hybris_dlclose(handle);
    }

    return true;
}

QSGTexture* RenderContext::createTexture(const QImage &image, uint flags) const
{
    QSGTexture* texture = nullptr;
    int numChannels = 0;
    ColorShader shader = ColorShader_None;

    // Asynchronously upload textures whenever possible to go easy on the render thread
    const bool async = (openglContext() && openglContext()->thread() == QThread::currentThread()) ||
                        image.width() > m_maxTextureSize || image.height() > m_maxTextureSize;
    const bool alpha = image.hasAlphaChannel() && (flags & QQuickWindow::TextureHasAlphaChannel); 

    if (!m_initialized)
        m_initialized = init();

    if (!m_initialized)
        goto default_method;

    if (!m_colorShadersBuilt)
        m_colorShadersBuilt = compileColorShaders();

    if (!m_colorShadersBuilt)
        goto default_method;

    // We don't support texture atlases, so defer to Qt's internal implementation
    if (flags & QSGRenderContext::CreateTexture_Atlas)
        goto default_method;

    // Same for mipmaps, use Qt's implementation
    if (flags & QSGRenderContext::CreateTexture_Mipmap)
        goto default_method;

    if (GrallocTextureCreator::convertFormat(image, numChannels, shader, alpha) < 0 || numChannels == 0)
        goto default_method;

    if ((m_quirks & RenderContext::DisableConversionShaders) && (shader != ColorShader_None))
        goto default_method;

    texture = m_textureCreator->createTexture(image, m_cachedShaders, m_maxTextureSize, flags, async, openglContext());
    if (texture)
        return texture;

default_method:
    if (m_logging)
        qDebug() << "Falling back to Qt for texture uploads";
    return QSGDefaultRenderContext::createTexture(image, flags);
}

bool RenderContext::compileColorShaders() const
{
    if (!openglContext())
        return false;

    QOpenGLFunctions* gl = openglContext()->functions();

    // Store the texture geometry limit to decide later on whether to use Gralloc or not
    gl->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTextureSize);

    if (m_logging)
        qDebug() << "Max texture size:" << m_maxTextureSize;

    m_cachedShaders.clear();
    m_cachedShaders[ColorShader_None] = std::make_shared<ShaderBundle>(nullptr, 0, 0, 0, 0);

    // When conversion shaders are disabled the application might still use EGLImage or the default
    if (m_quirks & RenderContext::DisableConversionShaders)
        return true;

    for (int i = (int)ColorShader::ColorShader_First; i < ColorShader::ColorShader_Count; i++) {
        auto program = std::make_shared<QOpenGLShaderProgram>();
        bool success = false;

        success = program->addCacheableShaderFromSourceCode(QOpenGLShader::Vertex, COLOR_CONVERSION_VERTEX);

        if (!success) {
            qWarning() << "Failed to compile vertex shader hence using defaults. Reason:";
            qWarning() << program->log();
            return false;
        }

        switch (i) {
        case ColorShader_Passthrough:
            success = program->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, PASSTHROUGH_SHADER);
            break;
        case ColorShader_FlipColorChannels:
            success = program->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, FLIP_COLOR_CHANNELS_SHADER);
            break;
        case ColorShader_FlipColorChannelsWithAlpha:
            success = program->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, FLIP_COLOR_CHANNELS_WITH_ALPHA_SHADER);
            break;
        case ColorShader_RGB32ToRGBX8888:
            success = program->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, RGB32_TO_RGBA8888_SHADER);
            break;
        case ColorShader_RGB32ToRGBX8888_Premult:
            success = program->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, RGB32_TO_RGBA8888_PREMULT_SHADER);
            break;
        case ColorShader_RedAndBlueSwap:
            success = program->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, RED_AND_BLUE_SWAP_SHADER);
            break;
        default:
            qWarning() << "No color shader type" << i;
            break;
        }

        if (!success) {
            qWarning() << "Failed to compile fragment shader" << i << "hence using defaults. Reason:";
            qWarning() << program->log();
            return false;
        }

        gl->glBindAttribLocation(program->programId(), 0, "vertexCoord");                                                 
        gl->glBindAttribLocation(program->programId(), 1, "textureCoord");                                                 

        success = program->link();
        if (!success) {
            qWarning() << "Failed to link shader" << i << "hence using defaults. Reason:";
            qWarning() << program->log();
            return false;
        }

        auto bundle = std::make_shared<ShaderBundle>(
            program,
            0,
            1,
            gl->glGetUniformLocation(program->programId(), "textureSampler"),
            gl->glGetUniformLocation(program->programId(), "hasAlpha")
        );
        m_cachedShaders[(ColorShader)i] = bundle;
    }
    return true;
}
