#pragma once

#include <QAbstractScrollArea>
#include <QWidget>
#include <QTimer>

class Document;
class MainWindow;
class CanvasView;

class Ruler : public QWidget
{
    Q_OBJECT
public:
    Ruler(Qt::Orientation o, CanvasView *view);
    static constexpr int Thickness = 18;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    Qt::Orientation m_orient;
    CanvasView *m_view;
};

class CanvasView : public QAbstractScrollArea
{
    Q_OBJECT
public:
    CanvasView(Document *doc, MainWindow *main, QWidget *parent = nullptr);

    Document *document() const { return m_doc; }
    MainWindow *main() const { return m_main; }

    double zoom() const { return m_zoom; }
    void setZoom(double z, const QPointF &viewAnchor = QPointF(-1, -1));
    void zoomIn(const QPointF &anchor = QPointF(-1, -1));
    void zoomOut(const QPointF &anchor = QPointF(-1, -1));
    void zoomToWindow();
    void actualSize();
    void zoomToRect(const QRectF &imageRect);
    static QVector<double> zoomPresets();

    QPointF viewToImage(const QPointF &v) const;
    QPointF imageToView(const QPointF &i) const;
    QRectF imageToView(const QRectF &r) const;
    QPointF imageOrigin() const;
    QRectF visibleImageRect() const;
    QPointF mouseImagePos() const { return m_mouseImage; }
    bool mouseInside() const { return m_mouseInside; }

    void setShowGrid(bool on);
    bool showGrid() const { return m_grid; }
    void setShowRulers(bool on);
    void setTiled(bool on);
    bool tiled() const { return m_tiled; }
    bool showRulers() const { return m_rulers; }

    void updateImageRect(const QRectF &imageRect);
    void updateOverlay() { viewport()->update(); }
    void refreshCursor();
    void scrollBy(const QPointF &delta);

signals:
    void zoomChanged(double z);
    void mouseMoved(const QPointF &imagePos);

protected:
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void scrollContentsBy(int dx, int dy) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;
    bool viewportEvent(QEvent *e) override;

private:
    void updateScrollBars();
    void scrollToImagePoint(const QPointF &img, const QPointF &viewPos);
    QPointF scrollOrigin(const QSize &vp) const;
    void layoutRulers();
    void drawSelection(QPainter &p);

    Document *m_doc;
    MainWindow *m_main;
    double m_zoom = 1.0;
    bool m_grid = false;
    bool m_rulers = false;
    Ruler *m_hruler, *m_vruler;
    QPointF m_mouseImage;
    bool m_mouseInside = false;
    bool m_spacePan = false;
    bool m_panning = false;
    QPoint m_panLast;
    bool m_toolDrag = false;
    QSize m_vpSize;               // viewport size the scroll position was last laid out for
    void updateMip();
    void drawTile(QPainter &p, const QRect &exposed, const QPointF &origin);
    QPointF tileMargin() const;
    bool m_tiled = false;
    QImage m_mip;                 // downscaled composite used when zoomed out below 50%
    int m_mipLevel = 0;
    QRect m_mipDirty;
    QTimer m_antsTimer;
    int m_antsOffset = 0;
    QPixmap m_checker;
};
