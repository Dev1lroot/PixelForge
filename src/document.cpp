#include "document.h"

#include <QPainter>
#include <QFileInfo>
#include <cmath>

QString blendModeName(BlendMode m)
{
    switch (m) {
    case BlendMode::Normal: return QObject::tr("Normal");
    case BlendMode::Multiply: return QObject::tr("Multiply");
    case BlendMode::Additive: return QObject::tr("Additive");
    case BlendMode::ColorBurn: return QObject::tr("Color Burn");
    case BlendMode::ColorDodge: return QObject::tr("Color Dodge");
    case BlendMode::Reflect: return QObject::tr("Reflect");
    case BlendMode::Glow: return QObject::tr("Glow");
    case BlendMode::Overlay: return QObject::tr("Overlay");
    case BlendMode::Difference: return QObject::tr("Difference");
    case BlendMode::Negation: return QObject::tr("Negation");
    case BlendMode::Lighten: return QObject::tr("Lighten");
    case BlendMode::Darken: return QObject::tr("Darken");
    case BlendMode::Screen: return QObject::tr("Screen");
    case BlendMode::Xor: return QObject::tr("Xor");
    default: return QString();
    }
}

QString blendModeSvgOp(BlendMode m)
{
    switch (m) {
    case BlendMode::Multiply: return "svg:multiply";
    case BlendMode::Additive: return "svg:plus";
    case BlendMode::ColorBurn: return "svg:color-burn";
    case BlendMode::ColorDodge: return "svg:color-dodge";
    case BlendMode::Overlay: return "svg:overlay";
    case BlendMode::Difference: return "svg:difference";
    case BlendMode::Lighten: return "svg:lighten";
    case BlendMode::Darken: return "svg:darken";
    case BlendMode::Screen: return "svg:screen";
    case BlendMode::Xor: return "svg:xor";
    case BlendMode::Reflect: return "pixelforge:reflect";
    case BlendMode::Glow: return "pixelforge:glow";
    case BlendMode::Negation: return "pixelforge:negation";
    default: return "svg:src-over";
    }
}

BlendMode blendModeFromSvgOp(const QString &op)
{
    for (int i = 0; i < int(BlendMode::Count); ++i)
        if (blendModeSvgOp(BlendMode(i)) == op)
            return BlendMode(i);
    return BlendMode::Normal;
}

static bool painterMode(BlendMode m, QPainter::CompositionMode &out)
{
    switch (m) {
    case BlendMode::Normal: out = QPainter::CompositionMode_SourceOver; return true;
    case BlendMode::Multiply: out = QPainter::CompositionMode_Multiply; return true;
    case BlendMode::ColorBurn: out = QPainter::CompositionMode_ColorBurn; return true;
    case BlendMode::ColorDodge: out = QPainter::CompositionMode_ColorDodge; return true;
    case BlendMode::Overlay: out = QPainter::CompositionMode_Overlay; return true;
    case BlendMode::Difference: out = QPainter::CompositionMode_Difference; return true;
    case BlendMode::Lighten: out = QPainter::CompositionMode_Lighten; return true;
    case BlendMode::Darken: out = QPainter::CompositionMode_Darken; return true;
    case BlendMode::Screen: out = QPainter::CompositionMode_Screen; return true;
    default: return false;
    }
}

static inline float blendChannel(BlendMode m, float d, float s)
{
    switch (m) {
    case BlendMode::Additive: return std::min(1.0f, d + s);
    case BlendMode::Reflect: return s >= 1.0f ? 1.0f : std::min(1.0f, d * d / (1.0f - s));
    case BlendMode::Glow: return d >= 1.0f ? 1.0f : std::min(1.0f, s * s / (1.0f - d));
    case BlendMode::Negation: return 1.0f - std::fabs(1.0f - d - s);
    case BlendMode::Xor: return float(int(d * 255.0f + 0.5f) ^ int(s * 255.0f + 0.5f)) / 255.0f;
    default: return s;
    }
}

