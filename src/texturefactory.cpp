#include "texturefactory.h"

#include <QQuickWindow>

TextureFactory::TextureFactory(const QImage& image) : QQuickTextureFactory(), m_image(image)
{
}

QSGTexture* TextureFactory::createTexture(QQuickWindow *window) const
{
    QQuickWindow::CreateTextureOptions flags = 0;
    if (m_image.hasAlphaChannel())
        flags |= QQuickWindow::TextureHasAlphaChannel;

    auto texture = window->createTextureFromImage(m_image, flags);
    return texture;
}

int TextureFactory::textureByteCount() const
{
    return m_image.bytesPerLine() * m_image.height();
}

QSize TextureFactory::textureSize() const
{
    return m_image.size();
}

QImage TextureFactory::image() const
{
    return m_image;

#if 0 // TODO
    auto texture = static_cast<GrallocTexture*>(m_texture);
    if (!texture) {
        return m_image;
    }

    struct graphic_buffer* handle = texture->buffer();
    void* vmemAddr = nullptr;
    graphic_buffer_lock(handle, GRALLOC_USAGE_SW_READ_RARELY | GRALLOC_USAGE_SW_WRITE_NEVER, &vmemAddr);

    if (!vmemAddr) {
        return m_image;
    }

    const int width = graphic_buffer_get_width(handle);
    const int height = graphic_buffer_get_height(handle);
    const auto format = m_texture->hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32;

    const auto ret = QImage((const uchar*) vmemAddr, width, height, format).copy();
    graphic_buffer_unlock(handle);

    return ret;
#endif
}
