#ifndef TEXTUREFACTORY_H
#define TEXTUREFACTORY_H

#include <QQuickTextureFactory>
#include <QImage>
#include <QSGTexture>

#include <memory>

#include "rendercontext.h"
#include "gralloctexture.h"

class TextureFactory : public QQuickTextureFactory
{
    Q_OBJECT

public:
    TextureFactory(const QImage& image);

    virtual QSGTexture* createTexture(QQuickWindow *window) const override;
    virtual int textureByteCount() const override;
    virtual QSize textureSize() const override;
    virtual QImage image() const override;

private:
    RenderContext* m_renderContext;
    QImage m_image;
};

#endif
