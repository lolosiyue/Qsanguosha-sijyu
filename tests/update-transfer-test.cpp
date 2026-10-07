#include "update-network-fixture.h"
#include "update-transfer.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <QDebug>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace {
int failures = 0;

void check(bool condition, const char *expression, int line)
{
    if (condition) return;
    qCritical().noquote() << QStringLiteral("line %1: %2").arg(line).arg(QString::fromLatin1(expression));
    ++failures;
}

#define CHECK(expression) check((expression), #expression, __LINE__)

using QSanUpdateTest::FakeManager;
using QSanUpdateTest::FakeResponse;
using QSanUpdates::Entry;

struct Outcome {
    QEventLoop loop;
    bool ready = false;
    bool failed = false;
    QByteArray catalogBytes;
    QString path;
    QString error;
};

void observe(UpdateTransfer &transfer, Outcome &outcome)
{
    QObject::connect(&transfer, &UpdateTransfer::catalogReady, &outcome.loop,
                     [&outcome](const QByteArray &bytes) {
        outcome.ready = true;
        outcome.catalogBytes = bytes;
        outcome.loop.quit();
    });
    QObject::connect(&transfer, &UpdateTransfer::downloadReady, &outcome.loop,
                     [&outcome](const QString &path) {
        outcome.ready = true;
        outcome.path = path;
        outcome.loop.quit();
    });
    QObject::connect(&transfer, &UpdateTransfer::failed, &outcome.loop,
                     [&outcome](const QString &message) {
        outcome.failed = true;
        outcome.error = message;
        outcome.loop.quit();
    });
}

bool waitFor(Outcome &outcome, int timeoutMs = 5000)
{
    if (!outcome.ready && !outcome.failed) {
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &outcome.loop, &QEventLoop::quit);
        timer.start(timeoutMs);
        outcome.loop.exec();
    }
    return outcome.ready || outcome.failed;
}

QByteArray sha256(const QByteArray &payload)
{
    return QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
}

Entry downloadEntry(const QByteArray &payload, const QString &name = QStringLiteral("QSanguosha-linux-x86_64.tar.zst"))
{
    Entry entry;
    entry.version = QStringLiteral("1.1.0");
    entry.name = name;
    entry.url = QUrl(QStringLiteral("https://github.com/lolosiyue/Qsanguosha-sijyu/releases/download/v1.1.0/") + name);
    entry.sha256 = QString::fromLatin1(sha256(payload));
    entry.size = payload.size();
    entry.downloadable = true;
    return entry;
}

QString partialPath(const QString &root, const Entry &entry)
{
    const QString name = entry.material ? QStringLiteral("package.zip") : entry.name;
    return QDir(root).filePath(QStringLiteral("update-downloads/") + entry.sha256 + QLatin1Char('/') + name + QStringLiteral(".part"));
}

