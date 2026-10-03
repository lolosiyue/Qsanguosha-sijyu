#ifndef _CONFIG_DIALOG_H
#define _CONFIG_DIALOG_H


namespace Ui {
    class ConfigDialog;
}

class SettingsSession;

// Legacy widget used by the in-game menu and builds without QML. SettingsSession shares preview, save and restore behavior with the home page.
// SettingsSession shares logic with the home-page SettingsScene.qml; this class provides the widget.
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
    // Persist widget changes to the session, but ignore updates while loading its values.
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