void blendLayer(QImage &dst, const Layer &layer, const QRect &rect)
{
    QRect r = rect.intersected(dst.rect()).intersected(layer.image.rect());
    if (r.isEmpty() || !layer.visible || layer.opacity <= 0)
        return;
    QPainter::CompositionMode cm;
    if (painterMode(layer.blend, cm)) {
        QPainter p(&dst);
        p.setCompositionMode(cm);
        p.setOpacity(layer.opacity / 255.0);
        p.drawImage(r.topLeft(), layer.image, r);
        return;
    }
    const float op = layer.opacity / 255.0f;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        QRgb *d = reinterpret_cast<QRgb *>(dst.scanLine(y));
        const QRgb *s = reinterpret_cast<const QRgb *>(layer.image.constScanLine(y));
        for (int x = r.left(); x <= r.right(); ++x) {
            QRgb sp = s[x];
            int sa8 = qAlpha(sp);
            if (!sa8)
                continue;
            QRgb dp = d[x];
            float as = sa8 / 255.0f * op;
            float ad = qAlpha(dp) / 255.0f;
            float sc[3] = {qRed(sp) / 255.0f, qGreen(sp) / 255.0f, qBlue(sp) / 255.0f};
            float dc[3] = {qRed(dp) / 255.0f, qGreen(dp) / 255.0f, qBlue(dp) / 255.0f};
            float sa0 = sa8 / 255.0f;
            float out[3];
            for (int c = 0; c < 3; ++c) {
                float cs = sc[c] / sa0;                      // unpremultiply
                float cd = ad > 0 ? dc[c] / ad : 0.0f;
                float b = blendChannel(layer.blend, cd, cs);
                out[c] = as * (1 - ad) * cs + ad * (1 - as) * cd + as * ad * b;
            }
            float ao = as + ad - as * ad;
            auto cl = [](float v) { return std::clamp(int(v * 255.0f + 0.5f), 0, 255); };
            int a = cl(ao);
            d[x] = qRgba(std::min(cl(out[0]), a), std::min(cl(out[1]), a), std::min(cl(out[2]), a), a);
        }
    }
}

Document::Document(const QSize &size, const QColor &background, QObject *parent)
    : QObject(parent), m_size(size)
{
    Layer l;
    l.image = blankLayerImage();
    l.image.fill(background);
    l.name = tr("Background");
    m_layers.append(l);
    m_composite = QImage(m_size, QImage::Format_ARGB32_Premultiplied);
    invalidate();
}

Document::Document(const QVector<Layer> &layers, const QSize &size, QObject *parent)
    : QObject(parent), m_size(size), m_layers(layers)
{
    m_active = m_layers.size() - 1;
    m_layerCounter = m_layers.size();
    m_composite = QImage(m_size, QImage::Format_ARGB32_Premultiplied);
    invalidate();
}

