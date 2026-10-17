#include "animpanel.h"
#include "document.h"
#include "icons.h"

#include <QPainter>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QToolButton>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>

class AnimView : public QWidget
{
public:
    explicit AnimView(AnimationPanel *panel) : QWidget(panel), m_panel(panel)
    {
        setMinimumSize(96, 96);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x5a, 0x60, 0x6b));
        QImage f = m_panel->frame(m_panel->m_frame);
        if (f.isNull())
            return;
        QSizeF s = QSizeF(f.size()).scaled(QSizeF(size()) - QSizeF(8, 8), Qt::KeepAspectRatio);
        if (m_panel->m_pixelated->isChecked() && s.width() >= f.width()) {
            // integer upscale keeps pixel art crisp
            double k = std::floor(s.width() / f.width());
            s = QSizeF(f.width() * k, f.height() * k);
        }
        QRectF r(QPointF((width() - s.width()) / 2, (height() - s.height()) / 2), s);
        p.fillRect(r, QBrush(Icons::checkerboard(6)));
        p.setRenderHint(QPainter::SmoothPixmapTransform, !m_panel->m_pixelated->isChecked());
        p.drawImage(r, f);
        p.setPen(QColor(30, 30, 30));
        p.drawRect(r.adjusted(-0.5, -0.5, 0.5, 0.5));
    }

private:
    AnimationPanel *m_panel;
};

AnimationPanel::AnimationPanel(QWidget *parent) : QWidget(parent)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(4);

    m_mode = new QComboBox;
    m_mode->addItems({tr("Vertical strip (frames stacked)"), tr("Horizontal strip (frames side by side)"),
                      tr("Layers as frames"), tr("Scroll vertically (wrap)"), tr("Scroll horizontally (wrap)")});
    v->addWidget(m_mode);

    auto *g = new QGridLayout;
    g->setHorizontalSpacing(4);
    g->setVerticalSpacing(2);
    m_framesLabel = new QLabel(tr("Frames:"));
    m_frames = new QSpinBox;
    m_frames->setRange(0, 1024);
    m_frames->setSpecialValueText(tr("Auto"));
    m_frames->setToolTip(tr("Number of frames in the strip. Auto assumes square frames."));
    m_stepLabel = new QLabel(tr("Step (px):"));
    m_step = new QSpinBox;
    m_step->setRange(1, 4096);
    m_step->setValue(1);
    m_step->setToolTip(tr("Pixels the texture scrolls per frame"));
    m_fps = new QSpinBox;
    m_fps->setRange(1, 60);
    m_fps->setValue(8);
    m_fps->setSuffix(tr(" fps"));
    g->addWidget(m_framesLabel, 0, 0);
    g->addWidget(m_frames, 0, 1);
    g->addWidget(m_stepLabel, 1, 0);
    g->addWidget(m_step, 1, 1);
    g->addWidget(new QLabel(tr("Speed:")), 2, 0);
    g->addWidget(m_fps, 2, 1);
    v->addLayout(g);

    m_keepBg = new QCheckBox(tr("Bottom layer is background"));
    m_keepBg->setToolTip(tr("Draw the bottom layer under every frame instead of using it as a frame"));
    m_skipHidden = new QCheckBox(tr("Skip hidden layers"));
    m_pixelated = new QCheckBox(tr("Pixelated"));
    m_pixelated->setChecked(true);
    v->addWidget(m_keepBg);
    v->addWidget(m_skipHidden);

    m_view = new AnimView(this);
    v->addWidget(m_view, 1);

    auto *controls = new QHBoxLayout;
    controls->setSpacing(1);
    auto button = [&](const QString &icon, const QString &tip) {
        auto *b = new QToolButton;
        b->setIcon(Icons::get(icon));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        controls->addWidget(b);
        return b;
    };
    QToolButton *back = button("stepback", tr("Previous frame"));
    m_play = button("pause", tr("Play / Pause"));
    QToolButton *fwd = button("stepfwd", tr("Next frame"));
    m_counter = new QLabel;
    m_counter->setMinimumWidth(50);
    controls->addSpacing(4);
    controls->addWidget(m_counter);
    controls->addStretch();
    controls->addWidget(m_pixelated);
    v->addLayout(controls);

    connect(&m_timer, &QTimer::timeout, this, [this] { step(1); });
    connect(m_play, &QToolButton::clicked, this, [this] { setPlaying(!m_playing); });
    connect(back, &QToolButton::clicked, this, [this] { setPlaying(false); step(-1); });
    connect(fwd, &QToolButton::clicked, this, [this] { setPlaying(false); step(1); });
    connect(m_mode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        m_frame = 0;
        updateUi();
    });
    for (QSpinBox *sb : {m_frames, m_step})
        connect(sb, qOverload<int>(&QSpinBox::valueChanged), this, &AnimationPanel::updateUi);
    connect(m_fps, qOverload<int>(&QSpinBox::valueChanged), this, [this](int f) { m_timer.setInterval(1000 / f); });
    for (QCheckBox *cb : {m_keepBg, m_skipHidden, m_pixelated})
        connect(cb, &QCheckBox::toggled, this, &AnimationPanel::updateUi);
    m_timer.setInterval(1000 / m_fps->value());
    updateUi();
}

