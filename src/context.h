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

#ifndef CONTEXT_H
#define CONTEXT_H

#include <private/qsgdefaultcontext_p.h>
#include <QtCore/QAnimationDriver>

class RenderContext;

class Context : public QSGDefaultContext
{
    Q_OBJECT

public:
    explicit Context(QObject *parent = 0);

    QAnimationDriver* createAnimationDriver(QObject *parent) override;
    QSGRenderContext* createRenderContext() override;
    QQuickTextureFactory* createTextureFactory(const QImage &image);

private:
    bool m_useHaliumQsgAnimationDriver;
};

#endif
