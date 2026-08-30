#ifndef MULTIPLE_CHOICE_PAYLOAD_H
#define MULTIPLE_CHOICE_PAYLOAD_H

#include <QString>
#include <QStringList>
#include <QVariant>

namespace QSanProtocol {

struct MultipleChoiceRequestPayload
{
    static constexpr int SchemaVersion = 1;

    QString skillName;
    QStringList options;
    QStringList disabledOptions;
    QString tip;

    QVariant toDomainVariant() const;
    QVariantMap toV2Variant() const;

    static bool parseDomain(const QVariant &value,
                            MultipleChoiceRequestPayload *payload,
                            QString *error = nullptr);
    static bool parseV2(const QVariant &value,
                        MultipleChoiceRequestPayload *payload,
                        QString *error = nullptr);
};

struct MultipleChoiceReplyPayload
{
    static constexpr int SchemaVersion = 1;

    QString choice;

    QVariant toDomainVariant() const;
    QVariantMap toV2Variant() const;

    static bool parseDomain(const QVariant &value,
                            MultipleChoiceReplyPayload *payload,
                            QString *error = nullptr);
    static bool parseV2(const QVariant &value,
                        MultipleChoiceReplyPayload *payload,
                        QString *error = nullptr);
};

}

#endif