void AnimationPanel::setDocument(Document *doc)
{
    if (m_doc)
        disconnect(m_doc, nullptr, this, nullptr);
    m_doc = doc;
    if (doc) {
        connect(doc, &Document::imageChanged, m_view, qOverload<>(&QWidget::update));
        connect(doc, &Document::layersChanged, this, &AnimationPanel::updateUi);
        connect(doc, &Document::sizeChanged, this, &AnimationPanel::updateUi);
    }
    m_frame = 0;
    updateUi();
}

void AnimationPanel::setMode(Mode m) { m_mode->setCurrentIndex(int(m)); }

void AnimationPanel::setPlaying(bool on)
{
    m_playing = on;
    m_play->setIcon(Icons::get(on ? "pause" : "play"));
    if (on && isVisible())
        m_timer.start();
    else
        m_timer.stop();
}

void AnimationPanel::step(int delta)
{
    int n = frameCount();
    if (n <= 0)
        return;
    m_frame = ((m_frame + delta) % n + n) % n;
    m_counter->setText(QString("%1 / %2").arg(m_frame + 1).arg(n));
    m_view->update();
}

void AnimationPanel::showEvent(QShowEvent *)
{
    if (m_playing)
        m_timer.start();
}

void AnimationPanel::hideEvent(QHideEvent *)
{
    m_timer.stop();            // never animate while hidden
}

static QVector<int> frameLayers(const Document *d, bool keepBg, bool skipHidden)
{
    QVector<int> idx;
    for (int i = keepBg ? 1 : 0; i < d->layerCount(); ++i)
        if (!skipHidden || d->layers()[i].visible)
            idx << i;
    return idx;
}

int AnimationPanel::frameCount() const
{
    if (!m_doc)
        return 0;
    const int w = m_doc->width(), h = m_doc->height();
    const int manual = m_frames->value();
    switch (Mode(m_mode->currentIndex())) {
    case VerticalStrip:
        return std::clamp(manual ? manual : (h % w == 0 ? h / w : 1), 1, h);
    case HorizontalStrip:
        return std::clamp(manual ? manual : (w % h == 0 ? w / h : 1), 1, w);
    case LayerFrames:
        return std::max(1, int(frameLayers(m_doc, m_keepBg->isChecked(), m_skipHidden->isChecked()).size()));
    case ScrollVertical:
        return std::max(1, (h + m_step->value() - 1) / m_step->value());
    case ScrollHorizontal:
        return std::max(1, (w + m_step->value() - 1) / m_step->value());
    }
    return 1;
}

QImage AnimationPanel::frame(int i) const
{
    if (!m_doc)
        return QImage();
    Document *d = m_doc;
    const int w = d->width(), h = d->height();
    const int n = frameCount();
    i = std::clamp(i, 0, n - 1);
    switch (Mode(m_mode->currentIndex())) {
    case VerticalStrip: {
        int fh = std::max(1, h / n);
        return d->composite().copy(0, i * fh, w, fh);
    }
    case HorizontalStrip: {
        int fw = std::max(1, w / n);
        return d->composite().copy(i * fw, 0, fw, h);
    }
    case LayerFrames: {
        QVector<int> idx = frameLayers(d, m_keepBg->isChecked(), m_skipHidden->isChecked());
        QImage out(d->size(), QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::transparent);
        if (m_keepBg->isChecked() && d->layerCount() > 0) {
            Layer bg = d->layers()[0];
            bg.visible = true;
            blendLayer(out, bg, out.rect());
        }
        if (!idx.isEmpty()) {
            Layer l = d->layers()[idx[std::min(i, int(idx.size()) - 1)]];
            l.visible = true;                // a frame shows even if hidden in the editor
            blendLayer(out, l, out.rect());
        }
        return out;
    }
    case ScrollVertical:
    case ScrollHorizontal: {
        const bool vert = m_mode->currentIndex() == ScrollVertical;
        const int off = i * m_step->value();
        QImage out(d->size(), QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::transparent);
        QPainter p(&out);
        const QImage &c = d->composite();
        if (vert) {
            int o = off % h;
            p.drawImage(0, -o, c);
            p.drawImage(0, h - o, c);
        } else {
            int o = off % w;
            p.drawImage(-o, 0, c);
            p.drawImage(w - o, 0, c);
        }
        return out;
    }
    }
    return QImage();
}

void AnimationPanel::updateUi()
{
    const Mode m = Mode(m_mode->currentIndex());
    const bool strip = m == VerticalStrip || m == HorizontalStrip;
    const bool scroll = m == ScrollVertical || m == ScrollHorizontal;
    m_frames->setVisible(strip);
    m_framesLabel->setVisible(strip);
    m_step->setVisible(scroll);
    m_stepLabel->setVisible(scroll);
    m_keepBg->setVisible(m == LayerFrames);
    m_skipHidden->setVisible(m == LayerFrames);
    step(0);
}
