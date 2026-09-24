#include "generalselector.h"
#include "engine.h"
//#include "serverplayer.h"
//#include "util.h"
#include "room.h"
#include "settings.h"
#include <QRegularExpression>

static GeneralSelector *Selector;

GeneralSelector *GeneralSelector::getInstance()
{
    if (Selector == nullptr) {
        Selector = new GeneralSelector;
        //@todo: this setParent is illegitimate in QT and is equivalent to calling
        // setParent(nullptr). So taking it off at the moment until we figure out
        // a way to do it.
        //Selector->setParent(Sanguosha);
    }

    return Selector;
}

GeneralSelector::GeneralSelector()
{
    loadFirstGeneralTable();
    loadSecondGeneralTable();
    load3v3Table();
    load1v1Table();
}

QString GeneralSelector::selectFirst(ServerPlayer *player, const QStringList &candidates)
{
    QMap<QString, qreal> values;
    QString role = player->getRole();
    ServerPlayer *lord = player->getRoom()->getLord();
    foreach (QString candidate, candidates) {
        qreal value = 5.0;
        const General *general = Sanguosha->getGeneral(candidate);
        if (role == "loyalist" && lord && (general->getKingdom() == lord->getKingdom() || general->getKingdom() == "god"))
            value *= 1.04;
        if (role == "rebel" && lord && lord->getGeneral() && lord->getGeneral()->hasSkill("xueyi")
            && general->getKingdom() == "qun")
            value *= 0.8;
        if (role != "loyalist" && lord && lord->getGeneral() && lord->getGeneral()->hasSkill("shichou")
            && general->getKingdom() == "shu")
            value *= 0.1;
        if (role == "rebel" && lord && lord->getGeneral() && lord->getGeneral()->hasSkill("guiming")
            && general->getKingdom() == "wu")
            value *= 0.5;
        QString key = QString("_:%1:%2").arg(candidate).arg(role);
        value *= qPow(1.1, first_general_table.value(key, 0.0));
        if (lord) {
            QString key2 = QString("%1:%2:%3").arg(lord->getGeneralName()).arg(candidate).arg(role);
            value *= qPow(1.1, first_general_table.value(key2, 0.0));
        }
        values.insert(candidate, value);
    }

    QStringList _candidates = candidates;
    QStringList choice_list;
    while (!_candidates.isEmpty() && choice_list.length() < 6) {
        qreal max = -1;
        QString choice = "";
        foreach (QString candidate, _candidates) {
            qreal value = values.value(candidate, 5.0);
            if (value > max) {
                max = value;
                choice = candidate;
            }
        }
        choice_list << choice;
        _candidates.removeOne(choice);
    }

    QString max_general;
    int rnd = qsanRandomBounded(100);
    int total = choice_list.length();
    int prob[6] = { 70, 85, 92, 95, 97, 99 };
    for (int i = 0; i < 6; i++) {
        if (rnd <= prob[i] || total <= i + 1) {
            max_general = choice_list.at(i).split(":").at(0);
            break;
        }
    }

    Q_ASSERT(!max_general.isEmpty());
    return max_general;
}

QString GeneralSelector::selectSecond(ServerPlayer *player, const QStringList &candidates)
{
    QString first = player->getGeneralName();

    int max = -1;
    QString max_general;

    foreach (QString candidate, candidates) {
        QString key = QString("%1+%2").arg(first).arg(candidate);
        int value = second_general_table.value(key, 0);
        if (value == 0) {
            key = QString("%1+%2").arg(candidate).arg(first);
            value = second_general_table.value(key, 50);
        }

        if (value > max) {
            max = value;
            max_general = candidate;
        }
    }

    Q_ASSERT(!max_general.isEmpty());

    return max_general;
}

