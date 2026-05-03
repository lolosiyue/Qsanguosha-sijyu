#ifndef _START_SCENE_H
#define _START_SCENE_H

//#include "button.h"
//#include "qsan-selectable-item.h"
//#include "server.h"

#include <QKeyEvent>

class Button;
class QSanSelectableItem;
class Server;

class StartScene : public QGraphicsScene
{
    Q_OBJECT

public:
    StartScene();
    ~StartScene();
    void addButton(QAction *action);
    void setServerLogBackground();
    void switchToServer(Server *server);

protected:
    virtual void keyPressEvent(QKeyEvent *event);

private:
    void printServerInfo();
    void selectButton(int index);
    void navigateUp();
    void navigateDown();
    void navigateLeft();
    void navigateRight();

    QSanSelectableItem *logo;
    QTextEdit *server_log;
    QList<Button *> buttons;
    int m_currentIndex;
};

#endif