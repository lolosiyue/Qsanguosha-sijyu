#ifndef _BOOT_VIDEO_ITEM_H
#define _BOOT_VIDEO_ITEM_H

#include <QQuickItem>
#include <QUrl>
#include <QVideoSink>
#include <memory>

class QAudioOutput;
class QMediaPlayer;
struct BootVideoFrames;

// Video for the boot splash. Qt's VideoOutput only shows a new frame when the GUI
// thread syncs, and the engine blocks that thread while the splash is up. Here the
// decoder thread copies each frame's YUV planes and the render thread uploads them
// and converts them in a shader, so the picture keeps moving as long as the window
// renders (the splash always runs an Animator).
class BootVideoItem : public QQuickItem
{
    Q_OBJECT

public:
    explicit BootVideoItem(QQuickItem *parent = nullptr);
    ~BootVideoItem() override;

    void play(const QUrl &source, qreal volume);
    // True once the decoder delivered a frame; safe to ask while the GUI thread lags.
    bool hasFrame() const;
    void fadeOutAudio(int durationMs);

signals:
    void firstFrame();
    void failed();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

private:
    void fail();

    std::shared_ptr<BootVideoFrames> m_frames;
    QVideoSink m_sink;
    QMediaPlayer *m_player;
    QAudioOutput *m_audio;
    bool m_failed = false;
};

#endif
