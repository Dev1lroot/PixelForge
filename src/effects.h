#pragma once

#include <QDialog>
#include <QImage>
#include <QVariantMap>
#include <QKeySequence>
#include <QTimer>
#include <functional>

class Document;
class MainWindow;

struct EffectParam {
    enum Type { Int, Double, Bool, Choice, Angle, Seed, Curve };
    QString key;
    QString label;
    Type type = Int;
    double min = 0, max = 100, def = 0;
    int decimals = 0;
    QStringList choices;
};

// src/dst are Format_ARGB32 (straight alpha), document sized. Only pixels inside roi need writing.
using EffectFn = std::function<void(const QImage &src, QImage &dst, const QRect &roi, const QVariantMap &p)>;

struct EffectDef {
    QString name;
    QString submenu;            // "" for adjustments, otherwise effect category
    QString icon;
    QKeySequence shortcut;
    bool adjustment = false;
    QVector<EffectParam> params;
    EffectFn apply;
};

const QVector<EffectDef> &effectRegistry();

// Applies an effect to the active layer of `doc`, honouring the selection (antialiased mask).
void runEffect(Document *doc, const EffectDef &def, const QVariantMap &params, const QImage &backup);

class EffectDialog : public QDialog
{
    Q_OBJECT
public:
    EffectDialog(MainWindow *main, Document *doc, const EffectDef &def, const QVariantMap &initial, QWidget *parent);
    QVariantMap values() const { return m_values; }

protected:
    void done(int r) override;

private:
    void schedulePreview();
    void preview();

    MainWindow *m_main;
    Document *m_doc;
    const EffectDef &m_def;
    QVariantMap m_values;
    QImage m_backup;
    QTimer m_timer;
};

// curve editor widget used by the Curves adjustment
class CurveEditor : public QWidget
{
    Q_OBJECT
public:
    explicit CurveEditor(QWidget *parent = nullptr);
    QVariantList points() const;
    void setPoints(const QVariantList &pts);
    QSize sizeHint() const override { return QSize(258, 258); }

signals:
    void changed();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    QPointF toWidget(const QPointF &p) const;
    QPointF fromWidget(const QPointF &p) const;
    QVector<QPointF> m_pts;
    int m_drag = -1;
};

QVector<int> curveLut(const QVariantList &pts);
