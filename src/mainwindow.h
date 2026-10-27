#pragma once

#include <QMainWindow>
#include <QHash>
#include <QPointer>
#include <QTransform>
#include <functional>
#include <QVariantMap>
#include "settings.h"
#include "tools.h"

class Document;
class CanvasView;
class FloatingPanel;
class ToolsPanel;
class ColorsPanel;
class HistoryPanel;
class LayersPanel;
class QStackedWidget;
class QLabel;
class QComboBox;
class QToolButton;
class QToolBar;
class QMenu;
class QActionGroup;
struct EffectDef;

class ImageTabs : public QWidget
{
    Q_OBJECT
public:
    explicit ImageTabs(class MainWindow *main);
    QSize sizeHint() const override;
    void refresh() { update(); }

signals:
    void activated(int index);
    void closeRequested(int index);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    QRect boxRect(int i) const;
    QRect closeRect(int i) const;
    QRect arrowRect() const;
    MainWindow *m_main;
    int m_hover = -1;
};

class ToleranceSlider : public QWidget
{
    Q_OBJECT
public:
    explicit ToleranceSlider(QWidget *parent = nullptr);
    void setValue(int v);
    int value() const { return m_value; }
    QSize sizeHint() const override { return QSize(130, 18); }

signals:
    void valueChanged(int);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;

private:
    void setFromX(int x);
    int m_value = 50;
};

class Workspace : public QWidget
{
    Q_OBJECT
public:
    explicit Workspace(QWidget *parent) : QWidget(parent) {}
signals:
    void resized();
protected:
    void resizeEvent(QResizeEvent *) override { emit resized(); }
    void paintEvent(QPaintEvent *) override;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;

    Document *document() const;
    CanvasView *view() const;
    const QVector<Document *> &documents() const { return m_docs; }
    int currentIndex() const { return m_current; }

    ToolSettings &settings() { return m_settings; }
    ColorState &colors() { return m_colors; }
    Tool *tool(ToolId id) const { return m_tools[int(id)]; }
    Tool *currentTool() const { return m_tools.isEmpty() ? nullptr : m_tools[int(m_toolId)]; }
    void setActiveTool(ToolId id);
    void switchToPreviousTool();
    void commitTool(Tool *except = nullptr);
    void prepareHistoryNavigation();
    void setToolStatus(const QString &s);
    bool handleCanvasKey(QKeyEvent *e);

