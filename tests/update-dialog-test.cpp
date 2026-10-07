#include "update-dialog.h"
#include "update-network-fixture.h"
#include "package-store.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTranslator>
#include <QtTest/QTest>

using namespace QSanUpdateTest;
#define CHECK(x) do { if (!(x)) { qCritical() << __LINE__ << #x; return 1; } } while (false)
static bool until(const std::function<bool()> &done) {
    QElapsedTimer elapsed; elapsed.start();
    while (!done() && elapsed.elapsed() < 5000) QTest::qWait(10);
    return done();
}
static QJsonObject material(const QByteArray &zip) {
    return {{"id", "material-test"}, {"version", "2.0.0"}, {"game_version", "20251231"},
            {"name", "Mock materials"}, {"notes", "<b>Plain notes</b>"},
            {"url", "https://cdn.example.test/material.zip"}, {"size", double(zip.size())},
            {"sha256", QString::fromLatin1(QCryptographicHash::hash(zip, QCryptographicHash::Sha256).toHex())}};
}
static int ancestryCase(const QString &commit, bool cleanBuild, bool expectAhead) {
    QTemporaryDir temp;
    CHECK(temp.isValid());
    QSettings settings(temp.filePath("ancestry.ini"), QSettings::IniFormat);
    settings.setValue("Updates/MaterialManifestUrl", QString());

    const QByteArray downloadBytes = "mock release bytes";
    const QString assetName = QSanUpdates::platform().startsWith("windows")
        ? QStringLiteral("QSanguosha-windows-x64.zip")
        : QStringLiteral("QSanguosha-linux-x86_64.tar.zst");
    const QJsonObject asset{{"name", assetName},
        {"digest", "sha256:" + QString::fromLatin1(QCryptographicHash::hash(downloadBytes, QCryptographicHash::Sha256).toHex())},
        {"size", double(downloadBytes.size())},
        {"browser_download_url", "https://github.com/lolosiyue/Qsanguosha-sijyu/releases/download/v1.1.0/" + assetName}};
    const QJsonObject release{{"tag_name", "v1.1.0"}, {"name", "v1.1.0"}, {"body", "ancestry fixture"},
        {"html_url", "https://github.com/lolosiyue/Qsanguosha-sijyu/releases/tag/v1.1.0"},
        {"draft", false}, {"prerelease", false}, {"assets", QJsonArray{asset}}};

    FakeManager manager;
    FakeResponse releases;
    releases.body = QJsonDocument(QJsonArray{release}).toJson(QJsonDocument::Compact);
    manager.enqueue(releases);
    const QString runtime = temp.filePath("runtime"), user = temp.filePath("user");
    CHECK(QDir().mkpath(runtime));
    UpdateDialog dialog(runtime, user, "1.0.0", commit, cleanBuild, &settings, nullptr, &manager);
    dialog.show();
    auto *list = dialog.findChild<QListWidget *>();
    auto *download = dialog.findChild<QPushButton *>("downloadSelectedUpdate");
    auto *status = dialog.findChild<QLabel *>("updateStatus");
    CHECK(list && download && status);
    CHECK(until([&] { return manager.requests().size() == 1 && list->count() == 1; }));

    if (!cleanBuild) {
        CHECK(!download->isEnabled());
        download->click();
        CHECK(manager.requests().size() == 1); // A dirty build must never request ancestry or a binary.
        dialog.reject();
        return 0;
    }

    QJsonObject compare{{"status", expectAhead ? "ahead" : "behind"},
                        {"ahead_by", expectAhead ? 3 : 0}};
    FakeResponse compareResponse;
    compareResponse.body = QJsonDocument(compare).toJson(QJsonDocument::Compact);
    manager.enqueue(compareResponse);
    if (expectAhead) {
        FakeResponse waitingDownload;
        waitingDownload.autoDeliver = false;
        manager.enqueue(waitingDownload);
    }
    CHECK(download->isEnabled());
    download->click();
    if (expectAhead) {
        CHECK(until([&] { return manager.requests().size() == 3; }));
        CHECK(manager.requests().at(1).url().path().contains("/compare/" + commit + "...v1.1.0"));
        CHECK(manager.requests().at(2).url().path().contains("/releases/download/v1.1.0/"));
        CHECK(status->text().contains("Downloading"));
        auto *cancel = dialog.findChild<QPushButton *>("cancelUpdateOperation");
        CHECK(cancel && cancel->isEnabled());
        cancel->click();
        CHECK(until([&] { return !cancel->isEnabled(); }));
        CHECK(manager.requests().size() == 3);
    } else {
        CHECK(until([&] { return status->text().contains("not newer"); }));
        CHECK(manager.requests().size() == 2); // Behind/diverged ancestry never fetches the asset.
        CHECK(!download->isEnabled());
    }
    dialog.reject();
    return 0;
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--translation-only")) {
        QTranslator translator;
        CHECK(translator.load(QString::fromLocal8Bit(argv[2])));
        app.installTranslator(&translator);
        QTemporaryDir temp;
        CHECK(temp.isValid());
        QSettings settings(temp.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        FakeManager manager;
        FakeResponse empty; empty.body = "[]"; manager.enqueue(empty);
        UpdateDialog translated(temp.path(), temp.filePath("user"), "20251231", QString(40, 'a'), true,
                                &settings, nullptr, &manager);
        translated.show();
        CHECK(translated.windowTitle() == QString::fromUtf8("检查更新"));
        CHECK(translated.findChild<QPushButton *>("downloadSelectedUpdate")->text() == QString::fromUtf8("下载所选更新"));
        CHECK(translated.findChild<QPushButton *>("cancelUpdateOperation")->text() == QString::fromUtf8("取消操作"));
        CHECK(until([&] { return translated.findChild<QLabel *>("updateStatus")->text().contains(QString::fromUtf8("尚未配置素材更新")); }));
        CHECK(QSanUpdates::materials("{}", "20251231", {}).error == QString::fromUtf8("素材目录无效或超过大小限制。"));
        translated.reject();
        qInfo() << "Compiled Simplified Chinese dialog/catalog translations passed";
        return 0;
    }
    CHECK(argc == 2);
    QTemporaryDir temp;
    CHECK(temp.isValid());
    QFile fixture(QString::fromLocal8Bit(argv[1]) + QStringLiteral("/v2.zip"));
    CHECK(fixture.open(QIODevice::ReadOnly));
    const QByteArray zip = fixture.readAll();
    QSettings settings(temp.filePath("settings.ini"), QSettings::IniFormat);
    settings.setValue("Updates/MaterialManifestUrl", "https://cdn.example.test/catalog.json");
    FakeManager manager;
    FakeResponse releases; releases.body = "[]";
    FakeResponse materials;
    materials.body = QJsonDocument(QJsonObject{{"schema_version", 1}, {"packages", QJsonArray{material(zip)}}}).toJson();
    manager.enqueue(releases); manager.enqueue(materials);
    const QString runtime = temp.filePath("runtime"), user = temp.filePath("user");
    CHECK(QDir().mkpath(runtime));
    QSanPackages::clearCatalog();
    UpdateDialog dialog(runtime, user, "20251231", QString(40, 'a'), true, &settings, nullptr, &manager);
    dialog.show();
    auto *list = dialog.findChild<QListWidget *>();
    auto *button = dialog.findChild<QPushButton *>("downloadSelectedUpdate");
    auto *status = dialog.findChild<QLabel *>("updateStatus");
    auto *details = dialog.findChild<QTextEdit *>("updateDetails");
    auto *cancel = dialog.findChild<QPushButton *>("cancelUpdateOperation");
    CHECK(list && button && status && details && cancel);
    CHECK(until([&] { return manager.requests().size() == 2 && list->count() == 1 && button->isEnabled(); }));
    CHECK(manager.requests().size() == 2); // Merely checking never starts a download.
    CHECK(details->toPlainText().contains("<b>Plain notes</b>"));
    CHECK(!details->toHtml().contains("<b>Plain notes</b>"));
    CHECK(dialog.property("controllerLocalDialog").toBool());
    CHECK(cancel->focusPolicy() != Qt::NoFocus);
    CHECK(button->focusPolicy() != Qt::NoFocus);

    FakeResponse download; download.body = zip;
    manager.enqueue(download);
    button->setFocus(); QTest::keyClick(button, Qt::Key_Space);
    CHECK(until([&] { return status->text().contains("is staged"); }));
    CHECK(manager.requests().size() == 3);
    PackageStore store(runtime, user);
    CHECK(store.hasPending());
    CHECK(!QFile::exists(runtime + "/packages/material-test/image/test.png"));
    CHECK(!button->isEnabled());
    QString error;
    CHECK(store.prepareStartup(&error));
    QFile active(runtime + "/packages/material-test/image/test.png");
    CHECK(active.open(QIODevice::ReadOnly)); CHECK(active.readAll() == "new asset");

    // Escape during a network operation cancels it and closes through the same
    // Qt keyboard path used by ControllerRouter's Back action.
    FakeManager offline;
    FakeResponse waiting; waiting.autoDeliver = false;
    offline.enqueue(waiting);
    UpdateDialog cancelling(runtime, user, "20251231", QString(40, 'a'), true, &settings, nullptr, &offline);
    cancelling.show();
    CHECK(until([&] { return offline.requests().size() == 1; }));
    auto *cancelButton = cancelling.findChild<QPushButton *>("cancelUpdateOperation");
    CHECK(cancelButton->isEnabled());
    CHECK(cancelButton->hasFocus());
    QTest::keyClick(cancelButton, Qt::Key_Escape);
    CHECK(until([&] { return !cancelling.isVisible(); }));
    CHECK(offline.requests().size() == 1); // Cancel must not advance to R2.

    // HTTP rate limiting still permits the independent material check.
    FakeManager limited;
    FakeResponse rate; rate.status = 429; rate.body = "rate limited";
    limited.enqueue(rate); limited.enqueue(materials);
    UpdateDialog rateDialog(runtime, user, "20251231", QString(40, 'a'), true, &settings, nullptr, &limited);
    rateDialog.show();
    auto *rateList = rateDialog.findChild<QListWidget *>();
    CHECK(until([&] { return limited.requests().size() == 2 && rateList->count() == 1; }));
    CHECK(rateDialog.findChild<QLabel *>("updateStatus")->text().contains("rate limit"));
    rateDialog.reject();
    dialog.reject();
    CHECK(ancestryCase(QString(40, 'a'), true, true) == 0);
    CHECK(ancestryCase(QString(40, 'a'), true, false) == 0);
    CHECK(ancestryCase(QString(40, 'b'), false, true) == 0);
    qInfo() << "Offline widget check/download/stage, plain notes, cancellation and rate-limit flow passed";
    return 0;
}
