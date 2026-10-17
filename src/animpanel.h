#pragma once

#include <QWidget>
#include <QPointer>
#include <QTimer>

class Document;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QLabel;
class QToolButton;

// Plays the current image as an animation: sprite strips, scrolling (wrapping) textures,
// or one frame per layer.
class AnimationPanel : public QWidget
{
    Q_OBJECT
public:
    enum Mode { VerticalStrip, HorizontalStrip, LayerFrames, ScrollVertical, ScrollHorizontal };

    explicit AnimationPanel(QWidget *parent = nullptr);
    void setDocument(Document *doc);

    int frameCount() const;
    QImage frame(int index) const;
    int currentFrame() const { return m_frame; }
    void setMode(Mode m);
    void setPlaying(bool on);
    void step(int delta);

protected:
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;

private:
    void updateUi();

    QPointer<Document> m_doc;
    int m_frame = 0;
    bool m_playing = true;
    QTimer m_timer;
    QComboBox *m_mode;
    QSpinBox *m_frames, *m_fps, *m_step;
    QLabel *m_framesLabel, *m_stepLabel, *m_counter;
    QCheckBox *m_pixelated, *m_keepBg, *m_skipHidden;
    QToolButton *m_play;
    QWidget *m_view;
    friend class AnimView;
};
