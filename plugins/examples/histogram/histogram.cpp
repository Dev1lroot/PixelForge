#include "histogram.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <algorithm>

namespace Histogram {

Counts compute(const QImage &image)
{
    Counts c;
    const QImage img = image.convertToFormat(QImage::Format_ARGB32);   // unpremultiplied values
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb p = row[x];
            if (qAlpha(p) == 0)
                continue;
            ++c.bins[0][qRed(p)];
            ++c.bins[1][qGreen(p)];
            ++c.bins[2][qBlue(p)];
            ++c.bins[3][qGray(p)];
            ++c.pixels;
        }
    }
    return c;
}

// ---------------------------------------------------------------- HistogramView

HistogramView::HistogramView(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(180, 100);
}

void HistogramView::setCounts(const Counts &c)
{
    m_counts = c;
    update();
}

void HistogramView::setChannel(int channel)
{
    m_channel = channel;
    update();
}

void HistogramView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().base());
    p.setPen(palette().mid().color());
    p.drawRect(rect().adjusted(0, 0, -1, -1));
    if (!m_counts.pixels)
        return;

    QVector<int> channels;
    if (m_channel == 4)
        channels = {0, 1, 2};
    else
        channels = {m_channel};
    // scale to the largest bin of the shown channels so one spike doesn't flatten everything
    int peak = 1;
    for (int ch : channels)
        peak = std::max(peak, *std::max_element(m_counts.bins[ch].begin(), m_counts.bins[ch].end()));

    const QColor colors[4] = {QColor(220, 40, 40), QColor(40, 170, 40), QColor(40, 80, 230), palette().text().color()};
    const QRectF area = QRectF(rect()).adjusted(2, 2, -2, -2);
    p.setRenderHint(QPainter::Antialiasing);
    p.setCompositionMode(QPainter::CompositionMode_Multiply);
    for (int ch : channels) {
        QPainterPath path;
        path.moveTo(area.bottomLeft());
        for (int v = 0; v < 256; ++v) {
            const double x = area.left() + area.width() * v / 255.0;
            const double h = area.height() * m_counts.bins[ch][v] / peak;
            path.lineTo(x, area.bottom() - h);
        }
        path.lineTo(area.bottomRight());
        path.closeSubpath();
        QColor fill = colors[ch];
        fill.setAlpha(channels.size() > 1 ? 150 : 200);
        p.fillPath(path, fill);
    }
}

// ---------------------------------------------------------------- Panel

Panel::Panel(PixelForge::Host *host, QWidget *parent) : QWidget(parent), m_host(host)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    auto *row = new QHBoxLayout;
    auto *channel = new QComboBox;
    channel->addItems({tr("Red"), tr("Green"), tr("Blue"), tr("Luminance"), tr("RGB")});
    channel->setCurrentIndex(4);
    m_source = new QComboBox;
    m_source->addItems({tr("Image"), tr("Active layer")});
    row->addWidget(channel);
    row->addWidget(m_source);
    layout->addLayout(row);
    m_view = new HistogramView;
    layout->addWidget(m_view, 1);
    m_info = new QLabel;
    layout->addWidget(m_info);

    connect(channel, &QComboBox::currentIndexChanged, m_view, &HistogramView::setChannel);
    connect(m_source, &QComboBox::currentIndexChanged, this, &Panel::reload);
}

void Panel::reload()
{
    // the host calls us for every edit, so skip the work while the window is closed
    if (!isVisible())
        return;
    QImage image;
    if (m_host->hasDocument()) {
        if (m_source->currentIndex() == 0) {
            image = m_host->flattenedImage();
        } else {
            const QVector<PixelForge::LayerInfo> layers = m_host->layers();
            const int active = m_host->activeLayer();
            if (active >= 0 && active < layers.size())
                image = layers[active].image;
        }
    }
    const Counts c = compute(image);
    m_view->setCounts(c);
    m_info->setText(image.isNull() ? tr("No image") : tr("%1 opaque pixels").arg(c.pixels));
}

void Panel::showEvent(QShowEvent *)
{
    reload();                           // catch up on edits made while the window was hidden
}

} // namespace Histogram

void HistogramPlugin::initialize(PixelForge::Host *host)
{
    auto *panel = new Histogram::Panel(host);
    host->addPanel("histogram", tr("&Histogram"), panel, QKeySequence(Qt::Key_F11), QIcon());
    host->onDocumentChanged(panel, [panel] { panel->reload(); });
}
