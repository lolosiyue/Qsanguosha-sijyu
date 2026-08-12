#include "protocol/state/player-ui-state.h"

#include <QJsonDocument>
#include <QVariantList>
#include <QVariantMap>

namespace
{
PlayerUIState completeState()
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
}

int main()
{
    const PlayerUIState expected = completeState();
    PlayerUIState parsed;
    if (!parsed.tryParse(expected.toVariant()) || !(parsed == expected))
        return 1;

    PlayerUIState emptyLists;
    emptyLists.handMax = 4;
    PlayerUIState parsedEmptyLists;
    if (!parsedEmptyLists.tryParse(emptyLists.toVariant()) || !(parsedEmptyLists == emptyLists))
        return 2;

    PlayerUIStateMessage expectedMessage;
    expectedMessage.playerName = "sgs1";
    expectedMessage.state = expected;
    PlayerUIStateMessage parsedMessage;
    if (!parsedMessage.tryParse(expectedMessage.toVariant())
        || parsedMessage.playerName != expectedMessage.playerName
        || !(parsedMessage.state == expectedMessage.state))
        return 3;

    const QByteArray wireData = QJsonDocument::fromVariant(expectedMessage.toVariant())
                                    .toJson(QJsonDocument::Compact);
    const QVariant decodedWireValue = QJsonDocument::fromJson(wireData).toVariant();
    PlayerUIStateMessage wireMessage;
    if (!wireMessage.tryParse(decodedWireValue)
        || wireMessage.playerName != expectedMessage.playerName
        || !(wireMessage.state == expectedMessage.state))
        return 12;

    QVariantMap missingField = expected.toVariant().toMap();
    missingField.remove("handMax");
    if (parsed.tryParse(missingField))
        return 4;

    QVariantMap wrongInteger = expected.toVariant().toMap();
    wrongInteger.insert("handMax", QString("7"));
    if (parsed.tryParse(wrongInteger))
        return 5;

    QVariantMap wrongList = expected.toVariant().toMap();
    wrongList.insert("offensiveSkills", QVariantList() << "mashu" << 1);
    if (parsed.tryParse(wrongList))
        return 6;

    PlayerUIState unchanged = expected;
    QVariantMap invalidNestedState = expected.toVariant().toMap();
    invalidNestedState.insert("defensiveSkills", true);
    if (unchanged.tryParse(invalidNestedState) || !(unchanged == expected))
        return 7;

    QVariantMap invalidMessage = expectedMessage.toVariant().toMap();
    invalidMessage.insert("state", invalidNestedState);
    PlayerUIStateMessage unchangedMessage = expectedMessage;
    if (unchangedMessage.tryParse(invalidMessage)
        || unchangedMessage.playerName != expectedMessage.playerName
        || !(unchangedMessage.state == expectedMessage.state))
        return 8;

    QVariantMap withUnknownField = expected.toVariant().toMap();
    withUnknownField.insert("futureField", "ignored");
    if (!parsed.tryParse(withUnknownField) || !(parsed == expected))
        return 9;

    PlayerUIState different = expected;
    different.handMax++;
    if (different == expected)
        return 10;

    QVariantMap wrongPlayerName = expectedMessage.toVariant().toMap();
    wrongPlayerName.insert("playerName", 1);
    if (parsedMessage.tryParse(wrongPlayerName))
        return 11;

    return 0;
}
