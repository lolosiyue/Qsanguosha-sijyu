#include "excel/excel-view.h"

#include "client/core/client-core.h"
#include "core/protocol.h"

#include <QtTest>

class ExcelViewTest final : public QObject
{
    Q_OBJECT

private slots:
    void snapshotIsRedactedAndNormalized()
    {
        ClientCore core;
        core.state()->setSelfName(QStringLiteral("self"));
        core.state()->setPlayerValue(QStringLiteral("self"), QStringLiteral("seat"), 1);
        core.state()->setPlayerValue(QStringLiteral("self"), QStringLiteral("hand"),
                                     QStringLiteral("must-not-be-copied"));
        core.state()->setCardValue(7, QStringLiteral("owner"), QStringLiteral("self"));
        core.state()->setCardValue(7, QStringLiteral("place"), 0);

        const QJsonObject view = ExcelView::snapshotView(core, QString(),
                                                          {QStringLiteral("public event")});
        QVERIFY(view.contains(QStringLiteral("players")));
        QCOMPARE(view.value(QStringLiteral("hand")).toArray().size(), 1);
        QVERIFY(!view.value(QStringLiteral("players")).toArray().first().toObject()
                    .contains(QStringLiteral("hand")));
        QCOMPARE(view.value(QStringLiteral("logs")).toArray().size(), 1);
    }

    void detailsRejectsUnknownKey()
    {
        ClientCore core;
        QString error;
        QVERIFY(ExcelView::details(core, QString(), QStringLiteral("card"),
                                   QStringLiteral("bad"), &error).isEmpty());
        QCOMPARE(error, QStringLiteral("detail_not_found"));
    }

    void detailsOnlyUsesAuthorizedCardProjection()
    {
        ClientCore core;
        core.state()->setCardValue(4, QStringLiteral("owner"), QStringLiteral("other"));
        core.state()->setCardValue(4, QStringLiteral("place"), 0);
        QString error;
        const QJsonObject value = ExcelView::details(core, QString(), QStringLiteral("card"),
                                                     QStringLiteral("4"), &error);
        QVERIFY(!value.isEmpty());
        QVERIFY(!value.contains(QStringLiteral("name")));
        QVERIFY(!value.contains(QStringLiteral("object_name")));
        QVERIFY(error.isEmpty());
    }

    void malformedTypedLogDoesNotEchoPayload()
    {
        ClientCore core;
        const QString value = ExcelView::presentationText(core, QSanProtocol::S_COMMAND_LOG_SKILL,
                                                          QStringLiteral("fallback"), QVariantMap());
        QVERIFY(value.isEmpty());
    }

    void hiddenHandsRetainCountWithoutFaces()
    {
        ClientCore core;
        core.state()->setSelfName(QStringLiteral("self"));
        core.state()->setPlayerValue(QStringLiteral("other"), QStringLiteral("hand_count"), 6);
        core.state()->setCardValue(4, QStringLiteral("owner"), QStringLiteral("other"));
        core.state()->setCardValue(4, QStringLiteral("place"), 0);
        core.state()->setCardValue(4, QStringLiteral("object_name"), QStringLiteral("stale-face"));
        const QJsonObject view = ExcelView::snapshotView(core, QString());
        // setSelfName also registers a player; locate the opponent by identity
        // instead of assuming the first rendered seat belongs to that player.
        QJsonObject opponent;
        const QJsonArray players = view.value(QStringLiteral("players")).toArray();
        for (const QJsonValue &player : players) {
            const QJsonObject row = player.toObject();
            if (row.value(QStringLiteral("id")).toString() == QStringLiteral("other"))
                opponent = row;
        }
        QVERIFY(!opponent.isEmpty());
        QCOMPARE(opponent.value(QStringLiteral("hand_count")).toInt(), 6);
        QVERIFY(view.value(QStringLiteral("cards")).toArray().isEmpty());
        const QJsonObject detail = ExcelView::details(core, QString(), QStringLiteral("card"), QStringLiteral("4"));
        QVERIFY(detail.value(QStringLiteral("hidden")).toBool());
        QVERIFY(!detail.contains(QStringLiteral("object_name")));
    }

    void skillsPreserveInstanceIdentity()
    {
        ClientCore core;
        core.state()->setSelfName(QStringLiteral("self"));
        core.state()->setPlayerValue(QStringLiteral("self"), QStringLiteral("skills"), QStringList{QStringLiteral("skill")});
        QVariantMap instances;
        for (int id : {1, 2}) {
            instances.insert(QString::number(id), QVariantMap{{QStringLiteral("skill_name"), QStringLiteral("skill")},
                {QStringLiteral("instance_id"), id}, {QStringLiteral("visible"), true}});
        }
        core.state()->setPlayerValue(QStringLiteral("self"), QStringLiteral("skill_instances"), instances);
        const QJsonArray skills = ExcelView::snapshotView(core, QString()).value(QStringLiteral("skills")).toArray();
        QCOMPARE(skills.size(), 2);
        QCOMPARE(skills.at(0).toObject().value(QStringLiteral("instance_id")).toInt(), 1);
        QCOMPARE(skills.at(1).toObject().value(QStringLiteral("instance_id")).toInt(), 2);
        QVERIFY(skills.at(0).toObject().value(QStringLiteral("rowid"))
            != skills.at(1).toObject().value(QStringLiteral("rowid")));
    }

    void plainTextPresentationAndUnlimitedModernCapacity()
    {
        ClientCore core;
        QCOMPARE(ExcelView::presentationText(core, -1, QStringLiteral("<b>hello</b><br/>world\x01")),
            QStringLiteral("hello\nworld"));
        QCOMPARE(ExcelView::catalog(core, QString(), false).value(QStringLiteral("max_players")).toInt(), 0);
        QCOMPARE(ExcelView::catalog(core, QString(), true).value(QStringLiteral("max_players")).toInt(), 10);
    }
};

QTEST_APPLESS_MAIN(ExcelViewTest)
#include "view-test.moc"
