#ifndef _CONFIG_DIALOG_H
#define _CONFIG_DIALOG_H


namespace Ui {
    class ConfigDialog;
}

class SettingsSession;

// 舊版設定 dialog:遊戲內選單與無 QML 的建置仍使用。讀寫、預覽、復原都交給
// SettingsSession,與首頁 SettingsScene.qml 共用同一份邏輯;這裡只負責 widget。
class ConfigDialog : public QDialog
{
    Q_OBJECT
public:
    ConfigDialog(SettingsSession *session, QWidget *parent = 0);
    ~ConfigDialog();

private:
    Ui::ConfigDialog *ui;
    SettingsSession *m_session;
    QCheckBox *m_responsiveLayout = nullptr;
    QComboBox *m_oneHandedness = nullptr;
    QLineEdit *m_portraitBackground = nullptr;
    void showFont(QLineEdit *lineedit, const QFont &font);
    void showTextEditColor(const QColor &color);

    void loadConfig();
    // widget 變動時寫回 session;載入中不回寫,避免把畫面同步當成使用者操作。
    void bindValue(const QString &key, const QVariant &value);
    bool m_loading = false;

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void on_setTextEditColorButton_clicked();
    void on_setTextEditFontButton_clicked();
    void on_changeAppFontButton_clicked();
    void on_resetBgMusicButton_clicked();
    void on_browseBgMusicButton_clicked();
    void on_resetBgButton_clicked();
    void on_browseBgButton_clicked();
};

#endif