    void openFile(const QString &path);
    void addDocument(Document *doc, bool fit = true);
    void showLayerProperties();
    QAction *action(const QString &id) const { return m_actions.value(id); }
    // adds a floating tool window with a Window-menu toggle (also used by plugins)
    FloatingPanel *addFloatingPanel(const QString &id, const QString &title, QWidget *content, const QKeySequence &ks,
                                    const QIcon &icon, bool defaultVisible, const QSize &size);
    void addPluginAction(const QString &menu, QAction *action);
    // plugin drawing over the image, see PixelForge::Host::addCanvasOverlay
    void addCanvasOverlay(QObject *context, std::function<void(QPainter &, const QTransform &)> paint);
    void paintCanvasOverlays(QPainter &p, const QTransform &imageToView);
    void updateCanvases();
    // region labels for the UV Map flood mode, see PixelForge::Host::setFloodRegions
    void setFloodRegions(QObject *context, std::function<QImage(const QSize &)> regions);
    QImage floodRegions(const QSize &size) const;
    class AnimationPanel *animationPanel() const { return m_animPanel; }

protected:
    void closeEvent(QCloseEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void showEvent(QShowEvent *e) override;

private:
    // construction
    void createActions();
    void createMenus();
    QWidget *createTopArea();
    QToolBar *createMainToolbar();
    QToolBar *createOptionsBar();
    void createPanels();
    void createStatusBar();
    void applyStyle();
    QAction *act(const QString &id, const QString &text, const QString &icon = QString(),
                 const QKeySequence &ks = QKeySequence(), void (MainWindow::*slot)() = nullptr);

    // state updates
    void setCurrentDocument(int i);
    void updateTitle();
    void updateActions();
    void updateOptionsBar();
    void updateStatus();
    void updateZoomCombo();
    void layoutPanels(bool initial);
    void registerPanel(FloatingPanel *w, const QString &id, const QString &name, const QKeySequence &ks,
                       const QIcon &icon, bool defaultVisible);
    void showPlugins();
    QString formatLength(double px) const;

    // file
    void fileNew();
    void fileOpen();
    void addRecentFile(const QString &path);
    void removeRecentFile(const QString &path);
    void updateRecentMenu();
    bool fileSave();
    bool fileSaveAs();
    bool saveDocument(Document *doc, const QString &path);
    void fileClose();
    bool closeDocument(int index);
    void filePrint();

    // edit
    void editUndo();
    void editRedo();
    void editCut();
    void editCopy();
    void editCopyMerged();
    void editPaste();
    void editPasteNewLayer();
    void editPasteNewImage();
    void editSelectAll();
    void editDeselect();
    void editInvertSelection();
    void editEraseSelection();
    void editFillSelection();
    QImage selectionImage(const QImage &src) const;
    bool pasteImage(const QImage &img, bool newLayer);

    // view
    void viewZoomIn();
    void viewZoomOut();
    void viewZoomWindow();
    void viewActualSize();
    void viewZoomSelection();

    // image
    void imageCrop();
    void imageResize();
    void imageCanvasSize();
    void imageFlipH();
    void imageFlipV();
    void imageRotateCW();
    void imageRotateCCW();
    void imageRotate180();
    void imageFlatten();
    void transformAllLayers(const QTransform &t, const QSize &newSize, const QString &name, const QString &icon);

    // layers
    void layerAdd();
    void layerDelete();
    void layerDuplicate();
    void layerMergeDown();
    void layerImport();
    void layerFlipH();
    void layerFlipV();
    void layerRotateZoom();
    void layerCreateNormalMap();
    void layerMoveUp();
    void layerMoveDown();

    // effects
    void runEffectDef(const EffectDef &def, bool repeat = false);
    void repeatEffect();

    // window/help
    void resetPanels();
    void showHelp();
    void showAbout();

    ToolSettings m_settings;
    ColorState m_colors;
    QVector<Tool *> m_tools;
    ToolId m_toolId = ToolId::Brush;
    ToolId m_prevTool = ToolId::Brush;

    QVector<Document *> m_docs;
    QVector<CanvasView *> m_views;
    struct CanvasOverlay {
        QPointer<QObject> context;
        std::function<void(QPainter &, const QTransform &)> paint;
    };
    QVector<CanvasOverlay> m_canvasOverlays;
    QPointer<QObject> m_floodRegionContext;
    std::function<QImage(const QSize &)> m_floodRegions;
    int m_current = -1;

    Workspace *m_workspace;
    QStackedWidget *m_stack;
    FloatingPanel *m_toolsWin, *m_colorsWin, *m_historyWin, *m_layersWin;
    ToolsPanel *m_toolsPanel;
    ColorsPanel *m_colorsPanel;
    HistoryPanel *m_historyPanel;
    LayersPanel *m_layersPanel;
    ImageTabs *m_tabs;
    bool m_panelsPlaced = false;
    QVector<FloatingPanel *> m_panels;
    QStringList m_panelIds;
    QAction *m_panelSeparator = nullptr;
    class AnimationPanel *m_animPanel = nullptr;
    class PluginHost *m_pluginHost = nullptr;
    QHash<QString, QMenu *> m_menus;

    QHash<QString, QAction *> m_actions;
    QMenu *m_menuAdjust = nullptr, *m_menuEffects = nullptr;
    QToolBar *m_mainBar, *m_optBar;
    QComboBox *m_zoomCombo, *m_unitsCombo;
    QToolButton *m_toolPicker;
    QHash<quint32, QList<QAction *>> m_optActions;

    // option widgets
    QComboBox *m_brushWidth;
    QToolButton *m_aaButton, *m_blendButton, *m_selModeButton, *m_floodButton, *m_shapeButton;
    ToleranceSlider *m_tolerance, *m_hardness;
    QComboBox *m_fillCombo, *m_samplingCombo, *m_sampleLayerCombo, *m_afterPickCombo, *m_lineStyleCombo,
        *m_resampleCombo, *m_fontSizeCombo;
    class QFontComboBox *m_fontCombo;
    class QSpinBox *m_radiusSpin;
    QAction *m_gradTypes[5];
    QAction *m_gradColor, *m_gradTransp, *m_bold, *m_italic, *m_underline, *m_strike, *m_align[3];

    // status bar
    QLabel *m_statusIcon, *m_statusText, *m_selSize, *m_cursorPos, *m_zoomLabel;
    QString m_toolStatus;

    // effects
    const EffectDef *m_lastEffect = nullptr;
    QHash<QString, QVariantMap> m_effectValues;
    QString m_lastDir;
    QStringList m_recentFiles;
    QMenu *m_recentMenu = nullptr;
};
