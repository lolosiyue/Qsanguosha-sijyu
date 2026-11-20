#ifndef REPLAY_INSPECTOR_H
#define REPLAY_INSPECTOR_H

#include <QStringList>

// Product QA entry point. Requires the ordinary QApplication/Engine bootstrap;
// reads existing recordings through MainWindow's native playback lifecycle.
class ReplayInspector
{
public:
    static int run(QStringList arguments);
};

#endif
