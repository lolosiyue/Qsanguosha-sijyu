#ifndef _MAIN_WINDOW_H
#define _MAIN_WINDOW_H

#include "src/pch.h"

namespace Ui {
    class MainWindow;
}

class FitView;
class RoomScene;
class QGraphicsScene;
class QSystemTrayIcon;
class Server;
class QTextEdit;
class ConnectionDialog;
class ConfigDialog;
class QStackedWidget;
class QQuickWidget;
class HomeController;
class PointerEffectOverlay;

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
    QGraphicsScene *getScene();
    void refitScene();
    void setUiScale(qreal scale);

    // Linux GUI M1 startup smoke 用的觀測點。HomeScene 的載入結果本身就係
    // MainWindow 的狀態，喺度公開出嚟，測試就唔使另外複製一份啟動流程。
    bool isHomeSceneReady() const;
    bool hasHomeSceneError() const;
    QString homeSceneError() const;
    QQuickWidget *homeSceneView() const;
    // M2B-A multimedia smoke 的觀測點：影片背景的結果狀態由 HomeController 持有，
    // 呢度只係將已有的 object 公開出嚟，唔會另外複製一次首頁載入流程。
    HomeController *homeSceneController() const;

signals:
    void homeSceneReady();
    void homeSceneFailed(const QString &error);

    // Linux GUI M2 network smoke 用的觀測點。RoomScene 唔喺 startup path,而係喺
    // 收到 server setup 之後由 enterRoom() 建立,所以要另外一個 seam;同 M1 一樣,
    // 呢度只係報告 MainWindow 本身已有的狀態,測試唔使複製一次進房流程。
    void roomSceneCreated(RoomScene *scene);

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
    PointerEffectOverlay *m_pointerOverlay = nullptr;
    bool m_homeSceneReady = false;
    QString m_homeSceneError;

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
    void startLocalConsoleGame();
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
