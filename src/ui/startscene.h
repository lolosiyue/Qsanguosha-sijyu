#ifndef _START_SCENE_H
#define _START_SCENE_H

#include <QGraphicsScene>
#include <QKeyEvent>
#include <QPixmap>

class HomeButton;
class HomePlate;
class QGraphicsPixmapItem;
class QGraphicsSimpleTextItem;
class QTextEdit;
class Server;
#ifdef QSAN_XP_LEGACY
class LocalServerController;
#endif

class StartScene : public QGraphicsScene
{
    Q_OBJECT

public:
    StartScene();
    ~StartScene();
    void addButton(QAction *action);
    void setServerLogBackground();
    void switchToServer(Server *server);
#ifdef QSAN_XP_LEGACY
    void switchToServer(LocalServerController *controller);
#endif

protected:
    virtual void keyPressEvent(QKeyEvent *event);
    void drawBackground(QPainter *painter, const QRectF &rect) override;

private:
    void relayout();
    void rebuildBackdrop();
    void printServerInfo();
    void selectButton(int index);

    QGraphicsPixmapItem *logo;
    QGraphicsPixmapItem *portrait;
    HomePlate *log_plate;
    HomePlate *dock;
    QGraphicsSimpleTextItem *website_text;
    QPixmap logo_source;
    QPixmap portrait_source;
    QPixmap backdrop_source;
    QPixmap backdrop;
    QTextEdit *server_log;
    Server *m_server;
    QList<HomeButton *> buttons;
    int m_currentIndex;
    bool m_relayouting;
};

#endif
