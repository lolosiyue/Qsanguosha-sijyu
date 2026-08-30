#ifndef SIMPLE_CHOICE_PAYLOADS_H
#define SIMPLE_CHOICE_PAYLOADS_H

#include <QString>
#include <QStringList>
#include <QVariant>

namespace QSanProtocol {

struct ChooseGeneralRequestPayload
{
    static constexpr int SchemaVersion = 1;

    QStringList candidates;

    QVariant toDomainVariant() const;
    QVariantMap toV2Variant() const;

    static bool parseDomain(const QVariant &value,
                            ChooseGeneralRequestPayload *payload,
                            QString *error = nullptr);
    static bool parseV2(const QVariant &value,
                        ChooseGeneralRequestPayload *payload,
                        QString *error = nullptr);
};

struct ChooseGeneralReplyPayload
{
    static constexpr int SchemaVersion = 1;

    QString general;

    QVariant toDomainVariant() const;
    QVariantMap toV2Variant() const;

    static bool parseDomain(const QVariant &value,
                            ChooseGeneralReplyPayload *payload,
                            QString *error = nullptr);
    static bool parseV2(const QVariant &value,
                        ChooseGeneralReplyPayload *payload,
                        QString *error = nullptr);
};

struct ChooseSuitRequestPayload
{
    static constexpr int SchemaVersion = 1;

    QVariantMap toV2Variant() const;

    static bool parseV2(const QVariant &value,
                        ChooseSuitRequestPayload *payload,
                        QString *error = nullptr);
};

struct ChooseSuitReplyPayload
{
    static constexpr int SchemaVersion = 1;

    QString suit;

    QVariant toDomainVariant() const;
    QVariantMap toV2Variant() const;

    static bool parseDomain(const QVariant &value,
                            ChooseSuitReplyPayload *payload,
                            QString *error = nullptr);
    static bool parseV2(const QVariant &value,
                        ChooseSuitReplyPayload *payload,
                        QString *error = nullptr);
};

struct ChooseKingdomRequestPayload
{
    static constexpr int SchemaVersion = 1;

    QStringList kingdoms;

    QVariant toDomainVariant() const;
    QVariantMap toV2Variant() const;

    static bool parseDomain(const QVariant &value,
                            ChooseKingdomRequestPayload *payload,
                            QString *error = nullptr);
    static bool parseV2(const QVariant &value,
                        ChooseKingdomRequestPayload *payload,
                        QString *error = nullptr);
};

struct ChooseKingdomReplyPayload
{
    static constexpr int SchemaVersion = 1;

    QString kingdom;

    QVariant toDomainVariant() const;
    QVariantMap toV2Variant() const;

    static bool parseDomain(const QVariant &value,
                            ChooseKingdomReplyPayload *payload,
                            QString *error = nullptr);
    static bool parseV2(const QVariant &value,
                        ChooseKingdomReplyPayload *payload,
                        QString *error = nullptr);
};

struct ChooseOrderRequestPayload
{
    static constexpr int SchemaVersion = 1;

    int reason = 0;

    QVariant toDomainVariant() const;
    QVariantMap toV2Variant() const;

    static bool parseDomain(const QVariant &value,
                            ChooseOrderRequestPayload *payload,
                            QString *error = nullptr);
    static bool parseV2(const QVariant &value,
                        ChooseOrderRequestPayload *payload,
                        QString *error = nullptr);
};

struct ChooseOrderReplyPayload
{
    static constexpr int SchemaVersion = 1;

    int camp = 0;

    QVariant toDomainVariant() const;
    QVariantMap toV2Variant() const;

    static bool parseDomain(const QVariant &value,
                            ChooseOrderReplyPayload *payload,
                            QString *error = nullptr);
    static bool parseV2(const QVariant &value,
                        ChooseOrderReplyPayload *payload,
                        QString *error = nullptr);
};

struct InvokeSkillRequestPayload
{
    static constexpr int SchemaVersion = 1;

    QString skillName;
    QString data;

    QVariant toDomainVariant() const;
    QVariantMap toV2Variant() const;

    static bool parseDomain(const QVariant &value,
                            InvokeSkillRequestPayload *payload,
                            QString *error = nullptr);
    static bool parseV2(const QVariant &value,
                        InvokeSkillRequestPayload *payload,
                        QString *error = nullptr);
};

struct InvokeSkillReplyPayload
{
    static constexpr int SchemaVersion = 1;

    bool invoke = false;

    QVariant toDomainVariant() const;
    QVariantMap toV2Variant() const;

    static bool parseDomain(const QVariant &value,
                            InvokeSkillReplyPayload *payload,
                            QString *error = nullptr);
    static bool parseV2(const QVariant &value,
                        InvokeSkillReplyPayload *payload,
                        QString *error = nullptr);
};

struct SurrenderVoteRequestPayload
{
    static constexpr int SchemaVersion = 1;

    QString initiatorGeneral;

    QVariant toDomainVariant() const;
    QVariantMap toV2Variant() const;

    static bool parseDomain(const QVariant &value,
                            SurrenderVoteRequestPayload *payload,
                            QString *error = nullptr);
    static bool parseV2(const QVariant &value,
                        SurrenderVoteRequestPayload *payload,
                        QString *error = nullptr);
};

struct SurrenderVoteReplyPayload
{
    static constexpr int SchemaVersion = 1;

    bool surrender = false;

    QVariant toDomainVariant() const;
    QVariantMap toV2Variant() const;

    static bool parseDomain(const QVariant &value,
                            SurrenderVoteReplyPayload *payload,
                            QString *error = nullptr);
    static bool parseV2(const QVariant &value,
                        SurrenderVoteReplyPayload *payload,
                        QString *error = nullptr);
};

}

#endif
