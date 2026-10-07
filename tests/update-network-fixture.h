#ifndef QSAN_UPDATE_NETWORK_FIXTURE_H
#define QSAN_UPDATE_NETWORK_FIXTURE_H

#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QVector>

#include <algorithm>
#include <cstring>

namespace QSanUpdateTest {

struct FakeResponse {
    int status = 200;
    QByteArray body;
    QMap<QByteArray, QByteArray> headers;
    QUrl redirect;
    bool autoDeliver = true;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
};

class FakeReply final : public QNetworkReply {
public:
    FakeReply(QNetworkAccessManager::Operation operation, const QNetworkRequest &request,
              const FakeResponse &response, QObject *parent = nullptr)
        : QNetworkReply(parent), m_body(response.body), m_autoDeliver(response.autoDeliver)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(operation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
        if (!response.redirect.isEmpty())
            setAttribute(QNetworkRequest::RedirectionTargetAttribute, response.redirect);
        if (response.error != QNetworkReply::NoError)
            setError(response.error, QStringLiteral("mock network failure"));
        for (auto it = response.headers.cbegin(); it != response.headers.cend(); ++it)
            setRawHeader(it.key(), it.value());
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }

    bool autoDeliver() const { return m_autoDeliver; }

    void deliver()
    {
        if (m_delivered || m_aborted) return;
        m_delivered = true;
        if (!m_body.isEmpty()) emit readyRead();
        setFinished(true);
        emit finished();
    }

    void abort() override { m_aborted = true; }

    qint64 bytesAvailable() const override
    {
        return qint64(m_body.size()) - m_offset + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        if (maxSize <= 0 || m_offset >= m_body.size()) return -1;
        const qint64 count = std::min(maxSize, qint64(m_body.size()) - m_offset);
        std::memcpy(data, m_body.constData() + m_offset, size_t(count));
        m_offset += count;
        return count;
    }

    qint64 writeData(const char *, qint64) override { return -1; }

private:
    QByteArray m_body;
    qint64 m_offset = 0;
    bool m_autoDeliver = true;
    bool m_delivered = false;
    bool m_aborted = false;
};

class FakeManager final : public QNetworkAccessManager {
public:
    explicit FakeManager(QObject *parent = nullptr) : QNetworkAccessManager(parent) {}

    void enqueue(FakeResponse response) { m_responses.append(std::move(response)); }
    const QList<QNetworkRequest> &requests() const { return m_requests; }

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData = nullptr) override
    {
        Q_UNUSED(outgoingData);
        m_requests.append(request);
        const FakeResponse response = m_responses.isEmpty()
            ? FakeResponse{500, {}, {}, {}, true} : m_responses.takeFirst();
        auto *reply = new FakeReply(operation, request, response, this);
        if (reply->autoDeliver())
            QTimer::singleShot(0, reply, [reply] { reply->deliver(); });
        return reply;
    }

private:
    QList<QNetworkRequest> m_requests;
    QList<FakeResponse> m_responses;
};

} // namespace QSanUpdateTest

#endif
