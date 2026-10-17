#pragma once

#include <QFrame>
#include <QListWidget>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QPointer>

class QToolButton;
class QButtonGroup;
class QGraphicsOpacityEffect;
class QVBoxLayout;
class QAction;
class MainWindow;
class Document;

// A small floating tool window living inside the workspace, mimicking the classic
// translucent palette windows: dark frame, light caption, red close button.
class FloatingPanel : public QFrame
{
    Q_OBJECT
public:
    FloatingPanel(const QString &title, QWidget *workspace);
    void setContent(QWidget *w);
    void setResizable(bool r) { m_resizable = r; }
    void setTranslucency(bool enabled);
    void setAnchors(Qt::Alignment a) { m_anchor = a; }
    void placeAnchored();                 // reposition according to anchor after workspace resize
    void rememberAnchor();                // compute anchor from current position
    void keepInside();
    void setHomeGeometry(const QRect &r, Qt::Alignment a);
    void resetToHome();

    static constexpr int Border = 4;
    static constexpr int TitleHeight = 18;

signals:
    void closeRequested();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    void updateOpacity();
    QString m_title;
    QToolButton *m_close;
    QWidget *m_content = nullptr;
    QVBoxLayout *m_layout;
    QGraphicsOpacityEffect *m_effect;
    bool m_translucent = true;
    bool m_resizable = false;
    enum { None, Move, Resize } m_drag = None;
    QPoint m_dragStart;
    QRect m_geomStart;
    Qt::Alignment m_anchor = Qt::AlignLeft | Qt::AlignTop;
    QPoint m_anchorOffset;
    QRect m_home;
    Qt::Alignment m_homeAnchor = Qt::AlignLeft | Qt::AlignTop;
    QTimer m_hoverCheck;
};

class ToolsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ToolsPanel(MainWindow *main);
    void setCurrent(int toolId);

private:
    QButtonGroup *m_group;
};

class HistoryList : public QListWidget
{
    Q_OBJECT
public:
    using QListWidget::QListWidget;
};

class HistoryPanel : public QWidget
{
    Q_OBJECT
public:
    HistoryPanel(MainWindow *main, QAction *first, QAction *undo, QAction *redo, QAction *last);
    void setDocument(Document *doc);

private:
    void rebuild();
    MainWindow *m_main;
    QPointer<Document> m_doc;
    HistoryList *m_list;
    bool m_updating = false;
};

class LayerDelegate : public QStyledItemDelegate
{
public:
    explicit LayerDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override;
    static QRect checkRect(const QRect &itemRect);
};

class LayerList : public QListWidget
{
    Q_OBJECT
public:
    explicit LayerList(QWidget *parent);

signals:
    void visibilityClicked(int row);
    void propertiesRequested(int row);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
};

class LayersPanel : public QWidget
{
    Q_OBJECT
public:
    LayersPanel(MainWindow *main, const QList<QAction *> &actions);
    void setDocument(Document *doc);

private:
    void rebuild();
    void refreshThumbs();
    MainWindow *m_main;
    QPointer<Document> m_doc;
    LayerList *m_list;
    QTimer m_thumbTimer;
    bool m_updating = false;
};
