#pragma once

#include <QWidget>
#include <QImage>

class ColorState;
class QComboBox;
class QPushButton;
class QSpinBox;
class QLineEdit;
class QLabel;

class ColorWheel : public QWidget
{
    Q_OBJECT
public:
    explicit ColorWheel(QWidget *parent = nullptr);
    void setHsv(double h, double s, double v);
    double hue() const { return m_h; }
    double sat() const { return m_s; }
    QSize sizeHint() const override { return QSize(132, 132); }

signals:
    void hsChanged(double h, double s);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    void pick(const QPointF &p);
    void rebuild();
    double m_h = 0, m_s = 0, m_v = 1;
    QImage m_cache;
    double m_cacheV = -1;
};

class SwatchWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SwatchWidget(ColorState *cs, QWidget *parent = nullptr);
    QSize sizeHint() const override { return QSize(52, 52); }
    void setEditingSecondary(bool s);

signals:
    void selectPrimary();
    void selectSecondary();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;

private:
    QRect primaryRect() const { return QRect(2, 2, 28, 28); }
    QRect secondaryRect() const { return QRect(18, 18, 28, 28); }
    QRect swapRect() const { return QRect(34, 2, 14, 14); }
    QRect resetRect() const { return QRect(2, 34, 14, 14); }
    ColorState *m_cs;
    bool m_secondary = false;
};

class GradientSlider : public QWidget
{
    Q_OBJECT
public:
    GradientSlider(int maximum, QWidget *parent = nullptr);
    void setStops(const QGradientStops &stops, bool alphaBackdrop = false);
    void setValue(int v);
    int value() const { return m_value; }
    QSize sizeHint() const override { return QSize(110, 18); }

signals:
    void valueChanged(int v);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;

private:
    void setFromPos(int x);
    int m_max, m_value = 0;
    QGradientStops m_stops;
    bool m_alpha = false;
};

class PaletteWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PaletteWidget(ColorState *cs, QWidget *parent = nullptr);
    QSize sizeHint() const override;
    bool assignMode = false;

signals:
    void picked(int index, Qt::MouseButton button, Qt::KeyboardModifiers mods);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    int indexAt(const QPoint &p) const;
    ColorState *m_cs;
    static constexpr int Cols = 16, CellW = 12, CellH = 10;
};

class ColorsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ColorsPanel(ColorState *cs, QWidget *parent = nullptr);
    bool expanded() const { return m_expanded; }

signals:
    void sizeChangedRequest();

private:
    void syncFromState();
    void setCurrentColor(const QColor &c);
    QColor currentColor() const;
    void setExpanded(bool e);
    void updateEditors();
    void paletteMenu();

    ColorState *m_cs;
    bool m_secondary = false;
    bool m_expanded = false;
    bool m_updating = false;
    double m_h = 0, m_s = 0, m_v = 0;   // kept separately so hue survives greys

    QComboBox *m_which;
    QPushButton *m_more;
    SwatchWidget *m_swatch;
    ColorWheel *m_wheel;
    PaletteWidget *m_palette;
    QWidget *m_extra;
    GradientSlider *m_sl[7];            // R G B H S V A
    QSpinBox *m_sp[7];
    QLineEdit *m_hex;
};
