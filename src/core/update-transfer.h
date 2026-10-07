#ifndef QSAN_UPDATE_TRANSFER_H
#define QSAN_UPDATE_TRANSFER_H
#include "update-catalog.h"
#include <QObject>
#include <QFile>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <atomic>
#include <memory>

class QLockFile;
class QNetworkReply;
class QThread;

// One bounded, HTTPS-only operation at a time. No credentials or installer execution.
class UpdateTransfer : public QObject {
    Q_OBJECT
public:
    explicit UpdateTransfer(QObject *parent = nullptr, QNetworkAccessManager *network = nullptr);
    ~UpdateTransfer() override;
    void catalog(const QUrl &url);
    void download(const QSanUpdates::Entry &entry, const QString &userDataRoot);
    void cancel();
    bool busy() const;
signals:
    void catalogReady(const QByteArray &bytes);
    void downloadReady(const QString &path);
    void failed(const QString &message);
    void progress(qint64 bytes, qint64 total);
private:
    void request(const QUrl &url);
    bool headers();
    void consume();
    void finish();
    void fail(const QString &message, bool discardPartial = false);
    void verify(const QString &path, bool cached);
    void stop();
    QNetworkAccessManager m_network;
    QNetworkAccessManager *m_manager;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timeout;
    QFile m_file;
    QByteArray m_bytes;
    QSanUpdates::Entry m_entry;
    QString m_partial, m_complete;
    std::unique_ptr<QLockFile> m_lock;
    QThread *m_worker = nullptr;
    std::atomic_bool m_cancel{false};
    qint64 m_offset = 0;
    int m_redirects = 0;
    bool m_download = false, m_headers = false;
};
#endif
