#ifndef _BOOT_SPLASH_H
#define _BOOT_SPLASH_H

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QStringList>

class MainWindow;
class QQuickView;

// Boot animation window, a card centred where the main window opens. It shows while the
// engine loads and while HomeScene warms its catalog pages in the still invisible main
// window, then fades out as the main window fades in.
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

    // Call after constructing the main window and before showing it.
    void cover(MainWindow *mainWindow);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit BootSplash(QQuickView *view);
    ~BootSplash() override;

    void leave();
    void playOutro();
    // HomeScene plays its entrance once the splash stops covering it.
    void releaseHome();

    QQuickView *m_view;
    QPointer<MainWindow> m_mainWindow;
    QElapsedTimer m_shown;
    bool m_leaving = false;
    bool m_outroStarted = false;
};

#endif
