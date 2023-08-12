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

#include "context.h"
#include "animationdriver.h"
#include "rendercontext.h"
#include "texturefactory.h"

#include <QQuickWindow>

#undef None
#include <deviceinfo.h>

Context::Context(QObject* parent) : QSGDefaultContext(parent)
{
    DeviceInfo deviceInfo(DeviceInfo::None);
    m_useHaliumQsgAnimationDriver = (deviceInfo.get("HaliumQsgAnimationDriver", "true") == "true");
}

QAnimationDriver* Context::createAnimationDriver(QObject *parent)
{
    if (!m_useHaliumQsgAnimationDriver)
        return QSGDefaultContext::createAnimationDriver(parent);
    return new AnimationDriver(parent);
}

QSGRenderContext* Context::createRenderContext()
{
    return new RenderContext(this);
}

QQuickTextureFactory* Context::createTextureFactory(const QImage &image)
{
    return new TextureFactory(image);
}