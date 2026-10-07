#ifndef GENERAL_AUTHORING_PROVIDER_H
#define GENERAL_AUTHORING_PROVIDER_H
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <functional>
class QNetworkAccessManager;
class QNetworkReply;
namespace GeneralAuthoring {
// One OpenAI-compatible chat-completions transport. No provider-specific state.
class Provider : public QObject {
public:
    explicit Provider(QObject *parent = nullptr, QNetworkAccessManager *manager = nullptr);
    ~Provider() override;
    void send(const QUrl &endpoint, const QString &key, const QByteArray &payload,
              std::function<void(QByteArray, QString)> done);
    void cancel();
private:
    QNetworkAccessManager *m_manager;
    QPointer<QNetworkReply> m_reply;
    quint64 m_epoch = 0;
};
}
#endif
