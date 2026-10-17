#pragma once
// Example of a floating tool window that follows the current document.

#include "pluginapi.h"

#include <QObject>
#include <QWidget>
#include <array>

class QComboBox;
class QLabel;

namespace Histogram {

// counts[channel][value] for red, green, blue, luminance; transparent pixels are skipped
struct Counts {
    std::array<std::array<int, 256>, 4> bins{};
    int pixels = 0;
};
Counts compute(const QImage &image);

class HistogramView : public QWidget
{
    Q_OBJECT
public:
    explicit HistogramView(QWidget *parent = nullptr);
    void setCounts(const Counts &c);
    void setChannel(int channel);       // 0..3 = R, G, B, luminance; 4 = RGB overlaid
    QSize sizeHint() const override { return QSize(260, 140); }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    Counts m_counts;
    int m_channel = 4;
};

class Panel : public QWidget
{
    Q_OBJECT
public:
    explicit Panel(PixelForge::Host *host, QWidget *parent = nullptr);
    void reload();

protected:
    void showEvent(QShowEvent *) override;

private:
    PixelForge::Host *m_host;
    HistogramView *m_view;
    QComboBox *m_source;
    QLabel *m_info;
};

} // namespace Histogram

class HistogramPlugin : public QObject, public PixelForge::Plugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PixelForgePlugin_iid FILE "histogram.json")
    Q_INTERFACES(PixelForge::Plugin)
public:
    QString name() const override { return "Histogram"; }
    QString version() const override { return "1.0"; }
    QString description() const override { return "Example plugin: floating window with a live RGB histogram."; }
    void initialize(PixelForge::Host *host) override;
};
