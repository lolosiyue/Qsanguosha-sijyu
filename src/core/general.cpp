#include "general.h"
#include "engine.h"
//#include "skill.h"
//#include "package.h"
//#include "client.h"
#include "clientstruct.h"
#include "settings.h"

General::General(Package *package, const QString &name, const QString &kingdom,
    int max_hp, bool male, bool hidden, bool never_shown, int start_hp, int start_hujia)
    : QObject(package), kingdom(kingdom), max_hp(max_hp), gender(male ? Male : Female),
    hidden(hidden), never_shown(never_shown), start_hp(start_hp), start_hujia(start_hujia)
{
	QString copy = name;
	lord = copy.contains("$");
    if (lord) copy.remove("$");
	if (copy.contains("*")){
		QStringList copys = copy.split("*");
		sub_package = copys[1];
		copy = copys[0];
	}
    setObjectName(copy);
}

int General::getMaxHp() const
{
    return max_hp;
}

QString General::getKingdom() const
{
	if(kingdom.contains("+")){
		QStringList kins = kingdom.split("+");
        foreach (QString king, kins)
            if (king != "god") return king;
		return kins.first();
	}
    return kingdom;
}

QString General::getKingdoms() const
{
    return kingdom;
}

bool General::isMale() const
{
    return gender == Male;
}

bool General::isFemale() const
{
    return gender == Female;
}

bool General::isNeuter() const
{
    return gender == Neuter;
}

bool General::isSexless() const
{
    return gender == Sexless;
}

void General::setGender(Gender gender)
{
    this->gender = gender;
}

General::Gender General::getGender() const
{
    return gender;
}

bool General::isLord() const
{
    return lord;
}

bool General::isHidden() const
{
    return hidden;
}

bool General::isTotallyHidden() const
{
    return never_shown;
}

void General::setStartHp(int hp)
{
    this->start_hp = hp;
}

int General::getStartHp() const
{
    return qMin(start_hp, max_hp);
}

void General::setStartHujia(int hujia)
{
    this->start_hujia = hujia;
}

int General::getStartHujia() const
{
    return start_hujia;
}

void General::addSkill(Skill *skill)
{
    if (skill) {
		if(skillname_list.contains(skill->objectName())) return;
		skillname_list << skill->objectName();
		skill->setParent(this);
		if(skill->getWakedSkills().isEmpty()) return;
		related_skills << skill->getWakedSkills().split(",");
    }else
        QMessageBox::warning(nullptr, "", tr("Invalid skill added to general %1").arg(objectName()));
}

void General::addSkill(const QString &skill_name)
{
    if(skillname_list.contains(skill_name)) return;
	skillname_list << skill_name;
	extra_set.insert(skill_name);
}

bool General::hasSkill(const QString &skill_name, bool related) const
{
	return skillname_list.contains(skill_name)
		||(related&&related_skills.contains(skill_name));
}

QList<const Skill *> General::getSkillList() const
{
    QList<const Skill *> skills;
    foreach (QString skill_name, skillname_list) {
        if (skill_name == "mashu" && ServerInfo.DuringGame && ServerInfo.GameMode == "02_1v1"
			&& ServerInfo.GameRuleMode != "Classical") skill_name = "xiaoxi";
        const Skill *skill = Sanguosha->getSkill(skill_name);
		if(skill&&!skills.contains(skill)){
			skills << skill;
			if(skill->isVisible()){
				foreach (const Skill *related_skill, Sanguosha->getRelatedSkills(skill_name)) {
					if(skills.contains(related_skill)) continue;
					skills << related_skill;
				}
			}
		}
    }
    return skills;
}

QList<const Skill *> General::getVisibleSkillList() const
{
    QList<const Skill *> skills;
    foreach (const Skill *skill, getSkillList()) {
        if (skill->isVisible()) skills << skill;
    }
    return skills;
}

QSet<const Skill *> General::getVisibleSkills() const
{
    QList<const Skill *> skills = getVisibleSkillList();
    return QSet<const Skill *>(skills.begin(), skills.end());
}

QSet<const TriggerSkill *> General::getTriggerSkills() const
{
    QSet<const TriggerSkill *> skills;
    foreach (QString skill_name, skillname_list) {
        const TriggerSkill *skill = Sanguosha->getTriggerSkill(skill_name);
        if (skill) skills << skill;
    }
    return skills;
}

void General::addRelateSkill(const QString &skill_name)
{
    if(related_skills.contains(skill_name)) return;
    related_skills << skill_name;
}

QStringList General::getRelatedSkillNames() const
{
    return related_skills;
}

QString General::getPackage() const
{
    QObject *p = parent();
    if (p) return p->objectName();
    return ""; // avoid null pointer exception;
}

static QHash<QString, QString> skill_strs;
static QHash<QString, QString> general_strs;

