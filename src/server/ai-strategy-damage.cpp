#include "ai-strategy-damage.h"
#include "room.h"
#include "engine.h"
#include "skill.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QSet>

// Deliberately limited to declared damage state. Arbitrary Room/Card tags and
// another player's hand-card private flags never enter this projection.
void projectAIStrategyDamage(AIWorldView &world, Room &room, ServerPlayer *viewer)
{
    if (!viewer) return;
    QJsonObject damage;
    // extensions/extra.lua:2165-2172 broadcasts $guandu_sj with the chosen event.
    damage.insert("guandu_event", room.getTag("guandu_sj").toString());
    // extensions/kearjsrg.lua:815-824 sets this only on publicly logged Death.
    damage.insert("keqiyanhuo_count", room.getTag("keqiyanhuonum").toInt());
    damage.insert("mode_player_count", Sanguosha->getPlayerCount(room.getMode()));
    QJsonArray armorNames;
    QSet<QString> seenArmor;
    for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
        const auto *card = Sanguosha->getEngineCard(id);
        if (card && card->isKindOf("Armor") && !seenArmor.contains(card->objectName())) {
            seenArmor.insert(card->objectName());
            armorNames.append(card->objectName());
        }
    }
    damage.insert("armor_names", armorNames);
    QJsonObject viewAsSkills;
    const auto takeSkills = [&](const QList<AISkillView> &skills, bool own) {
        for (const auto &skill : skills) {
            const auto *viewAs = Sanguosha->getViewAsSkill(skill.skillName);
            QJsonObject row{{"exists", viewAs != nullptr}};
            if (viewAs && own) row.insert("owner_enabled_at_play", viewAs->isEnabledAtPlay(viewer));
            if (own || !viewAsSkills.contains(skill.skillName)) viewAsSkills.insert(skill.skillName, row);
        }
    };
    takeSkills(world.self.skills, true);
    for (const auto &player : world.players) takeSkills(player.skills, false);
    damage.insert("view_as_skills", viewAsSkills);
    QJsonObject records, jianyuan;
    for (const auto *player : room.getAllPlayers(true)) {
        // NyarzFirst.lua:84-95 and 152-175 broadcast $Arecord / $Aremove and
        // change the public skill description whenever this record changes.
        const auto names = player->property("SkillDescriptionRecord_Djianxiong").toString()
            .split('+', Qt::SkipEmptyParts);
        records.insert(player->objectName(), QJsonArray::fromStringList(names));
        // genius.lua:1801-1803 changes the public skill description on addition.
        jianyuan.insert(player->objectName(), QJsonArray::fromStringList(
            player->property("SkillDescriptionRecord_f_jianyuan").toString().split('+', Qt::SkipEmptyParts)));
    }
    damage.insert("records_by_player", records);
    damage.insert("jianyuan_records_by_player", jianyuan);

    QSet<int> visibleIds;
    const auto take = [&visibleIds](const QList<AICardView> &cards) {
        for (const auto &card : cards) if (card.effectiveId >= 0) visibleIds.insert(card.effectiveId);
    };
    take(world.handCards);
    take(world.self.equips);
    take(world.self.judgingArea);
    take(world.discardPile);
    for (const auto &pile : world.self.piles) if (pile.open) take(pile.cards);
    for (const auto &player : world.players) {
        take(player.equips);
        take(player.judgingArea);
        // A previously revealed hand card's identity does not reveal its tags.
        for (const auto &pile : player.piles) if (pile.open) take(pile.cards);
    }
    const QStringList allowedFlags{"drank", "Qinggang", "yj_nvzhuang_debuff", "hanyong",
        "yb_canqu1_add_damage", "cuijinAddDamage_1", "cuijinAddDamage_2", "gd_liangjunxiangchi"};
    QJsonObject cards;
    for (int id : visibleIds) {
        const auto *card = room.getCard(id);
        if (!card) continue;
        const auto *owner = room.getCardOwner(id);
        const auto place = room.getCardPlace(id);
        // Owner-private card state is authorized. Foreign cards require a
        // currently public zone; an open pile is not permission for hidden tags.
        const bool own = owner == viewer;
        const bool publicZone = place == Player::PlaceTable || place == Player::PlaceEquip
            || place == Player::PlaceDelayedTrick || place == Player::DiscardPile;
        if (!own && !publicZone) continue;
        QJsonObject state{{"midao_nature", card->getTag("heg_midao_nature").toString()},
            {"basis", own ? "owner_card_state" : "public_midao_choice_log"}};
        // extraheg.lua:5598-5601 logs the selected Midao nature publicly.
        // Other damage flags/tags are disclosed only to the current owner.
        if (own) {
            QJsonArray flags;
            for (const auto &flag : allowedFlags) if (card->hasFlag(flag)) flags.append(flag);
            state.insert("flags", flags);
            state.insert("drank_amount", card->getTag("drank").toInt());
        }
        cards.insert(QString::number(id), state);
    }
    damage.insert("cards_by_id", cards);
    world.strategyFacts.insert("damage", damage);
}
