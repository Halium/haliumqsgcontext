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

#include "plugin.h"
#include "context.h"

LomiriContextPlugin::LomiriContextPlugin(QObject *parent)
    : QSGContextPlugin(parent)
{
}

QStringList LomiriContextPlugin::keys() const
{
    return QStringList() << QLatin1String("haliumqsgcontext");
}

QSGContext* LomiriContextPlugin::create(const QString&) const
{
    if (!instance)
        instance = new Context();
    return instance;
}

QQuickTextureFactory* LomiriContextPlugin::createTextureFactoryFromImage(const QImage &image)
{
    if (!instance)
        return nullptr;
    return instance->createTextureFactory(image);
}

Context *LomiriContextPlugin::instance = 0;
