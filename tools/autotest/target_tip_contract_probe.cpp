#include "target-tip.h"
#include "game-action-model.h"

#include <QCoreApplication>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&failures](const char *name, bool ok) {
        std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
        if (!ok) ++failures;
    };
    TargetTipRules rules;
    const QString declaration = QString::fromUtf8(R"([
        {"text":"額外摸1","when":{"owner":"source","card_name":"slash","candidate_wounded":true,"selected_card_count_gte":1}},
        {"text":"不生效","when":{"owner":"candidate","card_color":"black","selectable":false}}
    ])");
    check("accept declarative rules", rules.setJson(declaration));
    TargetTipQuery query;
    query.viewer = "viewer";
    query.source = "viewer";
    query.candidate = "target";
    query.owner = "source";
    query.cardName = "slash";
    query.cardColor = "black";
    query.selectedCards = {7};
    query.candidateHp = 2;
    query.candidateMaxHp = 3;
    check("selected card and wounded target", rules.targetTip(query) == QString::fromUtf8("額外摸1"));
    const auto beforeCards = query.selectedCards;
    for (int i = 0; i < 100; ++i) rules.targetTip(query);
    check("repeated queries leave values unchanged", query.selectedCards == beforeCards && query.candidateHp == 2 && !query.selectable);
    query.owner = "candidate";
    check("disabled target still receives advisory", rules.targetTip(query) == QString::fromUtf8("不生效") && !query.selectable);
    query.selectable = true;
    check("selectability remains an input", rules.targetTip(query).isEmpty());
    query.owner = "source";
    query.cardName = "jink";
    check("card changes invalidate hint", rules.targetTip(query).isEmpty());
    query.cardName = "slash";
    query.selectedCards.clear();
    check("selected cards participate", rules.targetTip(query).isEmpty());
    query.selectedCards = {7};
    query.viewer.clear();
    check("missing viewer fails closed", rules.targetTip(query).isEmpty());
    query.viewer = "viewer";
    check("reject hidden information predicate", !rules.setJson(R"([{"text":"leak","when":{"candidate_role":"rebel"}}])"));
    check("invalid replacement clears previous rules", rules.targetTip(query).isEmpty());
    check("reject callback string", !rules.setJson(R"([{"text":"x","when":{"callback":"return true"}}])"));
    check("reject wrong condition type", !rules.setJson(R"([{"text":"x","when":{"candidate_wounded":1}}])"));
    check("reject fractional counts", !rules.setJson(R"([{"text":"x","when":{"selected_card_count_gte":1.5}}])"));
    check("reject text controls", !rules.setJson(R"([{"text":"x\ny"}])"));
    check("reject malformed rules", !rules.setJson("["));
    check("empty rules clear", rules.setJson("[]") && rules.targetTip(query).isEmpty());
    check("public skill names and selected targets", rules.setJson(R"([{"text":"known","when":{"candidate_has_skill":"public","candidate_selected":true,"selected_target_count_lte":2}}])"));
    query.candidateSkills = {"public"};
    query.selectedTargets = {"target"};
    check("snapshot predicate matches", rules.targetTip(query) == "known");
    query.candidateSkills.clear();
    check("undisclosed skill fails closed", rules.targetTip(query).isEmpty());
    GameActionEntry entry{"target", "Target", false, true, "Original legality reason", 1, 2};
    const QJsonObject original = entry.toJson();
    entry.targetTip = QString::fromUtf8("不生效");
    QJsonObject withTip = entry.toJson();
    check("action model exposes advisory field", withTip.value("target_tip").toString() == entry.targetTip);
    withTip.insert("target_tip", QString());
    check("tip cannot modify legality votes or submitted identifier", withTip == original);
    return failures ? 1 : 0;
}
