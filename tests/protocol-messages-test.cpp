#include "json.h"
#include "protocol/card-provenance-message.h"
#include "protocol/skill-instance-message.h"
#include "protocol/state/player-ui-state.h"
#include "protocol/switch-context-message.h"
#include "protocol/sync-pile-message.h"

#include <QJsonDocument>
#include <QTextStream>
#include <QVariantList>
#include <QVariantMap>

namespace
{
bool expect(bool condition, const QString &label)
{
    if (condition)
        return true;
    QTextStream(stderr) << label << " failed\n";
    return false;
}

QVariant throughJson(const QVariant &value)
{
    return QJsonDocument::fromJson(QJsonDocument::fromVariant(value)
                                       .toJson(QJsonDocument::Compact))
        .toVariant();
}

SkillInstanceEntryMessage sampleEntry(bool includePrivateState)
{
    SkillInstanceEntryMessage entry;
    entry.ownerName = "owner";
    entry.instance.skillName = "sample_skill";
    entry.instance.instanceID = 3;
    entry.instance.source = SourceAttached;
    entry.instance.parentRef = SkillInstanceRef("parent_owner",
                                                SkillInstanceKey("parent_skill", 2));
    entry.instance.parent = entry.instance.parentRef.key;
    entry.instance.visible = true;
    entry.instance.bindHead = 2;
    entry.instance.hasAmountOverride = true;
    entry.instance.amountOverride = -1;
    entry.instance.correctState.insert("enabled", true);
    if (includePrivateState)
        entry.privateState.insert("secret", 7);
    return entry;
}

bool cardProvenanceMessages()
{
    CardProvenanceMessage expected;
    expected.kind = "use";
    expected.initiator = "initiator";
    expected.card = "#testCard:.::";
    expected.sourceOwner = "source_owner";
    expected.sourceSkill = "source_skill";
    expected.sourceInstanceId = 3;
    expected.activationOwner = "activation_owner";
    expected.activationSkill = "activation_skill";
    expected.activationInstanceId = 7;

    const QVariantMap v2 = expected.toVariant().toMap();
    CardProvenanceMessage parsed;
    if (!expect(v2.value(QStringLiteral("schema_version")).toInt() == 2
                    && v2.value(QStringLiteral("source_owner")).toString()
                        == expected.sourceOwner,
                "CardProvenance V2 named wire shape")
        || !expect(parsed.tryParse(throughJson(v2)), "CardProvenance V2 JSON parse")
        || !expect(parsed.sourceOwner == expected.sourceOwner
                       && parsed.activationInstanceId == expected.activationInstanceId,
                   "CardProvenance V2 fields"))
        return false;

    QVariantMap wrongVersion = v2;
    wrongVersion.insert(QStringLiteral("schema_version"), 1);
    if (!expect(!parsed.tryParse(throughJson(wrongVersion)),
                "old CardProvenance schema rejected"))
        return false;

    return expect(!parsed.tryParse(JsonArray() << 2 << "use"),
                  "CardProvenance positional shape rejected");
}

bool syncPileMessages()
{
    SyncPileMessage expected;
    expected.playerName = "sgs1";
    expected.pileName = "wooden_ox";
    expected.cardIds << 1 << 4 << 9;
    const QVariantMap wire = expected.toVariant().toMap();
    SyncPileMessage parsed;
    const QVariant decoded = throughJson(wire);
    return expect(wire.value(QStringLiteral("schema_version")).toInt() == 1
                      && wire.value(QStringLiteral("card_ids")).toList()
                          == (QVariantList() << 1 << 4 << 9),
                  "SyncPile named wire shape")
        && expect(parsed.tryParse(decoded), "SyncPile JSON parse")
        && expect(parsed.playerName == expected.playerName
                      && parsed.pileName == expected.pileName
                      && parsed.cardIds == expected.cardIds,
                  "SyncPile fields")
        && expect(!parsed.tryParse(JsonArray() << "sgs1" << "pile" << "not-an-array"),
                  "SyncPile positional shape rejected");
}

bool switchContextMessages()
{
    SwitchContextMessage expected;
    expected.playerName = "sgs2";
    SwitchContextMessage parsed;
    const QVariantMap wire = expected.toVariant().toMap();
    return expect(wire.value(QStringLiteral("schema_version")).toInt() == 1
                      && wire.value(QStringLiteral("player_name")).toString() == "sgs2",
                  "SwitchContext named wire shape")
        && expect(parsed.tryParse(expected.toVariant()) && parsed.playerName == "sgs2",
                  "SwitchContext parse")
        && expect(!parsed.tryParse(2), "SwitchContext malformed rejection");
}

bool skillInstanceEntries()
{
    const SkillInstanceEntryMessage ownerEntry = sampleEntry(true);
    const QVariantMap wire = ownerEntry.toVariant().toMap();

    SkillInstanceEntryMessage parsed;
    if (!expect(wire.value(QStringLiteral("owner_name")).toString() == "owner"
                    && wire.value(QStringLiteral("parent_owner")).toString() == "parent_owner"
                    && wire.value(QStringLiteral("has_amount_override")).toBool(),
                "SkillInstance entry named wire shape")
        || !expect(parsed.tryParse(throughJson(wire)), "SkillInstance entry JSON parse")
        || !expect(parsed.ownerName == ownerEntry.ownerName
                       && parsed.instance.parentRef == ownerEntry.instance.parentRef
                       && parsed.instance.hasAmountOverride
                       && parsed.instance.amountOverride == -1
                       && parsed.privateState.value("secret").toInt() == 7,
                   "SkillInstance entry fields"))
        return false;

    const QVariantMap observerWire = sampleEntry(false).toVariant().toMap();
    if (!expect(!observerWire.contains("state"), "SkillInstance observer privacy"))
        return false;

    return expect(!parsed.tryParse(JsonArray() << "owner" << "sample_skill" << 3),
                  "SkillInstance positional entry rejected");
}

bool skillInstanceMessages()
{
    const SkillInstanceEntryMessage entry = sampleEntry(true);
    const SkillInstanceMessage snapshot = SkillInstanceMessage::makeSnapshot({entry});
    const SkillInstanceMessage upsert = SkillInstanceMessage::makeUpsert(entry);
    const SkillInstanceMessage remove = SkillInstanceMessage::makeRemove("owner", "skill", 3);
    const SkillInstanceMessage amount = SkillInstanceMessage::makeAmount("owner", "skill", 3,
                                                                         true, -2);
    const SkillInstanceMessage correct = SkillInstanceMessage::makeCorrectState(
        "owner", "skill", 3, "set", "enabled", true);
    const SkillInstanceMessage state = SkillInstanceMessage::makeState(
        "owner", "skill", 3, "replace", "", QVariantMap{{"counter", 4}});

    const QList<SkillInstanceMessage> cases = {
        snapshot, upsert, remove, amount, correct, state
    };

    foreach (const auto &testCase, cases) {
        const QVariantMap wire = testCase.toVariant().toMap();
        SkillInstanceMessage parsed;
        if (!expect(wire.value(QStringLiteral("schema_version")).toInt() == 1
                        && wire.value(QStringLiteral("action")).userType()
                            == QMetaType::QString,
                    "SkillInstance action named wire shape")
            || !expect(parsed.tryParse(throughJson(wire)),
                       "SkillInstance action JSON parse")
            || !expect(parsed.action == testCase.action,
                       "SkillInstance action identity"))
            return false;
    }

    SkillInstanceMessage parsed;
    QVariantMap invalidAmount = amount.toVariant().toMap();
    invalidAmount.insert(QStringLiteral("has_amount_override"), QStringLiteral("true"));
    QVariantMap invalidState = state.toVariant().toMap();
    invalidState.insert(QStringLiteral("value"), QStringLiteral("not-a-map"));
    return expect(!parsed.tryParse(invalidAmount),
                  "SkillInstance typed amount rejection")
        && expect(!parsed.tryParse(invalidState),
                   "SkillInstance typed replace rejection");
}

PlayerUIState completePlayerUIState()
{
    PlayerUIState state;
    state.handMax = 7;
    state.offensiveDistance = -2;
    state.defensiveDistance = 3;
    state.maxCardsSkills << "keji^2^sgs1" << "yongsi^F7^sgs2";
    state.offensiveSkills << "mashu";
    state.defensiveSkills << "feiying";
    state.viewAsEquipSkills << "Crossbow^paoxiao";
    return state;
}

bool playerUIStateMessages()
{
    const PlayerUIState expected = completePlayerUIState();
    PlayerUIState parsed;
    if (!expect(parsed.tryParse(expected.toVariant()) && parsed == expected,
                "PlayerUIState round trip"))
        return false;

    PlayerUIState emptyLists;
    emptyLists.handMax = 4;
    PlayerUIState parsedEmptyLists;
    if (!expect(parsedEmptyLists.tryParse(emptyLists.toVariant())
                    && parsedEmptyLists == emptyLists,
                "PlayerUIState empty lists"))
        return false;

    PlayerUIStateMessage expectedMessage;
    expectedMessage.playerName = "sgs1";
    expectedMessage.state = expected;
    PlayerUIStateMessage parsedMessage;
    if (!expect(parsedMessage.tryParse(expectedMessage.toVariant())
                    && parsedMessage.playerName == expectedMessage.playerName
                    && parsedMessage.state == expectedMessage.state,
                "PlayerUIStateMessage round trip"))
        return false;

    const QVariant decodedWireValue = throughJson(expectedMessage.toVariant());
    PlayerUIStateMessage wireMessage;
    if (!expect(wireMessage.tryParse(decodedWireValue)
                    && wireMessage.playerName == expectedMessage.playerName
                    && wireMessage.state == expectedMessage.state,
                "PlayerUIStateMessage JSON round trip"))
        return false;

    QVariantMap missingField = expected.toVariant().toMap();
    missingField.remove("handMax");
    if (!expect(!parsed.tryParse(missingField), "PlayerUIState missing field"))
        return false;

    QVariantMap wrongInteger = expected.toVariant().toMap();
    wrongInteger.insert("handMax", QString("7"));
    if (!expect(!parsed.tryParse(wrongInteger), "PlayerUIState wrong integer type"))
        return false;

    QVariantMap wrongList = expected.toVariant().toMap();
    wrongList.insert("offensiveSkills", QVariantList() << "mashu" << 1);
    if (!expect(!parsed.tryParse(wrongList), "PlayerUIState wrong list type"))
        return false;

    PlayerUIState unchanged = expected;
    QVariantMap invalidNestedState = expected.toVariant().toMap();
    invalidNestedState.insert("defensiveSkills", true);
    if (!expect(!unchanged.tryParse(invalidNestedState) && unchanged == expected,
                "PlayerUIState invalid nested leaves original"))
        return false;

    QVariantMap invalidMessage = expectedMessage.toVariant().toMap();
    invalidMessage.insert("state", invalidNestedState);
    PlayerUIStateMessage unchangedMessage = expectedMessage;
    if (!expect(!unchangedMessage.tryParse(invalidMessage)
                    && unchangedMessage.playerName == expectedMessage.playerName
                    && unchangedMessage.state == expectedMessage.state,
                "PlayerUIStateMessage invalid nested leaves original"))
        return false;

    QVariantMap withUnknownField = expected.toVariant().toMap();
    withUnknownField.insert("futureField", "ignored");
    if (!expect(parsed.tryParse(withUnknownField) && parsed == expected,
                "PlayerUIState unknown field compatibility"))
        return false;

    PlayerUIState different = expected;
    different.handMax++;
    if (!expect(!(different == expected), "PlayerUIState equality"))
        return false;

    QVariantMap wrongPlayerName = expectedMessage.toVariant().toMap();
    wrongPlayerName.insert("player_name", 1);
    return expect(!parsedMessage.tryParse(wrongPlayerName),
                  "PlayerUIStateMessage wrong playerName type");
}
}

int main()
{
    return cardProvenanceMessages()
            && syncPileMessages()
            && switchContextMessages()
            && skillInstanceEntries()
            && skillInstanceMessages()
            && playerUIStateMessages()
        ? 0 : 1;
}
