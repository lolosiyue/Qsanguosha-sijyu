#include "server-config.h"

#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QSettings>

#include <initializer_list>

namespace
{
enum class ValueKind
{
    Boolean,
    Integer,
    String,
    StringList,
    Enumeration,
    BindAddress
};

struct SettingSpec
{
    QString key;
    ValueKind kind;
    QVariant defaultValue;
    qlonglong minimum = 0;
    qlonglong maximum = 0;
    QStringList allowedValues;
    bool allowEmpty = true;
};

SettingSpec booleanSetting(const char *key, bool defaultValue)
{
    return {QString::fromLatin1(key), ValueKind::Boolean, defaultValue};
}

SettingSpec integerSetting(const char *key, qlonglong defaultValue,
                           qlonglong minimum, qlonglong maximum)
{
    return {QString::fromLatin1(key), ValueKind::Integer, defaultValue,
            minimum, maximum};
}

SettingSpec stringSetting(const char *key, const char *defaultValue = "",
                          bool allowEmpty = true)
{
    SettingSpec spec{QString::fromLatin1(key), ValueKind::String,
                     QString::fromUtf8(defaultValue)};
    spec.allowEmpty = allowEmpty;
    return spec;
}

SettingSpec stringListSetting(const char *key)
{
    return {QString::fromLatin1(key), ValueKind::StringList, QStringList()};
}

SettingSpec enumSetting(const char *key, const char *defaultValue,
                        std::initializer_list<const char *> allowedValues)
{
    QStringList allowed;
    for (const char *value : allowedValues)
        allowed << QString::fromLatin1(value);
    return {QString::fromLatin1(key), ValueKind::Enumeration,
            QString::fromLatin1(defaultValue), 0, 0, allowed};
}

const QList<SettingSpec> &settingSpecs()
{
    static const QList<SettingSpec> specs = {
        stringSetting("ServerName", "", false),
        stringSetting("GameMode", "02p", false),
        integerSetting("ServerPort", 9527, 0, 65535),
        integerSetting("DetectorPort", 9526, 0, 65535),
        {QStringLiteral("BindAddress"), ValueKind::BindAddress, QStringLiteral("any")},
        stringSetting("Address"),
        integerSetting("OperationTimeout", 15, 1, 86400),
        booleanSetting("OperationNoLimit", false),
        integerSetting("CountDownSeconds", 3, 0, 10),
        integerSetting("NullificationCountDown", 8, 5, 15),

        stringListSetting("BanPackages"),
        booleanSetting("RandomSeat", true),
        booleanSetting("EnableCheat", false),
        booleanSetting("FreeChoose", false),
        booleanSetting("FreeAssign", false),
        booleanSetting("FreeAssignSelf", false),
        booleanSetting("ForbidSIMC", false),
        booleanSetting("DisableChat", false),
        booleanSetting("Enable2ndGeneral", false),
        booleanSetting("EnableSame", false),
        booleanSetting("EnableBasara", false),
        booleanSetting("EnableHegemony", false),
        booleanSetting("EnableMeleeMode", false),
        integerSetting("MaxHpScheme", 0, 0, 3),
        integerSetting("Scheme0Subtraction", 3, -5, 12),
        booleanSetting("PreventAwakenBelow3", false),
        integerSetting("PileSwappingLimitation", 5, -1, 15),
        booleanSetting("WithoutLordskill", false),
        booleanSetting("EnableSPConvert", true),
        integerSetting("MaxChoice", 5, 3, 21),
        integerSetting("LordMaxChoice", -1, -1, 15),
        integerSetting("NonLordMaxChoice", 2, 0, 15),
        integerSetting("HegemonyMaxChoice", 7, 5, 21),
        integerSetting("HegemonyMaxShown", 2, 1, 11),
        enumSetting("HegemonyCompanionReward", "Postponed", {"Instant", "Postponed"}),

        booleanSetting("EnableAI", true),
        booleanSetting("AIChat", true),
        booleanSetting("AIHumanized", true),
        integerSetting("OriginAIDelay", 1000, 0, 600000),
        booleanSetting("AlterAIDelayAD", false),
        integerSetting("AIDelayAD", 0, 0, 600000),
        booleanSetting("SurrenderAtDeath", false),
        booleanSetting("EnableLuckCard", false),
        integerSetting("LuckCardTimes", -1, -1, 10),
        booleanSetting("DisableLua", false),
        booleanSetting("AddGodGeneral", true),
        booleanSetting("GeneralVersionDedup", false),

        enumSetting("1v1/Rule", "2013", {"Classical", "2013", "WZZZ"}),
        booleanSetting("1v1/UsingExtension", false),
        booleanSetting("1v1/UsingCardExtension", false),
        enumSetting("3v3/OfficialRule", "2013", {"Classical", "2012", "2013"}),
        booleanSetting("3v3/UsingExtension", false),
        enumSetting("3v3/RoleChoose", "Normal", {"Normal", "Random", "AllRoles"}),
        booleanSetting("3v3/ExcludeDisasters", true),
        stringListSetting("3v3/ExtensionGenerals"),
        enumSetting("XMode/RoleChooseX", "Normal", {"Normal", "Random", "AllRoles"}),

        integerSetting("BossModeDifficulty", 0, 0, 63),
        booleanSetting("BossYanluo", false),
        booleanSetting("BossModeExp", false),
        booleanSetting("OptionalBoss", false),
        booleanSetting("BossModeEndless", false),
        integerSetting("BossModeTurnLimit", 70, -1, 200),

        stringListSetting("BannedIP"),
        stringListSetting("ForbidPackages"),
        stringSetting("LuaPackages"),
        booleanSetting("OfficialServer", false),
        booleanSetting("EnableOracleConcepts", true),
        integerSetting("MiniSceneStage", 1, 1, 10000),
        integerSetting("jiange_seat", 0, 0, 7),
        integerSetting("fuck_god_spinbox", 3, 0, 100),
        booleanSetting("serverconfig/upnp", true),
        booleanSetting("serverconfig/addtolistserver", false),

        stringListSetting("Banlist/Roles"),
        stringListSetting("Banlist/1v1"),
        stringListSetting("Banlist/Doudizhu"),
        stringListSetting("Banlist/Happy2v2"),
        stringListSetting("Banlist/BossMode"),
        stringListSetting("Banlist/05_ol"),
        stringListSetting("Banlist/06_ol"),
        stringListSetting("Banlist/Basara"),
        stringListSetting("Banlist/Hegemony"),
        stringListSetting("Banlist/Pairs"),
        stringListSetting("Banlist/Cards"),
        stringListSetting("Banlist/Zombie")
    };
    return specs;
}

const SettingSpec *findSpec(const QString &key)
{
    const QList<SettingSpec> &specs = settingSpecs();
    for (const SettingSpec &spec : specs) {
        if (spec.key == key)
            return &spec;
    }
    if (key.startsWith(QLatin1String("Banlist/"))) {
        static const SettingSpec dynamicBanlist = stringListSetting("Banlist/*");
        return &dynamicBanlist;
    }
    return nullptr;
}

bool normalizeBoolean(const QVariant &input, bool &value)
{
    if (input.userType() == QMetaType::Bool) {
        value = input.toBool();
        return true;
    }
    const QString text = input.toString().trimmed().toLower();
    if (text == QLatin1String("true") || text == QLatin1String("1")) {
        value = true;
        return true;
    }
    if (text == QLatin1String("false") || text == QLatin1String("0")) {
        value = false;
        return true;
    }
    return false;
}

bool normalizeValue(const SettingSpec &spec, const QVariant &input,
                    QVariant &normalized, QString &error)
{
    switch (spec.kind) {
    case ValueKind::Boolean: {
        bool value = false;
        if (!normalizeBoolean(input, value)) {
            error = QStringLiteral("expected true, false, 1, or 0");
            return false;
        }
        normalized = value;
        return true;
    }
    case ValueKind::Integer: {
        bool ok = false;
        const QString text = input.toString().trimmed();
        const qlonglong value = text.toLongLong(&ok, 10);
        if (!ok || value < spec.minimum || value > spec.maximum) {
            error = QStringLiteral("expected an integer in range %1-%2")
                .arg(spec.minimum)
                .arg(spec.maximum);
            return false;
        }
        normalized = value;
        return true;
    }
    case ValueKind::String: {
        const QString value = input.toString().trimmed();
        if (!spec.allowEmpty && value.isEmpty()) {
            error = QStringLiteral("value must not be empty");
            return false;
        }
        normalized = value;
        return true;
    }
    case ValueKind::StringList: {
        QStringList values;
        if (input.userType() == QMetaType::QStringList)
            values = input.toStringList();
        else if (!input.toString().trimmed().isEmpty())
            values = input.toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (QString &value : values)
            value = value.trimmed();
        values.removeAll(QString());
        normalized = values;
        return true;
    }
    case ValueKind::Enumeration: {
        const QString value = input.toString().trimmed();
        if (!spec.allowedValues.contains(value)) {
            error = QStringLiteral("expected one of: %1").arg(spec.allowedValues.join(", "));
            return false;
        }
        normalized = value;
        return true;
    }
    case ValueKind::BindAddress: {
        const QString value = input.toString().trimmed().toLower();
        if (value == QLatin1String("any") || value == QLatin1String("any-ipv4")
            || value == QLatin1String("any-ipv6")) {
            normalized = value;
            return true;
        }
        QHostAddress address;
        if (value.isEmpty() || !address.setAddress(value)) {
            error = QStringLiteral("expected a numeric IP address, any, any-ipv4, or any-ipv6");
            return false;
        }
        normalized = address.toString();
        return true;
    }
    }
    return false;
}

QString configValueText(const QVariant &value)
{
    if (value.userType() == QMetaType::Bool)
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.userType() == QMetaType::QStringList)
        return value.toStringList().join(QLatin1Char(','));
    return value.toString();
}
}