QString General::getSkillDescription(bool include_name) const
{
    QString description = skill_strs.value(objectName(),"");
	if(description.isEmpty()){
		QStringList relateds = getRelatedSkillNames();
		foreach (const Skill *skill, getVisibleSkillList()) {
			description += QString("<b>%1</b>：%2<br/><br/>").arg(Sanguosha->translate(skill->objectName())).arg(skill->getDescription());
			if(skill->getWakedSkills().isEmpty()) continue;
			foreach (QString sk, skill->getWakedSkills().split(",")) {
				if (relateds.contains(sk)) continue;
				relateds << sk;
			}
		}
	
		foreach (QString skill_name, relateds) {
			if(hasSkill(skill_name)) continue;
			const Skill *skill = Sanguosha->getSkill(skill_name);
			if (skill && skill->isVisible())
				description += QString("<font color=\"#01A5AF\"><b>%1</b>：%2</font><br/><br/>").arg(Sanguosha->translate(skill_name)).arg(skill->getDescription());
		}
		skill_strs[objectName()] = description;
	}

    if (include_name) {
		QString name = general_strs.value(objectName(),"");
		if(name.isEmpty()){
			QStringList kins = kingdom.split("+");
			QString str = Sanguosha->getKingdomColor(kins.first()).name();
			foreach (QString kin, kins)
				name.append(QString("<img src='image/kingdom/icon/%1.png'/>").arg(kin));
			name.append(QString("     <font color=%1><b>%2</b></font>     ").arg(str).arg(Sanguosha->translate(objectName())));
	
			QString gender("  <img src='image/gender/%1.png' height=17/>  ");
			if (isMale()) name.append(gender.arg("male"));
			else if (isFemale()) name.append(gender.arg("female"));
			else if (isNeuter()) name.append(gender.arg("neuter"));
			else if (isSexless()) name.append(gender.arg("sexless"));
	
			int start_hp = getStartHp();
			for (int i = 0; i < start_hp; i++)
				name.append("<img src='image/system/magatamas/5.png' height=12/>");
			for (int i = 0; i < max_hp - start_hp; i++)
				name.append("<img src='image/system/magatamas/0.png' height=12/>");
			for (int i = 0; i < getStartHujia(); i++)
				name.append("<img src='image/mark/@HuJia.png' height=17/>");
	
			name.append("<br/><br/>");
	
			str = Sanguosha->translate("information:" + objectName());
			if (str.contains("information:")&&objectName().contains("_"))
				str = Sanguosha->translate("information:" + objectName().split("_").last());
			if (!str.contains("information:"))
				name.append(QString("<font color=\"#045b58\">%1</font><br/><br/>").arg(str));
			general_strs[objectName()] = name;
		}
        description.prepend(name);
    }
	description.replace("\n", "<br/>");
    return description;
}

QString General::getBriefName() const
{
    static QMap<QString, QString> BriefNames;
	if(BriefNames.contains(objectName())) return BriefNames[objectName()];
	QString name = Sanguosha->translate("&" + objectName());
    if (name.startsWith("&")){
		name = Sanguosha->translate(objectName());
		name = name.split("[").first().split("-").first().split("·").first();
	}
	name.remove("&");
	BriefNames[objectName()] = name;
    return name;
}

QString General::getOracleText() const
{
    QString oracle = Sanguosha->translate("oracle:" + objectName());
    if (oracle.startsWith("oracle:"))
        oracle.clear();
    return oracle;
}

void General::lastWord() const
{
    int skin = Config.value("HeroSkin/"+objectName(), 0).toInt();
    QString actualGn = Sanguosha->getResourceAlias("heroskin", objectName());
	if (skin>0&&Sanguosha->playAudioEffect(QString("image/heroskin/audio/%1_%2/death/%1.ogg").arg(actualGn).arg(skin)))
		return;

    if (Sanguosha->playAudioEffect(QString("audio/death/%1.ogg").arg(objectName())))
        return;

    QStringList origins = objectName().split("_");
    if (origins.length()>1&&Sanguosha->getGeneral(origins.last())) {
		skin = Config.value("HeroSkin/"+origins.last(), 0).toInt();
		QString actualOrigin = Sanguosha->getResourceAlias("heroskin", origins.last());
		if (skin>0&&Sanguosha->playAudioEffect(QString("image/heroskin/audio/%1_%2/death/%1.ogg").arg(actualOrigin).arg(skin)))
			return;
		Sanguosha->playAudioEffect(QString("audio/death/%1.ogg").arg(origins.last()));
    }
}

bool General::hasHideSkill() const
{
    foreach (const Skill *skill, getSkillList()) {
        if (skill->isHideSkill())
            return true;
    }
    return false;
}

QString General::getSubPackage() const
{
    return sub_package;
}

void General::setAudioType(const QString &filename, const QString &types)
{
    Sanguosha->setAudioType(objectName(),filename,types);
}

void General::setImage(const QString &general_name)
{
    this->image = general_name;
}

QString General::getImage() const
{
    return image;
}

