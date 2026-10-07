#include "general-authoring-provider.h"
#include "general-authoring.h"
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <memory>

namespace GeneralAuthoring {
Provider::Provider(QObject *parent, QNetworkAccessManager *manager)
    : QObject(parent), m_manager(manager ? manager : new QNetworkAccessManager(this)) {}
Provider::~Provider() { cancel(); }
void Provider::cancel()
{
    ++m_epoch;
    if (m_reply) {
        m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); m_reply.clear();
    }
}
void Provider::send(const QUrl &endpoint, const QString &key, const QByteArray &payload,
                    std::function<void(QByteArray, QString)> done)
{
    cancel();
    auto fail = [&done](const QString &message) { done({}, message); };
    if (!validEndpoint(endpoint) || key.size() > 4096 || payload.size() > 128 * 1024
        || (!key.isEmpty() && (payload.contains(key.toUtf8()) || endpoint.toEncoded().contains(key.toUtf8())))) {
        fail(QCoreApplication::translate("GeneralAuthoring", "Invalid endpoint, credential or request payload.")); return;
    }
    for (QChar c : key) if (c.unicode() < 33 || c.unicode() > 126) { fail(QCoreApplication::translate("GeneralAuthoring", "Credentials must contain printable ASCII without spaces.")); return; }
    QNetworkRequest request(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    if (!key.isEmpty()) request.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setTransferTimeout(60000);
    auto *reply = m_manager->post(request, payload);
    m_reply = reply;
    reply->setReadBufferSize(512 * 1024 + 1);
    const auto epoch = m_epoch;
    struct Pending { QByteArray body; bool tooLarge = false, timedOut = false; };
    auto state = std::make_shared<Pending>();
    auto drain = [reply, state] {
        const qint64 remaining = 512 * 1024 - state->body.size();
        if (reply->bytesAvailable() > remaining) { state->tooLarge = true; reply->abort(); return; }
        state->body += reply->read(remaining);
    };
    connect(reply, &QIODevice::readyRead, this, drain);
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [reply, state] { state->timedOut = true; reply->abort(); });
    timer->start(60000);
    connect(reply, &QNetworkReply::finished, this, [this, reply, state, drain, epoch, key, done = std::move(done)] {
        if (epoch != m_epoch || m_reply != reply) { reply->deleteLater(); return; }
        // Claim completion before draining: abort() may emit finished reentrantly.
        m_reply.clear();
        if (!state->tooLarge) drain();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString error;
        if (state->tooLarge) error = QCoreApplication::translate("GeneralAuthoring", "Provider response exceeds the size limit.");
        else if (state->timedOut) error = QCoreApplication::translate("GeneralAuthoring", "Provider request timed out.");
        else if (status >= 300 && status < 400) error = QCoreApplication::translate("GeneralAuthoring", "Provider redirects are refused. Enter the final HTTPS endpoint.");
        else if (reply->error() != QNetworkReply::NoError || status != 200)
            error = QCoreApplication::translate("GeneralAuthoring", "Provider request failed (HTTP %1). Check endpoint and credentials locally.").arg(status);
        else if (!key.isEmpty() && state->body.contains(key.toUtf8())) error = QCoreApplication::translate("GeneralAuthoring", "Provider response contained a credential and was discarded.");
        m_reply.clear(); reply->deleteLater();
        done(error.isEmpty() ? state->body : QByteArray(), error);
    });
}
}
