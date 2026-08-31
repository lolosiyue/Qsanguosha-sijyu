#ifndef PROTOCOL_SESSION_PAYLOADS_H
#define PROTOCOL_SESSION_PAYLOADS_H

#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace QSanProtocol {

struct EmptyPayload
{
    static constexpr int SchemaVersion = 1;
    QVariantMap toVariant() const;
    static bool parse(const QVariant &value, EmptyPayload *payload,
                      QString *error = nullptr);
};

struct ServerHelloPayload
{
    static constexpr int SchemaVersion = 1;
    QString gameVersion;
    QString modName;
    int cardCount = 0;

    QVariantMap toVariant() const;
    static bool parse(const QVariant &value, ServerHelloPayload *payload,
                      QString *error = nullptr);
};

struct SignupRequestPayload
{
    static constexpr int SchemaVersion = 1;
    bool reconnectRequested = false;
    QString screenName;
    QString avatar;

    QVariantMap toVariant() const;
    static bool parse(const QVariant &value, SignupRequestPayload *payload,
                      QString *error = nullptr);
};

struct SignupReplyPayload
{
    static constexpr int SchemaVersion = 1;
    bool accepted = false;
    bool reconnected = false;
    QString playerId;
    QString errorCode;
    QString message;

    QVariantMap toVariant() const;
    static bool parse(const QVariant &value, SignupReplyPayload *payload,
                      QString *error = nullptr);
};

struct SetupPayload
{
    static constexpr int SchemaVersion = 1;

    QString serverName;
    QString gameMode;
    QString gameRuleMode;
    int operationTimeout = 0;
    int nullificationCountdown = 0;
    int serverTimeoutGraciousPeriod = 1000;
    QStringList banPackages;
    bool randomSeat = false;
    bool enableCheat = false;
    bool freeChoose = false;
    bool enableSecondGeneral = false;
    bool enableSame = false;
    bool enableBasara = false;
    bool enableHegemony = false;
    bool enableMeleeMode = false;
    bool enableAi = false;
    bool disableChat = false;
    int maxHpScheme = 0;
    int scheme0Subtraction = 0;
    int playerCount = 0;

    QVariantMap toVariant() const;
    static bool parse(const QVariant &value, SetupPayload *payload,
                      QString *error = nullptr);
};

struct ReadyPayload
{
    static constexpr int SchemaVersion = 1;
    bool ready = true;

    QVariantMap toVariant() const;
    static bool parse(const QVariant &value, ReadyPayload *payload,
                      QString *error = nullptr);
};

struct StateSyncPayload
{
    static constexpr int SchemaVersion = 1;
    QString syncId;
    QString phase;
    bool reconnect = true;

    QVariantMap toVariant() const;
    static bool parse(const QVariant &value, StateSyncPayload *payload,
                      QString *error = nullptr);
};

struct DiagnosticPayload
{
    static constexpr int SchemaVersion = 1;
    QString code;
    QString message;
    bool fatal = false;

    QVariantMap toVariant() const;
    static bool parse(const QVariant &value, DiagnosticPayload *payload,
                      QString *error = nullptr);
};

struct NetworkDelayPayload
{
    static constexpr int SchemaVersion = 1;
    QString nonce;

    QVariantMap toVariant() const;
    static bool parse(const QVariant &value, NetworkDelayPayload *payload,
                      QString *error = nullptr);
};

struct CommandResultPayload
{
    static constexpr int SchemaVersion = 1;
    bool success = false;
    QString errorCode;
    QString message;

    QVariantMap toVariant() const;
    static bool parse(const QVariant &value, CommandResultPayload *payload,
                      QString *error = nullptr);
};

struct ChatPayload
{
    static constexpr int SchemaVersion = 1;
    QString text;

    QVariantMap toVariant() const;
    static bool parse(const QVariant &value, ChatPayload *payload,
                      QString *error = nullptr);
};

struct ChatMessagePayload
{
    static constexpr int SchemaVersion = 1;
    QString speaker;
    QString text;

    QVariantMap toVariant() const;
    static bool parse(const QVariant &value, ChatMessagePayload *payload,
                      QString *error = nullptr);
};

struct AddRobotPayload
{
    static constexpr int SchemaVersion = 1;
    bool fillRemaining = false;
    int count = 0;

    QVariantMap toVariant() const;
    static bool parse(const QVariant &value, AddRobotPayload *payload,
                      QString *error = nullptr);
};

struct TrustPayload
{
    static constexpr int SchemaVersion = 1;
    bool trusted = false;

    QVariantMap toVariant() const;
    static bool parse(const QVariant &value, TrustPayload *payload,
                      QString *error = nullptr);
};

struct PausePayload
{
    static constexpr int SchemaVersion = 1;
    bool paused = false;

    QVariantMap toVariant() const;
    static bool parse(const QVariant &value, PausePayload *payload,
                      QString *error = nullptr);
};

struct AnytimeSkillPayload
{
    static constexpr int SchemaVersion = 1;
    QString skillName;

    QVariantMap toVariant() const;
    static bool parse(const QVariant &value, AnytimeSkillPayload *payload,
                      QString *error = nullptr);
};

struct SurrenderRequestPayload
{
    static constexpr int SchemaVersion = 1;
    bool requested = true;

    QVariantMap toVariant() const;
    static bool parse(const QVariant &value, SurrenderRequestPayload *payload,
                      QString *error = nullptr);
};

struct CheatRequestPayload
{
    static constexpr int SchemaVersion = 1;

    QString action;
    QString scriptData;
    QString playerName;
    QString sourcePlayer;
    QString targetPlayer;
    QString generalName;
    int nature = 0;
    int points = 0;
    int stateType = 0;
    int cardId = -1;
    bool secondaryGeneral = false;

    QVariantMap toVariant() const;
    static bool parse(const QVariant &value, CheatRequestPayload *payload,
                      QString *error = nullptr);
};

}

#endif
