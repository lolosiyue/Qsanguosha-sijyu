#ifndef RESOLUTION_STATE_MESSAGE_H
#define RESOLUTION_STATE_MESSAGE_H

#include <QSet>
#include <QStringList>
#include <QVariant>
#include "protocol-message-utils.h"

// Each lifecycle notification carries an authoritative public stack. Replacing
// it (rather than replaying push/pop) also makes duplicate replay delivery safe.
struct ResolutionStateMessage
{
    QString phase = QStringLiteral("reset");
    QVariantList frames;

    QVariantMap toVariant() const
    {
        return {{QStringLiteral("schema_version"), 1},
                {QStringLiteral("phase"), phase}, {QStringLiteral("frames"), frames}};
    }

    static bool parse(const QVariant &value, ResolutionStateMessage *result,
                      QString *error = nullptr)
    {
        const auto fail = [error]() {
            if (error) *error = QStringLiteral("Invalid ResolutionStatePayload");
            return false;
        };
        if (!result || value.userType() != QMetaType::QVariantMap) return fail();
        const QVariantMap object = value.toMap();
        int schema = 0;
        if (object.size() != 3
            || !ProtocolMessageUtils::tryParseInt(object.value(QStringLiteral("schema_version")), schema) || schema != 1
            || object.value(QStringLiteral("phase")).userType() != QMetaType::QString
            || object.value(QStringLiteral("frames")).userType() != QMetaType::QVariantList)
            return fail();
        ResolutionStateMessage parsed;
        parsed.phase = object.value(QStringLiteral("phase")).toString();
        if (!QStringList{QStringLiteral("begin"), QStringLiteral("update"),
                         QStringLiteral("end"), QStringLiteral("reset")}.contains(parsed.phase))
            return fail();
        parsed.frames = object.value(QStringLiteral("frames")).toList();
        QSet<QString> ids;
        QString parent;
        for (const QVariant &value : parsed.frames) {
            if (value.userType() != QMetaType::QVariantMap) return fail();
            const QVariantMap frame = value.toMap();
            // Only recipient-safe presentation fields are admitted; no raw card,
            // skill instance, request body or server pointer can enter this state.
            const QStringList fields{QStringLiteral("id"), QStringLiteral("parent_id"),
                QStringLiteral("kind"), QStringLiteral("actor"), QStringLiteral("source"),
                QStringLiteral("affected"), QStringLiteral("card_name")};
            if (frame.size() != fields.size() + 1) return fail();
            for (const QString &field : fields)
                if (frame.value(field).userType() != QMetaType::QString) return fail();
            const QString id = frame.value(QStringLiteral("id")).toString();
            bool validId = false;
            const quint64 number = id.toULongLong(&validId);
            if (!validId || number == 0 || QString::number(number) != id || ids.contains(id)
                || frame.value(QStringLiteral("parent_id")).toString() != parent)
                return fail();
            if (!QStringList{QStringLiteral("card"), QStringLiteral("effect"),
                    QStringLiteral("damage"), QStringLiteral("recover"), QStringLiteral("judge"),
                    QStringLiteral("dying"), QStringLiteral("skill")}
                    .contains(frame.value(QStringLiteral("kind")).toString())) return fail();
            const QVariant targets = frame.value(QStringLiteral("targets"));
            if (targets.userType() != QMetaType::QVariantList) return fail();
            for (const QVariant &target : targets.toList())
                if (target.userType() != QMetaType::QString) return fail();
            ids.insert(id);
            parent = id;
        }
        if ((parsed.phase == QLatin1String("begin") || parsed.phase == QLatin1String("update"))
            && parsed.frames.isEmpty()) return fail();
        *result = parsed;
        return true;
    }
};

#endif
