#ifndef TARGET_TIP_H
#define TARGET_TIP_H

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <cmath>

// Presentation values only. Never carry Player/Card/Room pointers or private
// roles, hidden cards, private marks, or skill-instance state into this query.
struct TargetTipQuery
{
    QString viewer;
    QString source;
    QString candidate;
    QString owner; // source, candidate, or card: the provider of these rules.
    QString cardName;
    QString cardType;
    QString cardColor;
    QStringList selectedTargets;
    QList<int> selectedCards; // Only the viewer's current selection.
    QStringList sourceSkills;
    QStringList candidateSkills;
    int sourceHp = 0;
    int candidateHp = 0;
    int candidateMaxHp = 0;
    bool selectable = false; // Existing UI verdict, never recomputed by tips.
};

class TargetTipRules
{
public:
    // Registration-time data validation; invalid replacement clears old rules.
    bool setJson(const QString &json)
    {
        m_rules = {};
        if (json.toUtf8().size() > 16384) return false;
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !document.isArray()) return false;
        const QJsonArray rules = document.array();
        if (rules.size() > 32) return false;
        const QSet<QString> strings = {"owner", "card_name", "card_type", "card_color",
            "source_has_skill", "candidate_has_skill"};
        const QSet<QString> booleans = {"candidate_self", "candidate_selected",
            "candidate_wounded", "selectable"};
        const QSet<QString> numbers = {"candidate_hp_lte", "source_hp_lte",
            "selected_target_count_lte", "selected_card_count_gte"};
        for (const QJsonValue &value : rules) {
            if (!value.isObject()) return false;
            const QJsonObject rule = value.toObject();
            for (auto it = rule.begin(); it != rule.end(); ++it)
                if (it.key() != "text" && it.key() != "when") return false;
            if (!rule.value("text").isString()) return false;
            const QString text = rule.value("text").toString();
            if (text.isEmpty() || text.size() > 120) return false;
            for (const QChar ch : text)
                if (ch.category() == QChar::Other_Control || ch.category() == QChar::Other_Format)
                    return false;
            if (rule.contains("when") && !rule.value("when").isObject()) return false;
            const QJsonObject when = rule.value("when").toObject();
            for (auto it = when.begin(); it != when.end(); ++it) {
                if (strings.contains(it.key())) {
                    if (!it.value().isString() || it.value().toString().isEmpty()
                        || it.value().toString().size() > 120) return false;
                    if (it.key() == "owner" && !QStringList{"source", "candidate", "card"}.contains(it.value().toString()))
                        return false;
                } else if (booleans.contains(it.key())) {
                    if (!it.value().isBool()) return false;
                } else if (numbers.contains(it.key())) {
                    const double number = it.value().toDouble();
                    if (!it.value().isDouble() || number < -10000 || number > 10000
                        || number != std::floor(number)) return false;
                } else return false;
            }
        }
        m_rules = rules;
        return true;
    }

    // Pure, bounded first-match query. No callbacks, engine access or mutation.
    QString targetTip(const TargetTipQuery &q) const
    {
        if (q.viewer.isEmpty() || q.source.isEmpty() || q.candidate.isEmpty() || q.cardName.isEmpty())
            return {};
        for (const QJsonValue &value : m_rules) {
            const QJsonObject rule = value.toObject();
            const QJsonObject when = rule.value("when").toObject();
            bool matches = true;
            for (auto it = when.begin(); matches && it != when.end(); ++it) {
                const QString &key = it.key();
                const QJsonValue v = it.value();
                if (key == "owner") matches = q.owner == v.toString();
                else if (key == "card_name") matches = q.cardName == v.toString();
                else if (key == "card_type") matches = q.cardType == v.toString();
                else if (key == "card_color") matches = q.cardColor == v.toString();
                else if (key == "source_has_skill") matches = q.sourceSkills.contains(v.toString());
                else if (key == "candidate_has_skill") matches = q.candidateSkills.contains(v.toString());
                else if (key == "candidate_self") matches = (q.candidate == q.source) == v.toBool();
                else if (key == "candidate_selected") matches = q.selectedTargets.contains(q.candidate) == v.toBool();
                else if (key == "candidate_wounded") matches = (q.candidateHp < q.candidateMaxHp) == v.toBool();
                else if (key == "selectable") matches = q.selectable == v.toBool();
                else if (key == "candidate_hp_lte") matches = q.candidateHp <= v.toInt();
                else if (key == "source_hp_lte") matches = q.sourceHp <= v.toInt();
                else if (key == "selected_target_count_lte") matches = q.selectedTargets.size() <= v.toInt();
                else if (key == "selected_card_count_gte") matches = q.selectedCards.size() >= v.toInt();
                else matches = false;
            }
            if (matches) return rule.value("text").toString();
        }
        return {};
    }

private:
    QJsonArray m_rules;
};

#endif