ServerConfigLoadResult loadServerConfigFile(const QString &path)
{
    ServerConfigLoadResult result;
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        result.errors << QStringLiteral("configuration file does not exist: %1").arg(path);
        return result;
    }

    QFile probe(info.absoluteFilePath());
    if (!probe.open(QIODevice::ReadOnly)) {
        result.errors << QStringLiteral("cannot read configuration file '%1': %2")
            .arg(path, probe.errorString());
        return result;
    }
    probe.close();

    QSettings settings(info.absoluteFilePath(), QSettings::IniFormat);
    settings.setFallbacksEnabled(false);
    const QStringList keys = settings.allKeys();
    if (settings.status() == QSettings::FormatError) {
        result.errors << QStringLiteral("invalid INI syntax in '%1'").arg(path);
        return result;
    }
    if (settings.status() == QSettings::AccessError) {
        result.errors << QStringLiteral("cannot access configuration file '%1'").arg(path);
        return result;
    }

    for (const QString &key : keys) {
        const SettingSpec *spec = findSpec(key);
        if (!spec) {
            result.errors << QStringLiteral("unknown server setting '%1'").arg(key);
            continue;
        }
        QVariant normalized;
        QString error;
        if (!normalizeValue(*spec, settings.value(key), normalized, error)) {
            result.errors << QStringLiteral("%1: %2 (received '%3')")
                .arg(key, error, settings.value(key).toString());
            continue;
        }
        result.values.insert(key, normalized);
    }

    result.success = result.errors.isEmpty();
    return result;
}

QStringList validateServerConfigValues(const QVariantMap &values)
{
    QStringList errors;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        const SettingSpec *spec = findSpec(it.key());
        if (!spec) {
            errors << QStringLiteral("unknown server setting '%1'").arg(it.key());
            continue;
        }
        QVariant normalized;
        QString error;
        if (!normalizeValue(*spec, it.value(), normalized, error))
            errors << QStringLiteral("%1: %2 (received '%3')")
                .arg(it.key(), error, it.value().toString());
    }
    return errors;
}

QVariantMap defaultServerConfigValues()
{
    QVariantMap values;
    for (const SettingSpec &spec : settingSpecs())
        values.insert(spec.key, spec.defaultValue);
    return values;
}

bool isKnownServerConfigKey(const QString &key)
{
    return findSpec(key) != nullptr;
}

QString serverConfigText(const QVariantMap &values)
{
    QStringList lines;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        lines << QStringLiteral("%1=%2").arg(it.key(), configValueText(it.value()));
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString serverConfigJson(const QVariantMap &values)
{
    const QJsonDocument document(QJsonObject::fromVariantMap(values));
    return QString::fromUtf8(document.toJson(QJsonDocument::Indented));
}