QImage Document::blankLayerImage() const
{
    QImage img(m_size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    return img;
}

QString Document::nextLayerName()
{
    return tr("Layer %1").arg(++m_layerCounter);
}

void Document::setActiveIndex(int i)
{
    i = std::clamp(i, 0, int(m_layers.size()) - 1);
    if (i == m_active)
        return;
    m_active = i;
    emit layersChanged();
}

void Document::setSelection(const QPainterPath &path)
{
    m_selection = path;
    m_selection.setFillRule(Qt::WindingFill);
    m_hasSelection = !path.isEmpty();
    m_selMaskValid = false;
    emit selectionChanged();
}

void Document::clearSelection()
{
    m_selection = QPainterPath();
    m_hasSelection = false;
    m_selMaskValid = false;
    emit selectionChanged();
}

QPainterPath Document::selectionOrAll() const
{
    if (m_hasSelection)
        return m_selection;
    QPainterPath p;
    p.addRect(rect());
    return p;
}

QRect Document::selectionBounds() const
{
    if (!m_hasSelection)
        return rect();
    return m_selection.boundingRect().toAlignedRect().intersected(rect());
}

const QImage &Document::selectionMask()
{
    if (!m_selMaskValid) {
        m_selMask = QImage(m_size, QImage::Format_Alpha8);
        if (!m_hasSelection) {
            m_selMask.fill(Qt::white);
        } else {
            m_selMask.fill(Qt::transparent);
            QPainter p(&m_selMask);
            p.setRenderHint(QPainter::Antialiasing);
            p.fillPath(m_selection, Qt::black);
        }
        m_selMaskValid = true;
    }
    return m_selMask;
}

void Document::clipToSelection(QPainter &p) const
{
    if (m_hasSelection)
        p.setClipPath(m_selection);
}

const QImage &Document::composite()
{
    if (m_composite.size() != m_size) {
        m_composite = QImage(m_size, QImage::Format_ARGB32_Premultiplied);
        m_dirty = rect();
    }
    if (!m_dirty.isEmpty()) {
        QRect r = m_dirty.intersected(rect());
        {
            QPainter p(&m_composite);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.fillRect(r, Qt::transparent);
        }
        for (const Layer &l : m_layers)
            blendLayer(m_composite, l, r);
        m_dirty = QRect();
    }
    return m_composite;
}

void Document::invalidate(const QRect &r)
{
    QRect rr = r.isNull() ? rect() : r.intersected(rect());
    m_dirty = m_dirty.united(rr);
    emit imageChanged(rr);
}

QImage Document::flattened() const
{
    QImage out(m_size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    for (const Layer &l : m_layers)
        blendLayer(out, l, rect());
    return out;
}

QImage Document::thumbnail(int maxSide)
{
    return composite().scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

DocState Document::captureState() const
{
    DocState s;
    s.layers = m_layers;
    s.selection = m_selection;
    s.hasSelection = m_hasSelection;
    s.active = m_active;
    s.size = m_size;
    return s;
}

void Document::restoreState(const DocState &s)
{
    bool sizeChange = s.size != m_size;
    m_layers = s.layers;
    m_active = std::clamp(s.active, 0, int(m_layers.size()) - 1);
    m_size = s.size;
    m_selection = s.selection;
    m_hasSelection = s.hasSelection;
    m_selMaskValid = false;
    if (sizeChange)
        emit sizeChanged();
    invalidate();
    emit layersChanged();
    emit selectionChanged();
    emit stateRestored();
}

void Document::resetHistory(const QString &name, const QIcon &icon)
{
    m_history.clear();
    m_history.append({name, icon, captureState()});
    m_historyIndex = 0;
    m_savedIndex = 0;
    emit historyChanged();
    emit dirtyChanged();
}

void Document::pushHistory(const QString &name, const QIcon &icon)
{
    if (m_historyIndex < m_savedIndex)
        m_savedIndex = -1;                       // saved state is no longer reachable
    m_history.resize(m_historyIndex + 1);
    m_history.append({name, icon, captureState()});
    m_historyIndex = m_history.size() - 1;
    emit historyChanged();
    emit dirtyChanged();
}

void Document::replaceTopHistory()
{
    if (m_historyIndex < 0)
        return;
    m_history[m_historyIndex].state = captureState();
}

void Document::undo()
{
    if (canUndo())
        jumpTo(m_historyIndex - 1);
}

void Document::redo()
{
    if (canRedo())
        jumpTo(m_historyIndex + 1);
}

void Document::jumpTo(int index)
{
    if (index < 0 || index >= m_history.size() || index == m_historyIndex)
        return;
    m_historyIndex = index;
    restoreState(m_history[index].state);
    emit historyChanged();
    emit dirtyChanged();
}

void Document::setLayers(const QVector<Layer> &layers, const QSize &size, int active)
{
    bool sizeChange = size != m_size;
    m_layers = layers;
    m_size = size;
    m_active = std::clamp(active, 0, int(m_layers.size()) - 1);
    if (sizeChange) {
        m_selMaskValid = false;
        emit sizeChanged();
    }
    invalidate();
    emit layersChanged();
}

void Document::notifyLayersChanged()
{
    emit layersChanged();
}

void Document::notifyStructureChanged()
{
    invalidate();
    emit layersChanged();
}

QString Document::displayName() const
{
    if (filePath.isEmpty())
        return tr("Untitled");
    return QFileInfo(filePath).fileName();
}

void Document::markSaved()
{
    m_savedIndex = m_historyIndex;
    emit dirtyChanged();
}