QString GeneralSelector::selectHegemonyPair(const QStringList &legalPairs)
{
    if (legalPairs.isEmpty()) return QString();
    // XXY selection values are separate from the identity-mode selector tables.
    if (hegemony_general_table.isEmpty()) {
        QFile file("etc/hegemony-general-value.txt");
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream stream(&file);
            const QRegularExpression row(QStringLiteral("^(\\w+)\\s+(\\d+)\\s*$"));
            while (!stream.atEnd()) {
                const auto match = row.match(stream.readLine());
                if (match.hasMatch())
                    hegemony_general_table.insert(match.captured(1), match.captured(2).toInt());
            }
        }
    }
    auto valueOf = [this](QString name) {
        if (name.startsWith("heg_")) name.remove(0, 4);
        return hegemony_general_table.value(name, 5);
    };
    auto hasSkill = [](const General *general, const QString &name) {
        return general->hasSkill(name) || general->hasSkill("heg_" + name)
            || general->hasSkill("nos" + name);
    };
    QStringList kingdoms = Sanguosha->getKingdoms();
    kingdoms.removeAll("god");
    kingdoms.removeAll("careerist");
    kingdoms.removeAll("ye");
    qsanShuffle(kingdoms);
    QString best = legalPairs.first();
    double bestScore = -1e9;
    // Only rank server-admitted ordered pairs; scoring never grants legality.
    for (const QString &pair : legalPairs) {
        const QStringList names = pair.split('+');
        if (names.size() != 2) continue;
        const General *head = Sanguosha->getGeneral(names.first());
        const General *deputy = Sanguosha->getGeneral(names.last());
        if (!head || !deputy) continue;
        const QStringList common = head->compareKingdomsWith(deputy);
        double score = valueOf(names.first()) + valueOf(names.last());
        int preference = -1;
        for (const QString &kingdom : common)
            preference = qMax(preference, kingdoms.indexOf(kingdom));
        score += preference - 1;
        const int hp = head->getMaxHpHead() + deputy->getMaxHpDeputy();
        if (hp % 2) score -= 1;
        if (head->isCompanionWith(names.last())) score += 3;
        if (head->isFemale()) {
            if (head->getKingdom() == "wu") score -= 0.5;
            else if (head->getKingdom() != "qun") score += 0.5;
        } else if (head->getKingdom() == "qun") score += 0.5;
        if (hasSkill(head, "baoling") && valueOf(names.last()) > 6) score -= 5;
        for (const QString &skill : {QString("cunsi"), QString("jianglve")})
            if (hasSkill(head, skill)) score -= 2;
        if (hasSkill(head, "qingyin")) score -= 1;
        for (const QString &skill : {QString("enyuan"), QString("tianxiang"), QString("diancai"),
                                     QString("qice"), QString("xishe")})
            if (hasSkill(head, skill)) score += 0.5;
        if (hasSkill(head, "xiaoji")) score += 1;
        if (hp < 8) {
            for (const General *general : {head, deputy})
                for (const QString &skill : {QString("zhiheng"), QString("zaiqi"), QString("kurou")})
                    if (hasSkill(general, skill)) score -= 5;
        }
        // A conversion bonus requires a registered sovereign, not just a skill name.
        const QString base = names.first().startsWith("heg_") ? names.first().mid(4) : names.first();
        if (Config.value("EnableLordConvertion", true).toBool()
            && Sanguosha->getGeneral("heg_lord_" + base) && qsanRandomBounded(3) > 0)
            score += 10;
        if (score > bestScore) {
            bestScore = score;
            best = pair;
        }
    }
    return best;
}

QString GeneralSelector::select3v3(ServerPlayer *, const QStringList &candidates)
{
    return selectHighest(priority_3v3_table, candidates, 5);
}

QString GeneralSelector::select1v1(const QStringList &candidates)
{
    return selectHighest(priority_1v1_table, candidates, 50);
}

QString GeneralSelector::selectHighest(const QHash<QString, int> &table, const QStringList &candidates, int default_value)
{
    int max = -1;
    QString max_general;

    foreach (QString candidate, candidates) {
        int value = table.value(candidate, default_value);

        if (value > max) {
            max = value;
            max_general = candidate;
        }
    }

    Q_ASSERT(!max_general.isEmpty());

    return max_general;
}

static bool CompareByMaxHp(const QString &a, const QString &b)
{
    const General *g1 = Sanguosha->getGeneral(a);
    const General *g2 = Sanguosha->getGeneral(b);

    return g1->getMaxHp() < g2->getMaxHp();
}

