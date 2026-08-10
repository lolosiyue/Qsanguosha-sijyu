#ifndef _MAIN_WINDOW_H
#define _MAIN_WINDOW_H

#include "src/pch.h"

namespace Ui {
    class MainWindow;
}

class FitView;
class QGraphicsScene;
class QSystemTrayIcon;
class Server;
class QTextEdit;
class ConnectionDialog;
class ConfigDialog;
class QStackedWidget;
class QQuickWidget;
class HomeController;

class BroadcastBox : public QDialog
{
    Q_OBJECT

public:
    BroadcastBox(Server *server, QWidget *parent = 0);

protected:
    virtual void accept();

private:
    Server *server;
    QTextEdit *text_edit;
};

class BackLoader
{
public:
    static void preload();
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();
    void setBackgroundBrush(bool center_as_origin);
    QGraphicsScene* getScene();
    void refitScene();

protected:
    virtual void closeEvent(QCloseEvent *);
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;

private:
    enum class MainPage {
        Home,
        Game
    };

private slots:
    void setupHomePage();
    void showHomePage();
    void reloadHomePage();
    void showGamePage(QGraphicsScene *scene);
    void restoreFromConfig();

private:
    QStackedWidget *pageStack = nullptr;
    QQuickWidget *homeView = nullptr;
    FitView *gameView = nullptr;
    QGraphicsScene *scene = nullptr;
    Ui::MainWindow *ui = nullptr;
    ConnectionDialog *connection_dialog = nullptr;
    ConfigDialog *config_dialog = nullptr;
    QSystemTrayIcon *systray = nullptr;
    Server *server = nullptr;
    HomeController *homeController = nullptr;

public slots:
    void startConnection();

private slots:
    void on_actionAbout_GPLv3_triggered();
    void on_actionAbout_Lua_triggered();
    void on_actionAbout_fmod_triggered();
    void on_actionReplay_file_convert_triggered();
    void on_actionRecord_analysis_triggered();
    void on_actionAcknowledgement_triggered();
    void on_actionBroadcast_triggered();
    void on_actionScenario_Overview_triggered();
    void on_actionRole_assign_table_triggered();
    void on_actionMinimize_to_system_tray_triggered();
    void on_actionShow_Hide_Menu_triggered();
    void on_actionFullscreen_triggered();
    void on_actionReplay_triggered();
    void on_actionAbout_triggered();
    void on_actionEnable_Hotkey_toggled(bool);
    void on_actionNever_nullify_my_trick_toggled(bool);
    void on_actionCard_Overview_triggered();
    void on_actionGeneral_Overview_triggered();
    void on_actionStart_Server_triggered();
    void on_actionExit_triggered();
    void on_actionCard_editor_triggered();

    void checkVersion(const QString &server_version, const QString &server_mod, int card_num);
    void networkError(const QString &error_msg);
    void enterRoom();
    void gotoScene(QGraphicsScene *scene);
    void gotoStartScene();
    void enableDialogButtons();
    void startGameInAnotherInstance();
    void changeBackground();
    void on_actionView_ban_list_triggered();

    void on_actionManage_Ban_IP_triggered();
};

#endif
