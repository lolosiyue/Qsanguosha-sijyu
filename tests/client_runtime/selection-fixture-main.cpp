#include "selection-fixture.h"
#include "engine-bootstrap.h"
#include "runtime-paths.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>

namespace {
struct EngineLifetime
{
    ~EngineLifetime()
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        EngineBootstrap::shutdown();
    }
};
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("qsanguosha_rules_fixture_runner"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Replay one client-rules fixture without a frontend or server."));
    parser.addHelpOption();
    parser.addOption(QCommandLineOption(QStringLiteral("fixture"), QStringLiteral("Input JSON fixture"), QStringLiteral("file")));
    parser.addOption(QCommandLineOption(QStringLiteral("output"), QStringLiteral("Output JSON file (written atomically on success)"), QStringLiteral("file")));
    parser.addOption(QCommandLineOption(QStringLiteral("asset-root"), QStringLiteral("Engine data directory"), QStringLiteral("directory")));
    if (!parser.parse(application.arguments())) {
        QTextStream(stderr) << parser.errorText() << Qt::endl;
        return 2;
    }
    if (parser.isSet(QStringLiteral("help"))) parser.showHelp();
    if (!parser.isSet(QStringLiteral("fixture")) || !parser.isSet(QStringLiteral("output"))
        || !parser.positionalArguments().isEmpty()) {
        QTextStream(stderr) << "--fixture and --output are required; positional arguments are not supported" << Qt::endl;
        return 2;
    }
    // Resolve before the asset resolver intentionally changes CWD.
    const QString inputPath = QFileInfo(parser.value(QStringLiteral("fixture"))).absoluteFilePath();
    const QString outputPath = QFileInfo(parser.value(QStringLiteral("output"))).absoluteFilePath();
    if (inputPath == outputPath || (QFileInfo::exists(outputPath)
        && QFileInfo(inputPath).canonicalFilePath() == QFileInfo(outputPath).canonicalFilePath())) {
        QTextStream(stderr) << "output must not overwrite the input fixture" << Qt::endl;
        return 2;
    }
    QFile input(inputPath);
    if (!input.open(QIODevice::ReadOnly)) {
        QTextStream(stderr) << input.errorString() << Qt::endl;
        return 2;
    }
    constexpr qint64 limit = 1024 * 1024;
    const QByteArray bytes = input.read(limit + 1);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (bytes.size() > limit || parseError.error != QJsonParseError::NoError || !document.isObject()
        || document.object().value(QStringLiteral("schema_version")) != QJsonValue(1)) {
        QTextStream(stderr) << "fixture must be a schema_version=1 JSON object of at most 1 MiB: "
                            << parseError.errorString() << Qt::endl;
        return 2;
    }
    QTemporaryDir userData;
    if (!userData.isValid()) {
        QTextStream(stderr) << "cannot create isolated fixture user data" << Qt::endl;
        return 3;
    }
    qputenv("QSAN_USER_DATA_ROOT", userData.path().toUtf8());
    QString error;
    if (!QSanRuntimePaths::resolve(application.arguments(), &error)) {
        QTextStream(stderr) << error << Qt::endl;
        return 3;
    }
    EngineLifetime lifetime;
    if (!EngineBootstrap::initialize(false, &error) || !EngineBootstrap::hasLuaState()) {
        QTextStream(stderr) << "engine/Lua initialization failed: " << error << Qt::endl;
        return 3;
    }
    QJsonObject result;
    if (!ClientRulesFixtures::run(document.object(), &result, &error)) {
        QTextStream(stderr) << "fixture failed: " << error << Qt::endl;
        return 4;
    }
    QSaveFile output(outputPath);
    const QByteArray encoded = ClientRulesFixtures::canonicalJson(result) + '\n';
    if (!output.open(QIODevice::WriteOnly) || output.write(encoded) != encoded.size() || !output.commit()) {
        QTextStream(stderr) << "cannot commit fixture result: " << output.errorString() << Qt::endl;
        return 5;
    }
    return 0;
}