QStringList GeneralSelector::arrange3v3(ServerPlayer *player)
{
    QStringList arranged = player->getSelected();
    qsanShuffle(arranged);
    arranged = arranged.mid(0, 3);

    std::sort(arranged.begin(), arranged.end(), CompareByMaxHp);
    arranged.swapItemsAt(0, 1);

    return arranged;
}

static bool CompareFunction(const QString &first, const QString &second)
{
    return Selector->get1v1ArrangeValue(first) > Selector->get1v1ArrangeValue(second);
}

int GeneralSelector::get1v1ArrangeValue(const QString &name)
{
    int value = priority_1v1_table.value(name, 5);
    if (sacrifice.contains(name))
        value += 1000;
    return value;
}

QStringList GeneralSelector::arrange1v1(ServerPlayer *player)
{
    QStringList arranged = player->getSelected();
    std::sort(arranged.begin(), arranged.end(), CompareFunction);

    QStringList result;
    int i;
    for (i = 0; i < 3; i++) {
        if (get1v1ArrangeValue(arranged[i]) > 1000) {
            result << arranged[i];
            break;
        }
    }
    if (!result.isEmpty()) {
        int strong = (i == 0) ? 1 : 0;
        int weak = (i == 2) ? 1 : 2;
        result << arranged[weak] << arranged[strong];
    } else {
        result << arranged[1] << arranged[2] << arranged[0];
    }

    return result;
}

void GeneralSelector::loadFirstGeneralTable()
{
    loadFirstGeneralTable("loyalist");
    loadFirstGeneralTable("rebel");
    loadFirstGeneralTable("renegade");
}

void GeneralSelector::loadFirstGeneralTable(const QString &role)
{
    QFile file(QString("etc/%1.txt").arg(role));
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream stream(&file);
        while (!stream.atEnd()) {
            QString lord_name, name;
            stream >> lord_name >> name;
            qreal value;
            stream >> value;

            QString key = QString("%1:%2:%3").arg(lord_name).arg(name).arg(role);
            first_general_table.insert(key, value);
        }

        file.close();
    }
}

void GeneralSelector::loadSecondGeneralTable()
{
    QFile file("etc/double-generals.txt");
    if (file.open(QIODevice::ReadOnly)) {
		static const QRegularExpression rx("^(\\w+)\\s+(\\w+)\\s+(\\d+)$");
        QTextStream stream(&file);
        while (!stream.atEnd()) {
            QString line = stream.readLine();
			const QRegularExpressionMatch match = rx.match(line);
			if (!match.hasMatch())
				continue;

			QStringList texts = match.capturedTexts();
            QString first = texts.at(1);
            QString second = texts.at(2);
            int value = texts.at(3).toInt();

            QString key = QString("%1+%2").arg(first).arg(second);
            second_general_table.insert(key, value);
        }

        file.close();
    }
}

void GeneralSelector::load3v3Table()
{
    QFile file("etc/3v3-priority.txt");
    if (file.open(QIODevice::ReadOnly)) {
        static QTextStream stream(&file);
        while (!stream.atEnd()) {
            QString name;
            int priority;

            stream >> name >> priority;

            priority_3v3_table.insert(name, priority);
        }

        file.close();
    }
}

void GeneralSelector::load1v1Table()
{
    QFile file("etc/1v1-priority.txt");
    if (file.open(QIODevice::ReadOnly)) {
		static const QRegularExpression rx("^(\\w+)\\s+(\\d+)\\s*(\\*)?$");
        QTextStream stream(&file);
        while (!stream.atEnd()) {
            QString line = stream.readLine();
			const QRegularExpressionMatch match = rx.match(line);
			if (!match.hasMatch())
				continue;

            // capturedTexts() stops at the last group that took part in the match, so
            // a line without the trailing '*' yields only three entries. captured(3)
            // answers an empty string for that optional group instead of going past the end.
            const QString name = match.captured(1);
            const int priority = match.captured(2).toInt();

            priority_1v1_table.insert(name, priority);

            if (!match.captured(3).isEmpty())
                sacrifice << name;
        }

        file.close();
    }
}

