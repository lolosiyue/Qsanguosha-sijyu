#include "json.h"
#include "protocol/card-provenance-message.h"
#include "protocol/skill-instance-message.h"
#include "protocol/switch-context-message.h"
#include "protocol/sync-pile-message.h"

#include <QJsonDocument>
#include <QTextStream>

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

    const JsonArray v2 = JsonArray() << 2 << "use" << "initiator" << "#testCard:.::"
                                     << "source_owner" << "source_skill" << 3
                                     << "activation_owner" << "activation_skill" << 7;
    CardProvenanceMessage parsed;
    if (!expect(expected.toVariant() == v2, "CardProvenance V2 wire shape")
        || !expect(parsed.tryParse(throughJson(v2)), "CardProvenance V2 JSON parse")
        || !expect(parsed.sourceOwner == expected.sourceOwner
                       && parsed.activationInstanceId == expected.activationInstanceId,
                   "CardProvenance V2 fields"))
        return false;

    const JsonArray v1 = JsonArray() << 1 << "response" << "legacy_owner" << "#legacy:.::"
                                     << "legacy_source" << 1 << "legacy_activation" << 2;
    if (!expect(parsed.tryParse(throughJson(v1)), "CardProvenance V1 parse")
        || !expect(parsed.sourceOwner == "legacy_owner"
                       && parsed.activationOwner == "legacy_owner",
                   "CardProvenance V1 owner fallback")
        || !expect(parsed.toVariant() == v1, "CardProvenance V1 wire preservation"))
        return false;

    return expect(!parsed.tryParse(JsonArray() << 2 << "use"),
                  "CardProvenance malformed rejection");
}

bool syncPileMessages()
{
    SyncPileMessage expected;
    expected.playerName = "sgs1";
    expected.pileName = "wooden_ox";
    expected.cardIds << 1 << 4 << 9;
    const JsonArray wire = JsonArray() << "sgs1" << "wooden_ox"
                                       << QVariant::fromValue(JsonArray() << 1 << 4 << 9);
    SyncPileMessage parsed;
    const QVariant decoded = throughJson(wire);
    if (!parsed.tryParse(decoded)) {
        const QVariantList values = decoded.toList();
        QTextStream(stderr) << "SyncPile decoded types: " << decoded.typeName()
                            << ", " << values.value(0).typeName()
                            << ", " << values.value(1).typeName()
                            << ", " << values.value(2).typeName() << "\n";
    }
    return expect(expected.toVariant() == wire, "SyncPile wire shape")
        && expect(parsed.tryParse(decoded), "SyncPile JSON parse")
        && expect(parsed.playerName == expected.playerName
                      && parsed.pileName == expected.pileName
                      && parsed.cardIds == expected.cardIds,
                  "SyncPile fields")
        && expect(!parsed.tryParse(JsonArray() << "sgs1" << "pile" << "not-an-array"),
                  "SyncPile malformed rejection");
}

bool switchContextMessages()
{
    SwitchContextMessage expected;
    expected.playerName = "sgs2";
    SwitchContextMessage parsed;
    return expect(expected.toVariant() == QVariant("sgs2"), "SwitchContext wire shape")
        && expect(parsed.tryParse(expected.toVariant()) && parsed.playerName == "sgs2",
                  "SwitchContext parse")
        && expect(!parsed.tryParse(2), "SwitchContext malformed rejection");
}

bool skillInstanceEntries()
{
    const SkillInstanceEntryMessage ownerEntry = sampleEntry(true);
    QVariantMap metadata;
    metadata.insert("has_amount", true);
    metadata.insert("amount", -1);
    metadata.insert("correct_state", QVariantMap{{"enabled", true}});
    metadata.insert("state", QVariantMap{{"secret", 7}});
    const JsonArray wire = JsonArray()
        << "owner" << "sample_skill" << 3 << static_cast<int>(SourceAttached)
        << "parent_owner" << "parent_skill" << 2 << true << 2 << metadata;

    SkillInstanceEntryMessage parsed;
    if (!expect(ownerEntry.toVariant() == wire, "SkillInstance entry wire shape")
        || !expect(parsed.tryParse(throughJson(wire)), "SkillInstance entry JSON parse")
        || !expect(parsed.ownerName == ownerEntry.ownerName
                       && parsed.instance.parentRef == ownerEntry.instance.parentRef
                       && parsed.instance.hasAmountOverride
                       && parsed.instance.amountOverride == -1
                       && parsed.privateState.value("secret").toInt() == 7,
                   "SkillInstance entry fields"))
        return false;

    const QVariant observerWire = sampleEntry(false).toVariant();
    const QVariantMap observerMetadata = observerWire.toList().value(9).toMap();
    if (!expect(!observerMetadata.contains("state"), "SkillInstance observer privacy"))
        return false;

    const JsonArray legacy = JsonArray() << "owner" << "sample_skill" << 3
                                         << static_cast<int>(SourceAcquired)
                                         << "parent_skill" << 2 << true << 1;
    return expect(parsed.tryParse(throughJson(legacy)), "SkillInstance legacy entry parse")
        && expect(parsed.instance.parentRef.ownerObjectName == "owner",
                  "SkillInstance legacy parent owner fallback")
        && expect(!parsed.tryParse(JsonArray() << "owner" << "sample_skill" << "3"),
                  "SkillInstance malformed entry rejection");
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

    const QList<QPair<SkillInstanceMessage, JsonArray>> cases = {
        {snapshot, JsonArray() << "snapshot"
                               << QVariant::fromValue(JsonArray() << entry.toVariant())},
        {upsert, JsonArray() << "upsert" << entry.toVariant()},
        {remove, JsonArray() << "remove" << "owner" << "skill" << 3},
        {amount, JsonArray() << "amount" << "owner" << "skill" << 3 << true << -2},
        {correct, JsonArray() << "correct_state" << "owner" << "skill" << 3
                              << "set" << "enabled" << true},
        {state, JsonArray() << "state" << "owner" << "skill" << 3
                            << "replace" << "" << QVariantMap{{"counter", 4}}}
    };

    foreach (const auto &testCase, cases) {
        SkillInstanceMessage parsed;
        if (!expect(testCase.first.toVariant() == testCase.second,
                    "SkillInstance action wire shape")
            || !expect(parsed.tryParse(throughJson(testCase.second)),
                       "SkillInstance action JSON parse")
            || !expect(parsed.action == testCase.first.action,
                       "SkillInstance action identity"))
            return false;
    }

    SkillInstanceMessage parsed;
    return expect(!parsed.tryParse(JsonArray() << "amount" << "owner" << "skill"
                                               << 3 << "true" << 1),
                  "SkillInstance typed amount rejection")
        && expect(!parsed.tryParse(JsonArray() << "state" << "owner" << "skill"
                                               << 3 << "replace" << "" << "not-a-map"),
                  "SkillInstance typed replace rejection");
}
}

int main()
{
    return cardProvenanceMessages()
            && syncPileMessages()
            && switchContextMessages()
            && skillInstanceEntries()
            && skillInstanceMessages()
        ? 0 : 1;
}
