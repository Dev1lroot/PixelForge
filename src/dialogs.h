#pragma once

#include <QDialog>
#include <QImage>
#include "document.h"

class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QRadioButton;
class QComboBox;
class QLineEdit;
class QSlider;
class QButtonGroup;

// Shared "pixel size / print size / resolution" editor used by New, Resize and Canvas Size.
class SizeEditor : public QWidget
{
    Q_OBJECT
public:
    SizeEditor(const QSize &initial, double dpi, bool percentMode, QWidget *parent = nullptr);
    QSize pixelSize() const;
    double dpi() const;

private:
    void fromPercent();
    void fromPixels(bool widthChanged);
    void fromPrint(bool widthChanged);
    void syncPrint();

    QSize m_base;
    QRadioButton *m_byPercent = nullptr, *m_byAbs = nullptr;
    QSpinBox *m_percent = nullptr;
    QCheckBox *m_aspect;
    QSpinBox *m_w, *m_h;
    QDoubleSpinBox *m_res, *m_pw, *m_ph;
    QComboBox *m_resUnit, *m_printUnit;
    bool m_busy = false;
};

class NewImageDialog : public QDialog
{
    Q_OBJECT
public:
    NewImageDialog(const QSize &initial, double dpi, QWidget *parent);
    QSize imageSize() const;
    double dpi() const;

private:
    SizeEditor *m_size;
};

class ResizeDialog : public QDialog
{
    Q_OBJECT
public:
    ResizeDialog(const QSize &cur, double dpi, QWidget *parent);
    QSize imageSize() const;
    double dpi() const;
    Qt::TransformationMode mode() const;
    bool nearest() const;

private:
    SizeEditor *m_size;
    QComboBox *m_resample;
};

class CanvasSizeDialog : public QDialog
{
    Q_OBJECT
public:
    CanvasSizeDialog(const QSize &cur, double dpi, QWidget *parent);
    QSize imageSize() const;
    double dpi() const;
    Qt::Alignment anchor() const { return m_anchor; }

private:
    void updateAnchorIcons();
    SizeEditor *m_size;
    Qt::Alignment m_anchor = Qt::AlignCenter;
    QButtonGroup *m_anchorGroup;
};

class LayerPropertiesDialog : public QDialog
{
    Q_OBJECT
public:
    LayerPropertiesDialog(Document *doc, QWidget *parent);
    bool changed() const;

protected:
    void done(int r) override;

private:
    void apply();
    Document *m_doc;
    Layer m_orig;
    QLineEdit *m_name;
    QCheckBox *m_visible;
    QComboBox *m_blend;
    QSlider *m_opSlider;
    QSpinBox *m_opSpin;
};

class RotateZoomDialog : public QDialog
{
    Q_OBJECT
public:
    RotateZoomDialog(Document *doc, QWidget *parent);

protected:
    void done(int r) override;

private:
    void apply();
    Document *m_doc;
    QImage m_backup;
    QDoubleSpinBox *m_angle, *m_zoom;
    QSpinBox *m_panX, *m_panY;
    QCheckBox *m_tile;
};

class SaveOptionsDialog : public QDialog
{
    Q_OBJECT
public:
    SaveOptionsDialog(const QString &format, QWidget *parent);
    int quality() const;

private:
    QSlider *m_q;
};

// Runs `dlg` like exec(), but without window modality. Desktop compositors (KWin, Mutter) dim the
// parent of a modal dialog, which tints the canvas behind a live preview; instead, input to every
// other window is swallowed until the dialog closes.
int execUndimmed(QDialog &dlg);