QString completePath(const QString &root, const Entry &entry)
{
    const QString name = entry.material ? QStringLiteral("package.zip") : entry.name;
    return QDir(root).filePath(QStringLiteral("update-downloads/") + entry.sha256 + QLatin1Char('/') + name);
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

FakeResponse response(int status, const QByteArray &body = {}, bool includeLength = true)
{
    FakeResponse value;
    value.status = status;
    value.body = body;
    if (includeLength)
        value.headers.insert(QByteArrayLiteral("Content-Length"), QByteArray::number(body.size()));
    return value;
}

void testCatalogFetchAndBounds()
{
    const QUrl url(QStringLiteral("https://api.github.com/repos/lolosiyue/Qsanguosha-sijyu/releases"));
    {
        FakeManager manager;
        const QByteArray body = QByteArrayLiteral("[]");
        manager.enqueue(response(200, body));
        UpdateTransfer transfer(nullptr, &manager);
        Outcome outcome;
        observe(transfer, outcome);
        transfer.catalog(url);
        CHECK(waitFor(outcome));
        CHECK(outcome.ready && outcome.catalogBytes == body);
        CHECK(manager.requests().size() == 1);
        if (manager.requests().size() == 1) {
            CHECK(manager.requests().first().url() == url);
            CHECK(manager.requests().first().rawHeader("Accept-Encoding") == QByteArrayLiteral("identity"));
        }
    }
    {
        FakeManager manager;
        manager.enqueue(response(200, QByteArray(QSanUpdates::CatalogLimit + 1, 'x'), false));
        UpdateTransfer transfer(nullptr, &manager);
        Outcome outcome;
        observe(transfer, outcome);
        transfer.catalog(url);
        CHECK(waitFor(outcome));
        CHECK(outcome.failed);
        CHECK(manager.requests().size() == 1);
    }
    {
        FakeManager manager;
        auto tooLarge = response(200);
        tooLarge.headers.insert(QByteArrayLiteral("Content-Length"),
                                QByteArray::number(QSanUpdates::CatalogLimit + 1));
        manager.enqueue(tooLarge);
        UpdateTransfer transfer(nullptr, &manager);
        Outcome outcome;
        observe(transfer, outcome);
        transfer.catalog(url);
        CHECK(waitFor(outcome));
        CHECK(outcome.failed);
    }
}

void testRateLimitAndCancellation()
{
    const QUrl url(QStringLiteral("https://api.github.com/repos/lolosiyue/Qsanguosha-sijyu/releases"));
    for (const int status : {403, 429}) {
        FakeManager manager;
        manager.enqueue(response(status));
        UpdateTransfer transfer(nullptr, &manager);
        Outcome outcome;
        observe(transfer, outcome);
        transfer.catalog(url);
        CHECK(waitFor(outcome));
        CHECK(outcome.failed);
    }
    {
        FakeManager manager;
        auto hanging = response(200);
        hanging.autoDeliver = false;
        manager.enqueue(hanging);
        UpdateTransfer transfer(nullptr, &manager);
        Outcome outcome;
        observe(transfer, outcome);
        transfer.catalog(url);
        CHECK(manager.requests().size() == 1);
        transfer.cancel();
        CHECK(outcome.failed && !outcome.ready);
        CHECK(!transfer.busy());
    }
}

void testHttpsRedirectPolicy()
{
    const QUrl url(QStringLiteral("https://updates.example.test/catalog.json"));
    {
        FakeManager manager;
        FakeResponse redirect;
        redirect.status = 302;
        redirect.redirect = QUrl(QStringLiteral("https://cdn.example.test/catalog.json"));
        manager.enqueue(redirect);
        const QByteArray body = QByteArrayLiteral("payload");
        manager.enqueue(response(200, body));
        UpdateTransfer transfer(nullptr, &manager);
        Outcome outcome;
        observe(transfer, outcome);
        transfer.catalog(url);
        CHECK(waitFor(outcome));
        CHECK(outcome.ready && outcome.catalogBytes == body);
        CHECK(manager.requests().size() == 2);
        if (manager.requests().size() == 2)
            CHECK(manager.requests().at(1).url() == redirect.redirect);
    }
    {
        FakeManager manager;
        FakeResponse redirect;
        redirect.status = 302;
        redirect.redirect = QUrl(QStringLiteral("http://cdn.example.test/catalog.json"));
        manager.enqueue(redirect);
        UpdateTransfer transfer(nullptr, &manager);
        Outcome outcome;
        observe(transfer, outcome);
        transfer.catalog(url);
        CHECK(waitFor(outcome));
        CHECK(outcome.failed && manager.requests().size() == 1);
    }
}

void testRange206ValidationAndResume()
{
    const QByteArray full = QByteArrayLiteral("verified package payload");
    const Entry entry = downloadEntry(full);
    QTemporaryDir temp;
    CHECK(temp.isValid());
    CHECK(writeFile(partialPath(temp.path(), entry), full.left(8)));

    FakeManager manager;
    FakeResponse partial = response(206, full.mid(8));
    partial.headers.insert(QByteArrayLiteral("Content-Range"),
                           QByteArrayLiteral("bytes 8-23/24"));
    manager.enqueue(partial);
    UpdateTransfer transfer(nullptr, &manager);
    Outcome outcome;
    observe(transfer, outcome);
    transfer.download(entry, temp.path());
    CHECK(waitFor(outcome));
    CHECK(outcome.ready && readFile(outcome.path) == full);
    CHECK(manager.requests().size() == 1);
    if (manager.requests().size() == 1)
        CHECK(manager.requests().first().rawHeader("Range") == QByteArrayLiteral("bytes=8-"));
}

void testRange206MustMatchExactRange()
{
    const QByteArray full = QByteArrayLiteral("abcdefgh");
    const Entry entry = downloadEntry(full);
    QTemporaryDir temp;
    CHECK(writeFile(partialPath(temp.path(), entry), full.left(3)));

    FakeManager manager;
    FakeResponse partial = response(206, full.mid(3));
    partial.headers.insert(QByteArrayLiteral("Content-Range"), QByteArrayLiteral("bytes 2-7/8"));
    manager.enqueue(partial);
    UpdateTransfer transfer(nullptr, &manager);
    Outcome outcome;
    observe(transfer, outcome);
    transfer.download(entry, temp.path());
    CHECK(waitFor(outcome));
    CHECK(outcome.failed);
    CHECK(!QFileInfo::exists(partialPath(temp.path(), entry)));
    CHECK(!QFileInfo::exists(completePath(temp.path(), entry)));
}

void testRange200RestartsFromZero()
{
    const QByteArray full = QByteArrayLiteral("fresh complete payload");
    const Entry entry = downloadEntry(full);
    QTemporaryDir temp;
    CHECK(writeFile(partialPath(temp.path(), entry), QByteArrayLiteral("old partial")));

    FakeManager manager;
    manager.enqueue(response(200, full));
    UpdateTransfer transfer(nullptr, &manager);
    Outcome outcome;
    observe(transfer, outcome);
    transfer.download(entry, temp.path());
    CHECK(waitFor(outcome));
    CHECK(outcome.ready && readFile(outcome.path) == full);
    CHECK(manager.requests().size() == 1);
    if (manager.requests().size() == 1)
        CHECK(manager.requests().first().rawHeader("Range") == QByteArrayLiteral("bytes=11-"));
}

void testDeclaredSizeOverflowAndShaMismatch()
{
    {
        const QByteArray full = QByteArrayLiteral("sixsix");
        Entry entry = downloadEntry(full);
        QTemporaryDir temp;
        FakeManager manager;
        auto tooLarge = response(200, full + 'x');
        manager.enqueue(tooLarge);
        UpdateTransfer transfer(nullptr, &manager);
        Outcome outcome;
        observe(transfer, outcome);
        transfer.download(entry, temp.path());
        CHECK(waitFor(outcome));
        CHECK(outcome.failed);
        CHECK(!QFileInfo::exists(partialPath(temp.path(), entry)));
    }
    {
        const QByteArray wanted = QByteArrayLiteral("expected");
        const QByteArray wrong = QByteArrayLiteral("mismatch");
        const Entry entry = downloadEntry(wanted);
        QTemporaryDir temp;
        FakeManager manager;
        manager.enqueue(response(200, wrong));
        UpdateTransfer transfer(nullptr, &manager);
        Outcome outcome;
        observe(transfer, outcome);
        transfer.download(entry, temp.path());
        CHECK(waitFor(outcome));
        CHECK(outcome.failed);
        CHECK(!QFileInfo::exists(partialPath(temp.path(), entry)));
        CHECK(!QFileInfo::exists(completePath(temp.path(), entry)));
    }
}

void testVerifiedCacheAndNoInstallerExecution()
{
    const QByteArray payload = QByteArrayLiteral("already verified bytes");
    const Entry entry = downloadEntry(payload);
    QTemporaryDir temp;
    CHECK(writeFile(completePath(temp.path(), entry), payload));

    FakeManager manager;
    UpdateTransfer transfer(nullptr, &manager);
    Outcome outcome;
    observe(transfer, outcome);
    transfer.download(entry, temp.path());
    CHECK(waitFor(outcome));
    CHECK(outcome.ready);
    CHECK(outcome.path == completePath(temp.path(), entry));
    CHECK(manager.requests().isEmpty());
    CHECK(readFile(outcome.path) == payload);
}

void testRejectedDescriptorDoesNotRequestNetwork()
{
    FakeManager manager;
    UpdateTransfer transfer(nullptr, &manager);
    Outcome outcome;
    observe(transfer, outcome);
    Entry bad;
    bad.downloadable = true;
    bad.url = QUrl(QStringLiteral("http://example.test/file"));
    bad.size = 1;
    bad.sha256 = QString(64, QLatin1Char('a'));
    QTemporaryDir temp;
    transfer.download(bad, temp.path());
    CHECK(outcome.failed);
    CHECK(manager.requests().isEmpty());
}

void testOfflineResumePreservesPartialDownload()
{
    const QByteArray full = QByteArrayLiteral("expected payload after reconnect");
    const QByteArray partial = full.left(9);
    const Entry entry = downloadEntry(full);
    QTemporaryDir temp;
    CHECK(writeFile(partialPath(temp.path(), entry), partial));

    FakeManager manager;
    FakeResponse offline;
    offline.status = 0;
    offline.error = QNetworkReply::HostNotFoundError;
    manager.enqueue(offline);
    UpdateTransfer transfer(nullptr, &manager);
    Outcome outcome;
    observe(transfer, outcome);
    transfer.download(entry, temp.path());
    CHECK(waitFor(outcome));
    CHECK(outcome.failed && !outcome.ready);
    CHECK(manager.requests().size() == 1);
    if (manager.requests().size() == 1)
        CHECK(manager.requests().first().rawHeader("Range") == QByteArrayLiteral("bytes=9-"));
    CHECK(QFileInfo::exists(partialPath(temp.path(), entry)));
    CHECK(readFile(partialPath(temp.path(), entry)) == partial);
    CHECK(!QFileInfo::exists(completePath(temp.path(), entry)));
}
void testLinkedPartialCannotOverwriteActiveFile()
{
#ifdef Q_OS_UNIX
    for (bool hardlink : {false, true}) {
        QTemporaryDir temp;
        const QByteArray payload = QByteArrayLiteral("active bytes must survive");
        const Entry entry = downloadEntry(payload);
        const QString active = temp.filePath(QStringLiteral("active-asset"));
        const QString partial = partialPath(temp.path(), entry);
        CHECK(writeFile(active, payload));
        CHECK(QDir().mkpath(QFileInfo(partial).absolutePath()));
        const bool linked = hardlink
            ? ::link(QFile::encodeName(active).constData(), QFile::encodeName(partial).constData()) == 0
            : QFile::link(active, partial);
        CHECK(linked);
        FakeManager manager;
        UpdateTransfer transfer(nullptr, &manager);
        Outcome outcome;
        observe(transfer, outcome);
        transfer.download(entry, temp.path());
        CHECK(outcome.failed);
        CHECK(manager.requests().isEmpty());
        CHECK(readFile(active) == payload);
    }
#endif
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testCatalogFetchAndBounds();
    testRateLimitAndCancellation();
    testHttpsRedirectPolicy();
    testRange206ValidationAndResume();
    testRange206MustMatchExactRange();
    testRange200RestartsFromZero();
    testDeclaredSizeOverflowAndShaMismatch();
    testVerifiedCacheAndNoInstallerExecution();
    testRejectedDescriptorDoesNotRequestNetwork();
    testOfflineResumePreservesPartialDownload();
    testLinkedPartialCannotOverwriteActiveFile();
    if (failures) {
        qCritical() << failures << "update transfer checks failed";
        return 1;
    }
    qInfo() << "All update transfer checks passed";
    return 0;
}
