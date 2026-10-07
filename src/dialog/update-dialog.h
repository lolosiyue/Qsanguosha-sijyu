#ifndef QSAN_UPDATE_DIALOG_H
#define QSAN_UPDATE_DIALOG_H
#include "update-catalog.h"
#include <QDialog>
#include <QMap>
#include <atomic>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QNetworkAccessManager;
class QProgressBar;
class QPushButton;
class QSettings;
class QTextEdit;
class QThread;
class UpdateTransfer;

class UpdateDialog : public QDialog {
    Q_OBJECT
public:
    UpdateDialog(const QString &runtimeRoot, const QString &userDataRoot,
                 const QString &version, const QString &commit, bool cleanBuild,
                 QSettings *settings, QWidget *parent = nullptr,
                 QNetworkAccessManager *network = nullptr);
    ~UpdateDialog() override;
    void reject() override;
private:
    enum Operation { Idle, Releases, Materials, Compare, Download, Stage };
    void check();
    void checkMaterials();
    void received(const QByteArray &bytes);
    void failed(const QString &error);
    void refresh();
    void selected();
    void download();
    void downloaded(const QString &path);
    void cancel();
    void setOperation(Operation operation);
    const QSanUpdates::Entry *entry() const;
    QString m_runtimeRoot, m_userDataRoot, m_version, m_commit, m_messages;
    bool m_cleanBuild = false, m_closeAfterCancel = false;
    QSettings *m_settings;
    Operation m_operation = Idle;
    QList<QSanUpdates::Entry> m_entries;
    QMap<QString, bool> m_newerCommits;
    QSanUpdates::Entry m_chosen;
    UpdateTransfer *m_transfer;
    QThread *m_worker = nullptr;
    std::atomic_bool m_cancel{false};
    QLineEdit *m_url;
    QCheckBox *m_prerelease;
    QLabel *m_status;
    QListWidget *m_list;
    QTextEdit *m_details;
    QProgressBar *m_progress;
    QPushButton *m_check, *m_download, *m_page, *m_cancelButton, *m_close;
};
#endif
