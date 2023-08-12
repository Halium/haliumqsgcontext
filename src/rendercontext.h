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

#ifndef RENDERCONTEXT_H
#define RENDERCONTEXT_H

#include <private/qsgdefaultcontext_p.h>
#include <private/qsgdefaultrendercontext_p.h>

#include <QOpenGLDebugLogger>

#undef None
#include <deviceinfo/deviceinfo.h>

#include "gralloctexture.h"

class RenderContext : public QSGDefaultRenderContext
{
public:
    explicit RenderContext(QSGContext* context);

    QSGTexture* createTexture(const QImage &image, uint flags = QSGRenderContext::CreateTexture_Alpha) const override;

private:
    enum Quirk {
        NoQuirk = 0x0,
        DisableConversionShaders = 0x1,
        UseRtScheduling = 0x2
    };
    Q_DECLARE_FLAGS(Quirks, Quirk)

    void messageReceived(const QOpenGLDebugMessage &debugMessage);

    bool compileColorShaders() const;
    bool init() const;

    bool mutable m_logging;
    QOpenGLDebugLogger mutable m_glLogger;
    ShaderCache mutable m_cachedShaders;
    GLint mutable m_maxTextureSize;
    bool mutable m_libuiFound;
    RenderContext::Quirks mutable m_quirks;
    DeviceInfo m_deviceInfo;
    mutable GrallocTextureCreator* m_textureCreator;
    mutable bool m_initialized;
    mutable bool m_colorShadersBuilt;
};

#endif
