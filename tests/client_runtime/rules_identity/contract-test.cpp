#include "protocol/session/rules-identity.h"
#include "protocol/session/session-payloads.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    int failures = 0;
    const auto check = [&](bool ok, const QString &name) {
        if (!ok) { ++failures; QTextStream(stderr) << "FAIL " << name << Qt::endl; }
    };
    if (argc != 2) { QTextStream(stderr) << "one vector file is required" << Qt::endl; return 2; }
    QFile input(QString::fromLocal8Bit(argv[1]));
    if (!input.open(QIODevice::ReadOnly)) return 2;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(input.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return 2;
    const QJsonArray cases = document.object().value(QStringLiteral("cases")).toArray();
    if (cases.isEmpty()) return 2;
    for (const QJsonValue &entry : cases) {
        const QJsonObject item = entry.toObject();
        const auto result = QSanProtocol::RulesIdentity::compare(
            item.value(QStringLiteral("local")), item.value(QStringLiteral("server")));
        const QJsonObject expected = item.value(QStringLiteral("expect")).toObject();
        check(result.compatible == expected.value(QStringLiteral("compatible")).toBool()
            && result.reason == expected.value(QStringLiteral("reason")).toString(),
            item.value(QStringLiteral("name")).toString());
    }
    QSanProtocol::ServerHelloPayload old;
    old.gameVersion = QStringLiteral("contract-test"); old.modName = QStringLiteral("test"); old.cardCount = 7;
    const QVariantMap legacy = old.toVariant();
    check(!legacy.contains(QStringLiteral("rules_identity")), QStringLiteral("legacy shape unchanged"));
    QSanProtocol::ServerHelloPayload parsed;
    QString diagnostic;
    check(QSanProtocol::ServerHelloPayload::parse(legacy, &parsed, &diagnostic), QStringLiteral("legacy parsed"));
    const QVariantMap identity = cases.first().toObject().value(QStringLiteral("local")).toObject().toVariantMap();
    old.rulesIdentity = identity;
    check(QSanProtocol::ServerHelloPayload::parse(old.toVariant(), &parsed, &diagnostic)
        && parsed.rulesIdentity == identity, QStringLiteral("identity round trip"));
    check(QSanProtocol::ServerHelloPayload::parse(legacy, &parsed, &diagnostic)
        && parsed.rulesIdentity.isEmpty(), QStringLiteral("legacy parse clears previous identity"));
    QVariantMap malformed = legacy;
    malformed.insert(QStringLiteral("rules_identity"), QStringLiteral("not an object"));
    check(!QSanProtocol::ServerHelloPayload::parse(malformed, &parsed, &diagnostic),
          QStringLiteral("non-object identity rejected"));
    QTextStream(stdout) << "RULES_IDENTITY_NATIVE cases=" << cases.size()
                        << " hello_checks=5 failures=" << failures << Qt::endl;
    return failures == 0 ? 0 : 1;
}
