#ifndef _CONFIG_DIALOG_H
#define _CONFIG_DIALOG_H


namespace Ui {
    class ConfigDialog;
}

class ConfigDialog : public QDialog
{
    Q_OBJECT
public:
    ConfigDialog(QWidget *parent = 0);
    ~ConfigDialog();

private:
    Ui::ConfigDialog *ui;
    void showFont(QLineEdit *lineedit, const QFont &font);

    // 「顯示」分頁視角元素設定的快照:開起 dialog 時記住已套用的值,
    // 取消時復原,按確定時才持久化。
    struct VisualSnapshot {
        int colorScheme = 0;
        qreal uiScale = 1.0;
        QString backgroundImage;
        QString visualMode;
        bool noIndicator = false;
        bool noEquipAnim = false;
        bool noCardMoveAnim = false;
        bool enableAnimatedGenerals = true;
        bool enablePointerEffect = true;
    } m_visual;

    void loadConfig();
    void snapshotVisualSettings();
    void refitRoomScene();
    void applyUiScalePreview();
    void previewTheme(int scheme);
    void previewVisualMode();
    void restoreVisualSettings();
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
    void saveConfig();

signals:
    void bg_changed();
    void previewChanged();
};

#endif