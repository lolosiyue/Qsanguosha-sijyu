#ifndef _BOOT_SPLASH_H
#define _BOOT_SPLASH_H

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QStringList>

class BootVideoItem;
class MainWindow;
class QQuickView;

// Boot animation window, a card centred where the main window opens. It plays a random
// clip from video/boot (or a drawn animation when there is none) while the engine loads
// and HomeScene warms its catalog pages in the still invisible main window. Once those
// are ready it cross-dissolves into the main window.
class BootSplash final : public QObject
{
    Q_OBJECT

public:
    // False for launches that go straight into a game or a scripted run.
    static bool wanted(const QStringList &arguments);
    // Shows the splash and waits for its first frame; nullptr if the QML fails to load.
    static BootSplash *show();
    // True from show() until the splash starts fading out.
    static bool isCovering();
    // Engine load pulse: moves the progress bar and keeps the window responsive.
    static void pulse();

    // Call after constructing the main window and before showing it.
    void cover(MainWindow *mainWindow);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    BootSplash(QQuickView *view, const QStringList &clips);
    ~BootSplash() override;

    // Plays the next clip of the pool; false when none is left.
    bool playNextClip();
    void clipFailed();
    void setProgress(qreal progress);
    void leave();
    // Fades out now, or once the drawn intro has played out.
    void scheduleOutro();
    void playOutro(int durationMs);
    // HomeScene plays its entrance once the splash stops covering it.
    void releaseHome();

    QQuickView *m_view;
    QPointer<BootVideoItem> m_video;
    QStringList m_clips;
    int m_attempt = 0;
    QPointer<MainWindow> m_mainWindow;
    QElapsedTimer m_shown;
    qint64 m_loadMs;
    qreal m_progress = 0;
    bool m_leaving = false;
    bool m_outroStarted = false;
};

#endif
