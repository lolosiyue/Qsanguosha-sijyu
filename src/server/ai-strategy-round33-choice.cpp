#include "ai-strategy-round33-choice.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <cmath>
#include <limits>

bool aiRound33ChoiceEvaluationSource(const QString &key)
{
    return key == "use_revise:qiangxi" || key == "use_revise:tiaoxin";
}

std::unique_ptr<Card> aiRound33ChoiceEvaluation(const AIRequest &request, Room &room,
    const ServerPlayer *viewer, const QString &key, const QString &className,
    const QJsonArray &costs, QString *error)
{
    if (error) error->clear();
    auto reject = [error](const QString &message) -> std::unique_ptr<Card> {
        if (error) *error = message;
        return {};
    };
    if (!aiRound33ChoiceEvaluationSource(key) || !viewer || viewer->getRoom() != &room
        || request.decisionId == 0 || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision() || !viewer->isAlive())
        return reject("use-revise evaluation is outside its current owner request");
    const QString skill = key.mid(QStringLiteral("use_revise:").size());
    const QString expected = skill == "qiangxi" ? "QiangxiCard" : "TiaoxinCard";
    if (className != expected || !viewer->hasSkill(skill)
        || costs.size() > (skill == "qiangxi" ? 1 : 0))
        return reject("use-revise evaluation source/class/cost shape differs");

    // Source Card_Parse is a hypothesis. Do not run V2 canActivate/canSelect,
    // hasUsed, using, Jilei or payment checks before the source policy selects.
    QStringList ids;
    for (const auto &value : costs) {
        const double number = value.toDouble(-1);
        if (!value.isDouble() || number < 0 || number > std::numeric_limits<int>::max()
            || number != std::floor(number))
            return reject("use-revise evaluation has an invalid material ID");
        const int id = int(number);
        const Card *material = Sanguosha->getCard(id);
        const Player::Place place = room.getCardPlace(id);
        if (!material || !material->isKindOf("Weapon") || room.getCardOwner(id) != viewer
            || (place != Player::PlaceHand && place != Player::PlaceEquip))
            return reject("use-revise material is not a current owner hand/equip Weapon");
        bool disclosed = false;
        auto matches = [material, id](const AICardView &face) {
            return face.effectiveId == id && face.objectName == material->objectName()
                && face.className == material->getClassName() && face.suit == int(material->getSuit())
                && face.number == material->getNumber();
        };
        const auto &faces = place == Player::PlaceHand ? request.worldView.handCards : request.worldView.self.equips;
        for (const auto &face : faces) if (matches(face)) { disclosed = true; break; }
        if (!disclosed) return reject("use-revise material face was not issued to this viewer");
        ids << QString::number(id);
    }
    const QString wire = "@" + expected + "=" + (ids.isEmpty() ? "." : ids.join('+'));
    const Card *parsed = Card::Parse(wire);
    // Empty error means the actual source constructor is absent, not denial.
    if (!parsed) return {};
    std::unique_ptr<Card> card(const_cast<Card *>(parsed));
    if (!card->isVirtualCard() || card->getClassName() != expected
        || card->getSkillName(false) != skill || card->subcardsLength() != costs.size())
        return reject("use-revise V1 parsed policy identity differs");
    for (int i = 0; i < costs.size(); ++i)
        if (card->getSubcards().at(i) != costs.at(i).toInt())
            return reject("use-revise V1 parsed policy changed material order");
    return card;
}
