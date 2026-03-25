#include "engine.h"
#include "client.h"
//#include "ai.h"
#include "settings.h"
//#include "scenario.h"
#include "lua.hpp"
#include "banpair.h"
#include <QMutexLocker>
//#include "protocol.h"
#include "lua-wrapper.h"
//#include "room-state.h"
#include "clientstruct.h"
#include "exppattern.h"
#include "wrapped-card.h"
#include "room.h"
#include "miniscenarios.h"

#include "guandu-scenario.h"
#include "couple-scenario.h"
#include "boss-mode-scenario.h"
#include "zombie-scenario.h"
#include "fancheng-scenario.h"
#include "challengedeveloper-scenario.h"


Engine*Sanguosha = nullptr;

// --- SafeLuaMutex implementation ---
void SafeLuaMutex::lock() {
    if (m_owner == QThread::currentThread()) { m_count++; return; }
    m_mutex.lock();
    m_owner = QThread::currentThread();
    m_count = 1;
}
void SafeLuaMutex::unlock() {
    if (m_owner == QThread::currentThread()) {
        if (--m_count == 0) { m_owner = nullptr; m_mutex.unlock(); }
    }
}
bool SafeLuaMutex::tryLock(int timeout) {
    if (m_owner == QThread::currentThread()) { m_count++; return true; }
    if (m_mutex.tryLock(timeout)) {
        m_owner = QThread::currentThread();
        m_count = 1;
        return true;
    }
    return false;
}
int SafeLuaMutex::yield() {
    if (m_owner != QThread::currentThread()) return 0;
    int saved_count = m_count;
    m_count = 0;
    m_owner = nullptr;
    m_mutex.unlock();
    return saved_count;
}
void SafeLuaMutex::restore(int count) {
    if (count <= 0) return;
    m_mutex.lock();
    m_owner = QThread::currentThread();
    m_count = count;
}

int Engine::getMiniSceneCounts()
{
    return m_miniScenes.size();
}

void Engine::_loadMiniScenarios()
{
    static bool loaded = false;
    if (loaded) return;
    for (int i = 1;; i++){
        if (QFile::exists(QString("etc/customScenes/%1.txt").arg(i)))
			m_miniScenes[QString(MiniScene::S_KEY_MINISCENE).arg(i)] = new LoadedScenario(QString::number(i));
		else
            break;
    }
    loaded = true;
}

void Engine::_loadModScenarios()
{
    addScenario(new GuanduScenario());
    addScenario(new CoupleScenario());
    addScenario(new FanchengScenario());
    addScenario(new ZombieScenario());
    addScenario(new ImpasseScenario());
    addScenario(new ChallengeDeveloperScenario());
}

void Engine::addPackage(const QString &name)
{
    // 防止重複實例化
    if (findChild<const Package*>(name)) {
        return; 
    }

    // 從 Hash Map 取得工廠函數
    PackageFactory factory = PackageAdder::packages().value(name, nullptr);
    if (factory) {
        Package *pack = factory(); // 真正執行 new 的地方
        addPackage(pack);
    }
    else {
        qWarning("Package %s cannot be loaded!", qPrintable(name));
    }
}

struct ManualSkill
{
    ManualSkill(const Skill*skill)
	: skill(skill), baseName(skill->objectName().split("_").last())
    {
        static const QString prefixes[] = { "boss", "gd", "jg", "jsp", "kof", "neo", "nos", "ol", "sp",
			"tw", "vs", "yt", "diy", "new", "tenyear", "mobile", "second", "third" };

        for (int i = 0; i < 18; ++i) {//sizeof(prefixes) / sizeof(QString)
            QString prefix = prefixes[i];
            if (baseName.startsWith(prefix))
                baseName.remove(0, prefix.length());
        }

        QTextCodec*codec = QTextCodec::codecForName("GBK");
        translatedBytes = codec->fromUnicode(Sanguosha->translate(skill->objectName()));

        printf("%s:%d", skill->objectName().toLocal8Bit().constData(), translatedBytes.length());
    }
    const Skill*skill;
    QString baseName;
    QByteArray translatedBytes;
    QList<const General*> relatedGenerals;
};

static bool nameLessThan(const ManualSkill*skill1, const ManualSkill*skill2)
{
    return skill1->baseName < skill2->baseName;
}

static bool translatedNameLessThan(const ManualSkill*skill1, const ManualSkill*skill2)
{
    return skill1->translatedBytes < skill2->translatedBytes;
}

class ManualSkillList
{
public:
    ManualSkillList()
    {
    }

    ~ManualSkillList()
    {
        foreach (ManualSkill*manualSkill, m_skills)
            delete manualSkill;
    }

    void insert(const Skill*skill, const General*owner)
    {
        bool exist = false;
        foreach (ManualSkill*manualSkill, m_skills) {
            if (skill == manualSkill->skill) {
                manualSkill->relatedGenerals << owner;
                exist = true;
            }
        }

        if (!exist) {
            ManualSkill*manualSkill = new ManualSkill(skill);
            manualSkill->relatedGenerals << owner;
            m_skills << manualSkill;
        }
    }

    void insert(QList<const Skill*>sks, const General*owner) {
        foreach (const Skill*s, sks)
            insert(s, owner);
    }

    void insert(ManualSkill*skill)
    {
        m_skills << skill;
    }

    void clear()
    {
        m_skills.clear();
    }

    bool isEmpty() const
    {
        return m_skills.isEmpty();
    }

    void sortByName()
    {
        std::sort(m_skills.begin(), m_skills.end(), nameLessThan);
    }

    void sortByTranslatedName(QList<ManualSkill*>::iterator begin, QList<ManualSkill*>::iterator end)
    {
        std::sort(begin, end, translatedNameLessThan);
    }

    QList<ManualSkill*>::iterator begin()
    {
        return m_skills.begin();
    }

    QList<ManualSkill*>::iterator end()
    {
        return m_skills.end();
    }

    QString join(const QString &sep)
    {
        QStringList baseNames;
        foreach (ManualSkill*skill, m_skills)
            baseNames << Sanguosha->translate(skill->skill->objectName());

        return baseNames.join(sep);
    }

private:
    QList<ManualSkill*> m_skills;
};

Engine::Engine(bool isManualMode)
{
#ifdef LOGNETWORK
	logFile.setFileName("netmsg.log");
	logFile.open(QIODevice::WriteOnly|QIODevice::Text);
    connect(this, SIGNAL(logNetworkMessage(QString)), this, SLOT(handleNetworkMessage(QString)),Qt::QueuedConnection);
#endif // LOGNETWORK

    Sanguosha = this;

    lua = CreateLuaState();
    if (!DoLuaScript(lua, "lua/config.lua")) exit(1);

    /*foreach (QString cv_pair, GetConfigFromLuaState(lua, "convert_pairs").toStringList()) {
        QStringList pairs = cv_pair.split("->");
        foreach (QString to, pairs[1].split("|"))
            sp_convert_pairs.insertMulti(pairs[0], to);
    }*/

    extra_hidden_generals = GetConfigFromLuaState(lua, "extra_hidden_generals").toStringList();
    removed_hidden_generals = GetConfigFromLuaState(lua, "removed_hidden_generals").toStringList();
    extra_default_lords = GetConfigFromLuaState(lua, "extra_default_lords").toStringList();
    removed_default_lords = GetConfigFromLuaState(lua, "removed_default_lords").toStringList();

    foreach (QString name, GetConfigFromLuaState(lua, "package_names").toStringList())
        addPackage(name);

    _loadMiniScenarios();
    _loadModScenarios();
    m_customScene = new CustomScenario;

    connect(qApp, SIGNAL(aboutToQuit()), this, SLOT(deleteLater()));

    if (!DoLuaScript(lua, "lua/sanguosha.lua")) exit(1);

#ifdef ANDROID
	foreach (Skill*skill, findChildren<Skill*>()) {
		if(skill->isVisible()) skill->initMediaSource();
	}
#endif // ANDROID

    if (isManualMode) {
        ManualSkillList allSkills;
        foreach (const General*general, getAllGenerals()) {
            allSkills.insert(general->getVisibleSkillList(), general);

            foreach (const QString &skillName, general->getRelatedSkillNames()) {
                const Skill*skill = getSkill(skillName);
                if (skill != nullptr && skill->isVisible())
                    allSkills.insert(skill, general);
            }
        }

        allSkills.sortByName();

        QList<ManualSkill*>::iterator j = allSkills.begin();
        QList<ManualSkill*>::iterator i = j;
        for (char c = 'a'; c <= 'z'; ++c) {
            while (j != allSkills.end()) {
                if ((*j)->baseName.startsWith(c))
                    ++j;
                else
                    break;
            }
            if (j - i > 1)
                allSkills.sortByTranslatedName(i, j);
            i = j;
        }

        static QDir dir("manual");
        if (!dir.exists())
            QDir::current().mkdir("manual");

        QList<ManualSkill*>::iterator iter = allSkills.begin();
        for (char c = 'a'; c <= 'z'; ++c) {
            QChar upper = QChar(c).toUpper();
            QFile file(QString("manual/Chapter%1.lua").arg(upper));
            if (file.open(QFile::WriteOnly | QFile::Truncate)) {
                QTextStream stream(&file);
                stream.setCodec(QTextCodec::codecForName("UTF-8"));

                ManualSkillList list;
                while (iter != allSkills.end()) {
                    if ((*iter)->baseName.startsWith(c)) {
                        list.insert(*iter);
                        ++iter;
                    } else
                        break;
                }

                QString info;
                if (list.isEmpty()) info = translate("Manual_Empty");
                else info = translate("Manual_Index") + list.join(" ");

                stream << translate("Manual_Head").arg(upper).arg(info).arg(getVersion())
						<< endl;

                for (QList<ManualSkill*>::iterator it = list.begin(); it < list.end(); ++it) {
                    ManualSkill*skill =*it;
                    QStringList generals;

                    foreach(const General*general, skill->relatedGenerals) {
                        generals << QString("%1-%2").arg(translate(general->getPackage())).arg(general->getBriefName());
                    }
                    stream << translate("Manual_Skill").arg(translate(skill->skill->objectName())).arg(generals.join(" ")).arg(skill->skill->getDescription())
                           << endl << endl;
                }

                list.clear();
                file.close();
            }
        }
        return;
    }
	Config.setValue("AutoSkillTypeColorReplacement", true);
	Config.setValue("AutoSuitReplacement", true);

    // available game modes
    modes["02p"] = tr("2 players");
    //modes["02pbb"] = tr("2 players (using blance beam)");
    modes["02_1v1"] = tr("2 players (KOF style)");
    modes["03p"] = tr("3 players");
    modes["03_1v2"] = tr("3 players (Dou Di Zhu)");
    modes["04p"] = tr("4 players");
    modes["04_1v3"] = tr("4 players (Hulao Pass)");
    modes["04_boss"] = tr("4 players(Boss)");
    modes["04_2v2"] = tr("4 players (Happy)");
    modes["05p"] = tr("5 players");
	modes["05_ol"] = "5 人局 [诸侯伐董]";
	modes["06_ol"] = "6 人局 [神武在世]";
    modes["06p"] = tr("6 players");
    modes["06pd"] = tr("6 players (2 renegades)");
    modes["06_3v3"] = tr("6 players (3v3)");
    modes["06_XMode"] = tr("6 players (XMode)");
    modes["07p"] = tr("7 players");
    modes["08p"] = tr("8 players");
    modes["08pd"] = tr("8 players (2 renegades)");
    modes["08pz"] = tr("8 players (0 renegade)");
    modes["08_defense"] = tr("8 players (JianGe Defense)");
    modes["09p"] = tr("9 players");
    modes["10pd"] = tr("10 players");
    modes["10p"] = tr("10 players (1 renegade)");
    modes["10pz"] = tr("10 players (0 renegade)");
    modes["20p"] = tr("20 players (1 renegade)");

	ZhinangCards << "ExNihilo" << "Dismantlement" << "Nullification" << "Qizhengxiangsheng"
			<< "Mantianguohai" << "Tiaojiyanmei" << "Binglinchengxia";//添加初始智囊牌名
	available_generals = generals;
    foreach (Card*c, cards) {
		if(patterns.contains(c->objectName())||c->getTypeId()>2) continue;
		patterns[c->objectName()] = new ExpPattern(c->getClassName());
	}
}

lua_State*Engine::getLuaState() const
{
    return lua;
}

SafeLuaMutex &Engine::getLuaMutex() const
{
    return lua_mutex;
}

void Engine::addTranslationEntry(const QString &key, const QString &value)
{
    if (!translations.contains(key))
		engine_translations.insert(key, value);
	translations.insert(key, value);
}

void Engine::addModes(const QString &key, const QString &value, const QString &roles)
{
    modes[key] = value;
	if (!roles.isEmpty())
		mode_roles[key] = roles;
}

Engine::~Engine()
{
    delete m_customScene;
#ifdef AUDIO_SUPPORT
    Audio::quit();
#endif
    lua_close(lua);
}

QStringList Engine::getModScenarioNames() const
{
    return m_scenarios.keys();
}

void Engine::addScenario(Scenario*scenario)
{
    m_scenarios[scenario->objectName()] = scenario;
    addPackage(scenario);
}

const Scenario*Engine::getScenario(const QString &name) const
{
	if (m_scenarios.contains(name))
		return m_scenarios[name];
	else if (m_miniScenes.contains(name))
		return m_miniScenes[name];
	else if (name == "custom_scenario")
		return m_customScene;
	return nullptr;
}

void Engine::addSkills(QList<const Skill*> all_skills)
{
    foreach (const Skill* skill, all_skills) {
        if (skill) {
            if (skills.contains(skill->objectName()))
                QMessageBox::warning(nullptr, "", tr("Duplicated skill : %1").arg(skill->objectName()));
            
            // [修改點]
            // QPointer<Skill> 只能接收 Skill* (非 const)，因為它需要連接 destroyed 信號。
            // 這裡使用 const_cast 是安全的，因為 Skill 對象本身是 new 出來的，不是唯讀內存。
            Skill *mutableSkill = const_cast<Skill*>(skill);

            // 存入 QPointer 容器 (mutableSkill 會自動隱式轉換為 QPointer<Skill>)
            skills.insert(skill->objectName(), mutableSkill);
        } else {
            QMessageBox::warning(nullptr, "", tr("The engine tries to add an invalid skill"));
        }
    }
}
QList<const ProhibitSkill*> Engine::getProhibitSkills() const
{
    static QList<const ProhibitSkill*> prohibitSkills;
    if (prohibitSkills.isEmpty()) {
        foreach (const Skill* skill, getSafeSkills()) {
            if (skill->inherits("ProhibitSkill"))
                prohibitSkills << qobject_cast<const ProhibitSkill*>(skill);
        }
    }
    return prohibitSkills;
}

// 7. 修復 getDistanceSkills
QList<const DistanceSkill*> Engine::getDistanceSkills() const
{
    static QList<const DistanceSkill*> distanceSkills;
    if (distanceSkills.isEmpty()) {
        foreach (const Skill* skill, getSafeSkills()) {
            if (skill->inherits("DistanceSkill"))
                distanceSkills << qobject_cast<const DistanceSkill*>(skill);
        }
    }
    return distanceSkills;
}

// 8. 修復 getMaxCardsSkills
QList<const MaxCardsSkill*> Engine::getMaxCardsSkills() const
{
    static QList<const MaxCardsSkill*> maxcardsSkills;
    if (maxcardsSkills.isEmpty()) {
        foreach (const Skill* skill, getSafeSkills()) {
            if (skill->inherits("MaxCardsSkill"))
                maxcardsSkills << qobject_cast<const MaxCardsSkill*>(skill);
        }
    }
    return maxcardsSkills;
}

// 9. 修復 getTargetModSkills
QList<const TargetModSkill*> Engine::getTargetModSkills() const
{
    static QList<const TargetModSkill*> targetmodSkills;
    if (targetmodSkills.isEmpty()) {
        foreach (const Skill* skill, getSafeSkills()) {
            if (skill->inherits("TargetModSkill"))
                targetmodSkills << qobject_cast<const TargetModSkill*>(skill);
        }
    }
    return targetmodSkills;
}

QList<const InvaliditySkill*> Engine::getInvaliditySkills() const
{
    // 建議：去掉 static 緩存。
    // 如果技能系統支持動態加載/卸載，static 緩存會導致存儲了過期的指針，再次引發崩潰。
    // 除非你確定技能加載後永遠不會變。
    static QList<const InvaliditySkill*> invaliditySkills;

    if (invaliditySkills.isEmpty()) {
        // 假設 skills 是 QHash<QString, QPointer<Skill>>，我們遍歷它的值
        foreach (const QPointer<Skill> &skillPtr, skills.values()) {
            
            // [修復 1] 必須檢查指針是否有效
            if (skillPtr.isNull()) 
                continue;

            // [修復 2] 獲取裸指針
            const Skill* skill = skillPtr.data();

            // 這樣調用 inherits 才是安全的
            if (skill->inherits("InvaliditySkill")) {
                invaliditySkills << qobject_cast<const InvaliditySkill*>(skill);
            }
        }
    }
    return invaliditySkills;
}

QList<const TriggerSkill*> Engine::getGlobalTriggerSkills() const
{
    static QList<const TriggerSkill*> globalTriggerSkills;
    // 注意：static 緩存有風險，如果動態加載技能，建議去掉 if(isEmpty)
    if (globalTriggerSkills.isEmpty()) {
        // [核心修改] 使用 getSafeSkills() 替代直接遍歷 skills
        foreach (const Skill* skill, getSafeSkills()) {
            // 安全轉型：getSafeSkills 保證了 skill 不為 nullptr
            const TriggerSkill* ts = qobject_cast<const TriggerSkill*>(skill);
            if (ts && ts->isGlobal())
                globalTriggerSkills << ts;
        }
    }
    return globalTriggerSkills;
}

// 2. 修復 getAttackRangeSkills
QList<const AttackRangeSkill*> Engine::getAttackRangeSkills() const
{
    static QList<const AttackRangeSkill*> attackRangeSkills;
    if (attackRangeSkills.isEmpty()) {
        foreach (const Skill* skill, getSafeSkills()) {
            if (skill->inherits("AttackRangeSkill"))
                attackRangeSkills << qobject_cast<const AttackRangeSkill*>(skill);
        }
    }
    return attackRangeSkills;
}

// 3. 修復 getViewAsEquipSkills
QList<const ViewAsEquipSkill*> Engine::getViewAsEquipSkills() const
{
    static QList<const ViewAsEquipSkill*> viewAsEquipSkills;
    if (viewAsEquipSkills.isEmpty()) {
        foreach (const Skill* skill, getSafeSkills()) {
            if (skill->inherits("ViewAsEquipSkill"))
                viewAsEquipSkills << qobject_cast<const ViewAsEquipSkill*>(skill);
        }
    }
    return viewAsEquipSkills;
}

// 4. 修復 getCardLimitSkills
QList<const CardLimitSkill*> Engine::getCardLimitSkills() const
{
    static QList<const CardLimitSkill*> cardLimitSkills;
    if (cardLimitSkills.isEmpty()) {
        foreach (const Skill* skill, getSafeSkills()) {
            if (skill->inherits("CardLimitSkill"))
                cardLimitSkills << qobject_cast<const CardLimitSkill*>(skill);
        }
    }
    return cardLimitSkills;
}

// 5. 修復 getProhibitPindianSkills
QList<const ProhibitPindianSkill*> Engine::getProhibitPindianSkills() const
{
    static QList<const ProhibitPindianSkill*> prohibitPindiankills;
    if (prohibitPindiankills.isEmpty()) {
        foreach (const Skill* skill, getSafeSkills()) {
            if (skill->inherits("ProhibitPindianSkill"))
                prohibitPindiankills << qobject_cast<const ProhibitPindianSkill*>(skill);
        }
    }
    return prohibitPindiankills;
}

void Engine::addPackage(Package*package)
{
    if (findChild<const Package*>(package->objectName()))
        return;

    package->setParent(this);
    //sp_convert_pairs.unite(package->getConvertPairs());
    patterns.unite(package->getPatterns());
    related_skills.unite(package->getRelatedSkills());

    foreach (Card*card, package->findChildren<Card*>()) {
        card->setId(cards.length());
        cards << card;
		if(name2cards.contains(card->objectName())) continue;
		name2cards.insert(card->objectName(), card);
		if(card->objectName()=="slash") name2cards.insert("normal_slash", card);
		if(name2cards.contains(card->getClassName())) continue;
		name2cards.insert(card->getClassName(), card);/*
        if (card->inherits("LuaBasicCard")) {
            if(luaBasicCards.contains(card->objectName())) continue;
            luaBasicCards.insert(card->getClassName(), card);
            luaBasicCards.insert(card->objectName(), card);
        } else if (card->inherits("LuaTrickCard")) {
            if(luaTrickCards.contains(card->objectName())) continue;
            luaTrickCards.insert(card->getClassName(), card);
            luaTrickCards.insert(card->objectName(), card);
        } else if (card->inherits("LuaWeapon")) {
            if(luaWeapons.contains(card->objectName())) continue;
            luaWeapons.insert(card->getClassName(), card);
            luaWeapons.insert(card->objectName(), card);
        } else if (card->inherits("LuaArmor")) {
            if(luaArmors.contains(card->objectName())) continue;
            luaArmors.insert(card->getClassName(), card);
            luaArmors.insert(card->objectName(), card);
        } else if (card->inherits("LuaOffensiveHorse")) {
            if(LuaOffensiveHorses.contains(card->objectName())) continue;
            LuaOffensiveHorses.insert(card->getClassName(), card);
            LuaOffensiveHorses.insert(card->objectName(), card);
        } else if (card->inherits("LuaDefensiveHorse")) {
            if(LuaDefensiveHorses.contains(card->objectName())) continue;
            LuaDefensiveHorses.insert(card->getClassName(), card);
            LuaDefensiveHorses.insert(card->objectName(), card);
        } else if (card->inherits("LuaTreasure")) {
            if(luaTreasures.contains(card->objectName())) continue;
			luaTreasures.insert(card->getClassName(), card);
            luaTreasures.insert(card->objectName(), card);
        } else if(!metaobjects.contains(card->objectName())){
            const QMetaObject*meta = card->metaObject();
			metaobjects.insert(meta->className(), meta);
			metaobjects.insert(card->objectName(), meta);
			className2objectName.insert(meta->className(), card->objectName());
        }*/
    }

	QList<const Skill*> sks = package->getSkills();
	sks << package->findChildren<const Skill*>();
    foreach (const Skill*skill, sks) {
		if(skill->getWakedSkills().isEmpty()) continue;
        foreach (QString sk_name, skill->getWakedSkills().split(",")) {
            if (sk_name.startsWith("#"))
				related_skills.insertMulti(skill->objectName(), sk_name);
        }
    }
	addSkills(sks);

    foreach (General*general, package->findChildren<General*>()) {
        foreach (QString skill_name, general->getExtraSkillSet()) {
            if (skill_name.startsWith("#")) continue;
			foreach(QString name, related_skills.values(skill_name)){
				if (name.startsWith("#")) general->addSkill(name);
			}
        }
        generals.insert(general->objectName(), general);
    }

    foreach(const QMetaObject*meta, package->getMetaObjects()){
		//name2cards.insert(meta->className(),(const Card*)meta->newInstance());
		metaobjects.insert(meta->className(), meta);
	}
}

void Engine::addBanPackage(const QString &package_name)
{
    ban_package << package_name;
}

QStringList Engine::getBanPackages() const
{
    if (qApp->arguments().contains("-server")) return Config.BanPackages;
    return ban_package;//+Config.BanPackages;
}

QList<const Package*> Engine::getPackages() const
{
    return findChildren<const Package*>();
}

Package*Engine::getPackage(const QString &package_name)
{
	return findChild<Package*>(package_name);
}

void Engine::setPackage(Package*package)
{
    //package->setParent(this);
    //patterns.unite(package->getPatterns());
    related_skills.unite(package->getRelatedSkills());
    //sp_convert_pairs.unite(package->getConvertPairs());

    foreach (Card*card, package->findChildren<Card*>()) {
        if (card->getId()>=0||cards.contains(card)) continue;
		card->setId(cards.length());
        cards << card;
		if(name2cards.contains(card->objectName())) continue;
		name2cards.insert(card->objectName(), card);
		if(name2cards.contains(card->getClassName())) continue;
		name2cards.insert(card->getClassName(), card);/*
        if (card->inherits("LuaBasicCard")) {
            if(luaBasicCards.contains(card->objectName())) continue;
            luaBasicCards.insert(card->getClassName(), card);
            luaBasicCards.insert(card->objectName(), card);
        } else if (card->inherits("LuaTrickCard")) {
            if(luaTrickCards.contains(card->objectName())) continue;
            luaTrickCards.insert(card->getClassName(), card);
            luaTrickCards.insert(card->objectName(), card);
        } else if (card->inherits("LuaWeapon")) {
            if(luaWeapons.contains(card->objectName())) continue;
            luaWeapons.insert(card->getClassName(), card);
            luaWeapons.insert(card->objectName(), card);
        } else if (card->inherits("LuaArmor")) {
            if(luaArmors.contains(card->objectName())) continue;
            luaArmors.insert(card->getClassName(), card);
            luaArmors.insert(card->objectName(), card);
        } else if (card->inherits("LuaOffensiveHorse")) {
            if(LuaOffensiveHorses.contains(card->objectName())) continue;
            LuaOffensiveHorses.insert(card->getClassName(), card);
            LuaOffensiveHorses.insert(card->objectName(), card);
        } else if (card->inherits("LuaDefensiveHorse")) {
            if(LuaDefensiveHorses.contains(card->objectName())) continue;
            LuaDefensiveHorses.insert(card->getClassName(), card);
            LuaDefensiveHorses.insert(card->objectName(), card);
        } else if (card->inherits("LuaTreasure")) {
            if(luaTreasures.contains(card->objectName())) continue;
			luaTreasures.insert(card->getClassName(), card);
            luaTreasures.insert(card->objectName(), card);
        } else if(!metaobjects.contains(card->objectName())){
            const QMetaObject*meta = card->metaObject();
			metaobjects.insert(meta->className(), meta);
			metaobjects.insert(card->objectName(), meta);
			className2objectName.insert(meta->className(), card->objectName());
        }*/
    }

    foreach (const Skill* skill, package->getSkills() + package->findChildren<const Skill*>()) {
        if (skills.contains(skill->objectName())) continue;

        // [修復點] 使用 const_cast 將 const Skill* 轉為 Skill*
        // 這是 QPointer 正常工作所必須的
        Skill *mutableSkill = const_cast<Skill*>(skill);
        skills.insert(skill->objectName(), mutableSkill);

        if (skill->getWakedSkills().isEmpty()) continue;
        foreach (QString sk_name, skill->getWakedSkills().split(",")) {
            if (sk_name.startsWith("#"))
                related_skills.insertMulti(skill->objectName(), sk_name);
        }
    }

    foreach (General*general, package->findChildren<General*>()) {
        if (generals.contains(general->objectName())) continue;
        foreach (QString skill_name, general->getExtraSkillSet()) {
            if (skill_name.startsWith("#")) continue;
			foreach(QString name, related_skills.values(skill_name)){
				if (name.startsWith("#")) general->addSkill(name);
			}
        }
        generals.insert(general->objectName(), general);
    }

    foreach(const QMetaObject*meta, package->getMetaObjects()){
		if(metaobjects.contains(meta->className())) continue;
		//name2cards.insert(meta->className(),(const Card*)meta->newInstance());
		metaobjects.insert(meta->className(), meta);
	}
}

QStringList Engine::getZhinangCards() const
{
    return ZhinangCards;
}

void Engine::setZhinangCard(const QString &flag)
{
    if (flag.startsWith("-")){
		ZhinangCards.removeOne(flag.mid(1));
	}else if (!ZhinangCards.contains(flag))
		ZhinangCards << flag;
}

QString Engine::translate(const QString &to_translate, bool initial) const
{
    if(to_translate.isEmpty()) return "";
	if(to_translate.contains("\\")){
		QString res;
		foreach(QString str, to_translate.split("\\"))
			res += (initial?engine_translations:translations).value(str, str);
		return res;
	}
	return (initial?engine_translations:translations).value(to_translate, to_translate);
}

int Engine::getRoleIndex() const
{
	if (ServerInfo.GameMode == "06_3v3" || ServerInfo.GameMode == "06_XMode")
		return 4;
	else if (ServerInfo.EnableHegemony)
		return 5;
	return 1;
}

const CardPattern*Engine::getPattern(const QString &name, bool extra) const
{
    const CardPattern*ptn = patterns.value(name, nullptr);
    if (ptn) return ptn;
	else if(extra){
		ExpPattern*expptn = new ExpPattern(name);
		patterns.insert(name, expptn);
		return expptn;
	}
	return nullptr;
}

bool Engine::matchPattern(const QString &pattern, const Player*player, const Card*card) const
{
    const CardPattern*ptn = getPattern(pattern, true);
    if (ptn) return ptn->match(player, card);
	return false;
}

bool Engine::matchExpPattern(const QString &pattern, const Player*player, const Card*card) const
{
    ExpPattern*ptn = exp_patterns.value(pattern, nullptr);
    if (ptn==nullptr) {
		ptn = new ExpPattern(pattern);
		exp_patterns.insert(pattern, ptn);
	}
	return ptn->match(player, card);
	/*
	ExpPattern p(pattern);
    return p.match(player, card);*/
}

Card::HandlingMethod Engine::getCardHandlingMethod(const QString &method_name) const
{
    if (method_name == "use")
        return Card::MethodUse;
    else if (method_name == "response")
        return Card::MethodResponse;
    else if (method_name == "discard")
        return Card::MethodDiscard;
    else if (method_name == "recast")
        return Card::MethodRecast;
    else if (method_name == "pindian")
        return Card::MethodPindian;
    else if (method_name == "ignore")
        return Card::MethodIgnore;
    else if (method_name == "effect")
        return Card::MethodEffect;
    else {
        Q_ASSERT(false);
        return Card::MethodNone;
    }
}

QList<const Skill*> Engine::getRelatedSkills(const QString &skill_name) const
{
    QList<const Skill*> relateds;
    foreach(QString name, related_skills.values(skill_name)){
		const Skill*sk = getSkill(name);
		if(sk) relateds << sk;
	}
    return relateds;
}

const Skill*Engine::getMainSkill(const QString &skill_name) const
{
	if(skill_name.startsWith("#")){
		foreach (QString key, related_skills.keys())
			if (related_skills.values(key).contains(skill_name))
				return getSkill(key);
	}
    return getSkill(skill_name);
}

const General*Engine::getGeneral(const QString &name) const
{
    return generals.value(name, nullptr);
}

int Engine::getGeneralCount(bool include_banned, const QString &kingdom) const
{
    int total = 0;
	QStringList banPackages = ServerInfo.BanPackages;
	if (ServerInfo.GameMode == "03_1v2")
		banPackages << Config.value("Banlist/Doudizhu").toStringList();
	else if (ServerInfo.GameMode == "04_2v2")
		banPackages << Config.value("Banlist/Happy2v2").toStringList();
	else if (ServerInfo.GameMode.contains("_mini_")||ServerInfo.GameMode=="custom_scenario"||isNormalGameMode(ServerInfo.GameMode))
		banPackages << Config.value("Banlist/Roles").toStringList();
	else if (ServerInfo.EnableBasara)
		banPackages << Config.value("Banlist/Basara").toStringList();
	else if (ServerInfo.EnableHegemony)
		banPackages << Config.value("Banlist/Hegemony").toStringList();
	if(include_banned)
		banPackages.clear();
	foreach (const General*general, generals) {
        if (banPackages.contains(general->getPackage())||banPackages.contains(general->objectName())||isGeneralHidden(general->objectName()))
            continue;
        if (ServerInfo.Enable2ndGeneral&&BanPair::isBanned(general->objectName()))
            continue;
		if (kingdom.isEmpty()||general->getKingdoms().contains(kingdom))
			total++;
    }

    // special case for neo standard package
    if (banPackages.contains("standard") && total<5) {
        if(kingdom.isEmpty())
			total += 4;
		else if (kingdom == "wei")
            ++total; // zhenji
        else if (kingdom == "shu")
            ++total; // zhugeliang
        else if (kingdom == "wu")
            total += 2; // suanquan && sunshangxiang
    }

    return total;
}

void Engine::registerRoom(QObject*room)
{
    m_mutex.lock();
    m_rooms[QThread::currentThread()] = room;
    m_mutex.unlock();
}

void Engine::unregisterRoom()
{
    m_mutex.lock();
    m_rooms.remove(QThread::currentThread());
    m_mutex.unlock();
}

QObject*Engine::currentRoomObject()
{
    QObject*room;
    m_mutex.lock();
    room = m_rooms[QThread::currentThread()];
    //Q_ASSERT(room);
    m_mutex.unlock();
    return room;
}

Room*Engine::currentRoom()
{
    QObject*roomObject = currentRoomObject();/*
    Room*room = qobject_cast<Room*>(roomObject);
    Q_ASSERT(room);
    return room;*/
	return qobject_cast<Room*>(roomObject);
}

RoomState*Engine::currentRoomState()
{
    QObject*roomObject = currentRoomObject();
    Room*room = qobject_cast<Room*>(roomObject);
    if (room) return room->getRoomState();
	Client*client = qobject_cast<Client*>(roomObject);
	//Q_ASSERT(client);
	return client->getRoomState();
}

const Player*Engine::getCardOwner(int card_id)
{
    QObject*roomObject = currentRoomObject();
    if (!roomObject) return nullptr;
    Room*room = qobject_cast<Room*>(roomObject);
    if (room) return room->getCardOwner(card_id);
	Client*client = qobject_cast<Client*>(roomObject);
	return client->getCardOwner(card_id);
}

Player::Place Engine::getCardPlace(int card_id)
{
    QObject*roomObject = currentRoomObject();
    if (!roomObject) return Player::PlaceUnknown;
    Room*room = qobject_cast<Room*>(roomObject);
    if (room) return room->getCardPlace(card_id);
	Client*client = qobject_cast<Client*>(roomObject);
	return client->getCardPlace(card_id);
}

QString Engine::getCurrentCardUsePattern()
{
    return currentRoomState()->getCurrentCardUsePattern();
}

CardUseStruct::CardUseReason Engine::getCurrentCardUseReason()
{
    return currentRoomState()->getCurrentCardUseReason();
}

QString Engine::findConvertFrom(const QString &general_name) const
{
    foreach (QString general, sp_convert_pairs.keys()) {
        if (sp_convert_pairs.values(general).contains(general_name))
            return general;
    }
    return "";
}

bool Engine::isGeneralHidden(const QString &general_name) const
{
    if(extra_hidden_generals.contains(general_name)) return true;
    if(removed_hidden_generals.contains(general_name)) return false;
	const General*general = getGeneral(general_name);
    return general&&general->isHidden();
}

WrappedCard*Engine::getWrappedCard(int cardId)
{
    //WrappedCard*wrappedCard = qobject_cast<WrappedCard*>(getCard(cardId));
    //Q_ASSERT(wrappedCard && wrappedCard->getId() == cardId);
    return qobject_cast<WrappedCard*>(getCard(cardId));//wrappedCard;
}

Card*Engine::getCard(int cardId, bool)
{
	/*QObject*room = currentRoomObject();
    Q_ASSERT(room || !need_Q_ASSERT);
    if (!room && !need_Q_ASSERT) return nullptr;
    Room*serverRoom = qobject_cast<Room*>(room);
    Card*card = nullptr;
    if (serverRoom != nullptr)
        card = serverRoom->getCard(cardId);
    else {
        Client*clientRoom = qobject_cast<Client*>(room);
        Q_ASSERT(clientRoom != nullptr || !need_Q_ASSERT);
        if (!need_Q_ASSERT && clientRoom == nullptr) return nullptr;
        card = clientRoom->getCard(cardId);
    }
    Q_ASSERT(card || !need_Q_ASSERT);
    if (!need_Q_ASSERT && !card) return nullptr;
    return card;*/
    QObject*room = currentRoomObject();
	if(!room) return nullptr;
    Card*card = nullptr;
	Room*serverRoom = qobject_cast<Room*>(room);
	if(serverRoom) card = serverRoom->getCard(cardId);
	if(card==nullptr){
		Client*clientRoom = qobject_cast<Client*>(room);
		if(clientRoom) card = clientRoom->getCard(cardId);
	}
	return card;//currentRoomState()->getCard(cardId);
}

const Card*Engine::getEngineCard(int cardId) const
{
	return cards.value(cardId,nullptr);
}

Card*Engine::cloneCard(const Card*card) const
{
    Card*result = cloneCard(card->objectName(), card->getSuit(), card->getNumber(), card->getFlags());
    if (result){
		result->setId(card->getEffectiveId());
		result->setSkillName(card->getSkillName(false));
	}
    return result;
}

Card*Engine::cloneCard(const QString &name, Card::Suit suit, int number, const QStringList &flags) const
{
    Card*card = nullptr;
	if(name2cards.contains(name)){
		const Card*lcard = name2cards.value(name);
		if(lcard->inherits("LuaBasicCard"))
			card = qobject_cast<const LuaBasicCard*>(lcard)->clone(suit,number);
		else if(lcard->inherits("LuaTrickCard"))
			card = qobject_cast<const LuaTrickCard*>(lcard)->clone(suit,number);
		else if(lcard->inherits("LuaWeapon"))
			card = qobject_cast<const LuaWeapon*>(lcard)->clone(suit,number);
		else if(lcard->inherits("LuaArmor"))
			card = qobject_cast<const LuaArmor*>(lcard)->clone(suit,number);
		else if(lcard->inherits("LuaOffensiveHorse"))
			card = qobject_cast<const LuaOffensiveHorse*>(lcard)->clone(suit,number);
		else if(lcard->inherits("LuaDefensiveHorse"))
			card = qobject_cast<const LuaDefensiveHorse*>(lcard)->clone(suit,number);
		else if(lcard->inherits("LuaTreasure"))
			card = qobject_cast<const LuaTreasure*>(lcard)->clone(suit,number);
		else
			card = qobject_cast<Card*>(lcard->metaObject()->newInstance(Q_ARG(Card::Suit,suit),Q_ARG(int,number)));
	}else if(metaobjects.contains(name))
		card = qobject_cast<Card*>(metaobjects.value(name)->newInstance(Q_ARG(Card::Suit, suit), Q_ARG(int, number)));
	/*if (metaobjects.contains(name)){
		QObject*card_obj = metaobjects.value(name)->newInstance(Q_ARG(Card::Suit, suit), Q_ARG(int, number));
		card_obj->setObjectName(className2objectName.value(name, name));
		card = qobject_cast<Card*>(card_obj);
	}else if (luaBasicCards.contains(name)) {
		const LuaBasicCard*lcard = qobject_cast<const LuaBasicCard*>(luaBasicCards.value(name));
		if (lcard) card = lcard->clone(suit, number);
	} else if (luaTrickCards.contains(name)) {
		const LuaTrickCard*lcard = qobject_cast<const LuaTrickCard*>(luaTrickCards.value(name));
		if (lcard) card = lcard->clone(suit, number);
	} else if (luaWeapons.contains(name)) {
		const LuaWeapon*lcard = qobject_cast<const LuaWeapon*>(luaWeapons.value(name));
		if (lcard) card = lcard->clone(suit, number);
	} else if (luaArmors.contains(name)) {
		const LuaArmor*lcard = qobject_cast<const LuaArmor*>(luaArmors.value(name));
		if (lcard) card = lcard->clone(suit, number);
	} else if (LuaOffensiveHorses.contains(name)) {
		const LuaOffensiveHorse*lcard = qobject_cast<const LuaOffensiveHorse*>(LuaOffensiveHorses.value(name));
		if (lcard) card = lcard->clone(suit, number);
	} else if (LuaDefensiveHorses.contains(name)) {
		const LuaDefensiveHorse*lcard = qobject_cast<const LuaDefensiveHorse*>(LuaDefensiveHorses.value(name));
		if (lcard) card = lcard->clone(suit, number);
	} else if (luaTreasures.contains(name)) {
		const LuaTreasure*lcard = qobject_cast<const LuaTreasure*>(luaTreasures.value(name));
		if (lcard) card = lcard->clone(suit, number);
    }*/
    if (card!=nullptr){
		card->clearFlags();
		foreach(QString flag, flags)
			card->setFlags(flag);
		if(name!=card->getClassName())
			card->setObjectName(name);
	}
    return card;
}

SkillCard*Engine::cloneSkillCard(const QString &name) const
{
	if(metaobjects.contains(name))
		return qobject_cast<SkillCard*>(metaobjects.value(name)->newInstance());
    if (name2cards.contains(name))
		return qobject_cast<SkillCard*>(name2cards.value(name)->metaObject()->newInstance());
	return nullptr;
}

#ifndef USE_BUILDBOT
QString Engine::getVersionNumber() const
{
    return "20251231";
}
#endif

QString Engine::getVersion() const
{
    QString version_number = getVersionNumber(), mod_name = getMODName();
    if (mod_name == "official") return version_number;
    else return QString("%1:%2").arg(version_number).arg(mod_name);
}

QString Engine::getVersionName() const
{
    return "V2";
}

QString Engine::getMODName() const
{
    return "official";
}

QStringList Engine::getExtensions() const
{
    QStringList extensions;
    foreach (const Package*package, findChildren<const Package*>()) {
        if (package->inherits("Scenario")) continue;
        extensions << package->objectName();
    }
    return extensions;
}

QStringList Engine::getKingdoms() const
{
    LuaLocker locker;
    static QStringList kingdoms = GetConfigFromLuaState(lua, "kingdoms").toStringList();
    return kingdoms;
}

QColor Engine::getKingdomColor(const QString &kingdom) const
{
    static QMap<QString, QColor> color_map;
    if (color_map.isEmpty()) {
        LuaLocker locker;
        QVariantMap map = GetValueFromLuaState(lua, "config", "kingdom_colors").toMap();
        QMapIterator<QString, QVariant> itor(map);
        while (itor.hasNext()) {
            itor.next();
            QColor color(itor.value().toString());
            if (!color.isValid()) {
                qWarning("Invalid color for kingdom %s", qPrintable(itor.key()));
                color = QColor(128, 128, 128);
            }
            color_map[itor.key()] = color;
        }
        //Q_ASSERT(!color_map.isEmpty());
    }
    return color_map.value(kingdom);
}

QMap<QString, QColor> Engine::getSkillTypeColorMap() const
{
    static QMap<QString, QColor> color_map;
    if (color_map.isEmpty()) {
        LuaLocker locker;
        QVariantMap map = GetValueFromLuaState(lua, "config", "skill_type_colors").toMap();
        QMapIterator<QString, QVariant> itor(map);
        while (itor.hasNext()) {
            itor.next();
            QColor color(itor.value().toString());
            if (!color.isValid()) {
                qWarning("Invalid color for skill type %s", qPrintable(itor.key()));
                color = QColor(128, 128, 128);
            }
            color_map[itor.key()] = color;
        }
        //Q_ASSERT(!color_map.isEmpty());
    }
    return color_map;
}

QStringList Engine::getChattingEasyTexts() const
{
    LuaLocker locker;
    static QStringList easy_texts = GetConfigFromLuaState(lua, "easy_text").toStringList();
    return easy_texts;
}

QString Engine::getSetupString() const
{
    QString flags;
    if (Config.RandomSeat)
        flags.append("R");
    if (Config.EnableCheat)
        flags.append("C");
    if (Config.EnableCheat && Config.FreeChoose)
        flags.append("F");
    if (Config.Enable2ndGeneral)
        flags.append("S");
    if (Config.EnableSame)
        flags.append("T");
    if (Config.EnableBasara)
        flags.append("B");
    if (Config.EnableHegemony)
        flags.append("H");
    if (Config.EnableAI)
        flags.append("A");
    if (Config.DisableChat)
        flags.append("M");

    if (Config.MaxHpScheme == 1)
        flags.append("1");
    else if (Config.MaxHpScheme == 2)
        flags.append("2");
    else if (Config.MaxHpScheme == 3)
        flags.append("3");
    else if (Config.MaxHpScheme == 0) {
        char c = Config.Scheme0Subtraction + 5 + 'a'; // from -5 to 12
        flags.append(c);
    }

    QString mode = Config.GameMode.mode_id;
    if (mode == "02_1v1")
        mode += Config.value("1v1/Rule", "2013").toString();
    else if (mode == "06_3v3")
        mode += Config.value("3v3/OfficialRule", "2013").toString();
    QStringList setup_items;
    setup_items << Config.ServerName.toUtf8().toBase64() << mode
		<< QString::number(Config.OperationNoLimit ? 0 : Config.OperationTimeout)
        << QString::number(Config.NullificationCountDown)
        << getBanPackages().join("+") << flags;

    return setup_items.join(":");
}

QMap<QString, QString> Engine::getAvailableModes() const
{
    return modes;
}

QString Engine::getModeName(const QString &mode) const
{
    if (modes.contains(mode)) return modes.value(mode);
    else return tr("%1 [Scenario mode]").arg(translate(mode));
}

int Engine::getPlayerCount(const QString &mode) const
{
    if (modes.contains(mode) || isNormalGameMode(mode)) { // hidden pz settings?
		if (mode_roles.contains(mode))
			return mode_roles.value(mode).length();
		static QRegExp rx("(\\d+)");
        if (rx.indexIn(mode) != -1)
            return rx.capturedTexts().first().toInt();
    } else {
        // scenario mode
        const Scenario*scenario = getScenario(mode);
        Q_ASSERT(scenario);
        return scenario->getPlayerCount();
    }
    return -1;
}

QString Engine::getRoles(const QString &mode) const
{
	if (mode_roles.contains(mode))
		return mode_roles.value(mode);
    else if (mode == "02_1v1")
        return "ZN";
    else if (mode == "03_1v2")
        return "ZFF";
    else if (mode == "04_2v2")
        return "CFFC";
    else if (mode == "04_1v3" || mode == "04_boss")
        return "ZFFF";
    else if (mode == "05_ol")
        return "ZCCFF";
    else if (mode == "06_ol")
        return "ZCCFFF";
    else if (mode == "08_defense")
        return "CCCCFFFF";

    int n = getPlayerCount(mode);
    if (modes.contains(mode) || isNormalGameMode(mode)) { // hidden pz settings?
        static const char*table1[] = {
            "",
            "",

            "ZF", // 2
            "ZFN", // 3
            "ZNFF", // 4
            "ZCFFN", // 5
            "ZCFFFN", // 6
            "ZCCFFFN", // 7
            "ZCCFFFFN", // 8
            "ZCCCFFFFN", // 9
            "ZCCCFFFFFN", // 10
            "ZCCCFFFFFN", // 11
            "ZCCCFFFFFN", // 12
            "ZCCCFFFFFN", // 13
            "ZCCCFFFFFN", // 14
            "ZCCCFFFFFN", // 15
            "ZCCCFFFFFN", // 16
            "ZCCCFFFFFN", // 17
            "ZCCCFFFFFN", // 18
            "ZCCCFFFFFN", // 19
            "ZCCCCCCCCFFFFFFFFFFN" // 20
        };

        static const char*table2[] = {
            "",
            "",

            "ZF", // 2
            "ZFN", // 3
            "ZNFF", // 4
            "ZCFFN", // 5
            "ZCFFNN", // 6
            "ZCCFFFN", // 7
            "ZCCFFFNN", // 8
            "ZCCCFFFFN", // 9
            "ZCCCFFFFNN" // 10
        };

        const char**table = mode.endsWith("d") ? table2 : table1;
        QString rolechar = table[n];
        if (mode.endsWith("z"))
            rolechar.replace("N", "C");
        else if (Config.EnableHegemony) {
            rolechar.replace("F", "N");
            rolechar.replace("C", "N");
        }
        return rolechar;
    } else if (mode.startsWith("@")) {
        if (n == 8)
            return "ZCCCNFFF";
        else if (n == 6)
            return "ZCCNFF";
    } else {
        const Scenario*scenario = getScenario(mode);
        if (scenario) return scenario->getRoles();
    }
    return "";
}

QStringList Engine::getRoleList(const QString &mode) const
{
    QStringList role_list;
    QString roles = getRoles(mode);
    for (int i = 0; i < roles.length(); i++) {
        switch (roles[i].toLatin1()) {
        case 'Z': role_list << "lord"; break;
        case 'C': role_list << "loyalist"; break;
        case 'N': role_list << "renegade"; break;
        case 'F': role_list << "rebel"; break;
        }
    }
    return role_list;
}

int Engine::getCardCount() const
{
    return cards.length();
}

QStringList Engine::getLords(bool contain_banned) const
{
    static QStringList lordList;
	if(lordList.isEmpty()){
		foreach (QString generalName, generals.keys()) {
			if (!translations.contains(generalName)||isGeneralHidden(generalName)) continue;
			if (extra_default_lords.contains(generalName)||(!removed_default_lords.contains(generalName)&&generals[generalName]->isLord()))
				lordList << generalName;
		}
	}
    QStringList lords;
	// add intrinsic lord
    foreach (QString lord, lordList) {
        if (!contain_banned) {
            if (ServerInfo.GameMode.endsWith("p")||ServerInfo.GameMode.endsWith("pd")||ServerInfo.GameMode.endsWith("pz")
				||ServerInfo.GameMode.contains("_mini_")||ServerInfo.GameMode=="custom_scenario")
                if (Config.value("Banlist/Roles").toStringList().contains(lord))
                    continue;
            if (Config.Enable2ndGeneral&&BanPair::isBanned(lord))
                continue;
        }
        lords << lord;
    }
    return lords;
}

QStringList Engine::getRandomLords() const
{
	QStringList lords = getLords(true),gns = getLimitedGeneralNames();
    godLottery(gns);
    qShuffle(gns);
	bool ban = false;
    int n = Config.value("LordMaxChoice", -1).toInt();
    if (n <= 0)
        n = Config.value("MaxChoice", 5).toInt();
    if (n <= 0)
        n = 1;
    if (n > 0) {
		QStringList _lords = lords;
		lords.clear();
		foreach (QString g, gns) {
			if(_lords.contains(g)){
				foreach (QString bn, lords){
					ban = sameNameWith(bn,g);
					if(ban) break;
				}
				gns.removeOne(g);
				if(ban) continue;
				lords << g;
				if(lords.length()>=n)
					break;
			}
		}
    }
    n = Config.value("NonLordMaxChoice", 2).toInt()+lords.length();
    foreach (QString g, gns) {
		foreach (QString bn, lords){
			ban = sameNameWith(bn,g);
			if(ban) break;
		}
		if(ban) continue;
        lords << g;
		if(lords.length()>=n)
			break;
	}
    return lords;
}

QStringList Engine::getLimitedGeneralNames(const QString &kingdom, bool available) const
{
	QStringList general_names, ban = ServerInfo.BanPackages;
	if (ServerInfo.GameMode == "03_1v2")
		ban << Config.value("Banlist/Doudizhu").toStringList();
	else if (ServerInfo.GameMode == "04_2v2")
		ban << Config.value("Banlist/Happy2v2").toStringList();
	else if (ServerInfo.GameMode == "02_1v1")
		ban << Config.value("Banlist/1v1").toStringList();
    else if (ServerInfo.GameMode == "04_boss")
		ban << Config.value("Banlist/BossMode").toStringList();
    else if (ServerInfo.GameMode == "05_ol")
		ban << Config.value("Banlist/05_ol").toStringList();
    else if (ServerInfo.GameMode == "06_ol")
		ban << Config.value("Banlist/06_ol").toStringList();
	else if (ServerInfo.GameMode=="custom_scenario"||ServerInfo.GameMode.contains("_mini_")||isNormalGameMode(ServerInfo.GameMode))
		ban << Config.value("Banlist/Roles").toStringList();
	if (ServerInfo.EnableBasara)
		ban << Config.value("Banlist/Basara").toStringList();
	if (ServerInfo.EnableHegemony)
		ban << Config.value("Banlist/Hegemony").toStringList();/*
	
    QHashIterator<QString, const General*> itor(available?available_generals:generals);
    while (itor.hasNext()) {
        itor.next();
        if(ban.contains(itor.key())||ban.contains(itor.value()->getPackage())||!translations.contains(itor.key())||isGeneralHidden(itor.key())) continue;
		if(!Config.AddGodGeneral&&itor.value()->getKingdoms().contains("god")) continue;
        if(kingdom.isEmpty()||itor.value()->getKingdoms().contains(kingdom))
            general_names << itor.key();
    }*/
	foreach (const General*general, (available?available_generals:generals)) {
        if(ban.contains(general->objectName())||ban.contains(general->getPackage())) continue;
        if(!translations.contains(general->objectName())||isGeneralHidden(general->objectName())) continue;
		if(!Config.AddGodGeneral&&general->getKingdoms().contains("god")) continue;
        if(kingdom.isEmpty()||general->getKingdoms().contains(kingdom))
            general_names << general->objectName();
	}
    // special case for neo standard package
    if (ban.contains("standard") && general_names.length()<5) {
        if (kingdom.isEmpty() || kingdom == "wei")
            general_names << "zhenji";
        if (kingdom.isEmpty() || kingdom == "shu")
            general_names << "zhugeliang";
        if (kingdom.isEmpty() || kingdom == "wu")
            general_names << "sunquan" << "sunshangxiang";
    }
    return general_names;
}

QStringList Engine::getSlashNames() const
{
    return getCardNames("Slash");
}

QStringList Engine::getCardNames(const QString &pattern) const
{
    QStringList Names;
	QList<int> ids = getRandomCards();
    for (int i = 0; i < getCardCount(); i++){
		if(ids.contains(i)){
			const Card*card = getEngineCard(i);
			if(Names.contains(card->objectName())) continue;
			if(matchExpPattern(pattern,nullptr,card))
				Names << card->objectName();
		}
	}
    return Names;
}

bool Engine::hasCard(const QString &name) const
{
    if (name.isEmpty()) return false;
    foreach (int id, getRandomCards()) {
        if (getEngineCard(id)->objectName() == name)
            return true;
    }
    return false;
}

bool Engine::sameNameWith(const QString &name1, const QString &name2) const
{
    if(name1.contains(name2)||name2.contains(name1))
		return true;
    if(name1.contains("_")&&name2.contains(name1.split("_").last()))
		return true;
    if(name2.contains("_")&&name1.contains(name2.split("_").last()))
		return true;
	return false;
	//return name1.contains(name2)||name2.contains(name1)
	//||name1.contains(name2.split("_").last())||name2.contains(name1.split("_").last());
}

QList<const Skill *> Engine::getSafeSkills() const
{
    QList<const Skill *> list;
    // 遍歷 Hash 中的所有 QPointer
    foreach (const QPointer<Skill> &ptr, skills.values()) {
        // QPointer 自動魔法：如果對象被 delete 了，isNull() 會變 true
        if (!ptr.isNull()) {
            list << ptr.data();
        }
    }
    return list;
}

QStringList Engine::getRandomGenerals(int count, const QSet<QString> &ban_set, const QString &kingdom) const
{
    QStringList general_list, all_generals = getLimitedGeneralNames(kingdom);
    //Q_ASSERT(all_generals.count() > count);

    godLottery(all_generals);
    qShuffle(all_generals);

	bool ban = false;
	foreach (QString general_name, all_generals) {
		foreach (QString bn, ban_set){
			ban = sameNameWith(bn,general_name);
			if(ban) break;
		}
		if(ban) continue;
		foreach (QString bn, general_list){
			ban = sameNameWith(bn,general_name);
			if(ban) break;
		}
		if(ban) continue;
		general_list << general_name;
		if(general_list.length()>=count)
			break;
	}
    return general_list;
}

QList<int> Engine::getRandomCards(bool derivative) const
{
    bool exclude_disaster = Config.GameMode.mode_id == "04_1v3", using_2012_3v3 = false,
		using_2013_3v3 = false, challengedeveloper = Config.GameMode.mode_id == "challengedeveloper";
    if (Config.GameMode.mode_id == "06_3v3") {
        using_2012_3v3 = Config.value("3v3/OfficialRule").toString() == "2012";
        using_2013_3v3 = Config.value("3v3/OfficialRule", "2013").toString() == "2013";
        exclude_disaster = !Config.value("3v3/UsingExtension").toBool() || Config.value("3v3/ExcludeDisasters", true).toBool();
    }
    QList<int> list;
    foreach (Card*card, cards) {
		if(card->objectName().startsWith("_")){
			if(!derivative||card->objectName().startsWith("__")) continue;
		}else if (challengedeveloper && card->objectName() == "god_salvation") continue;
		else if (exclude_disaster && card->isKindOf("Disaster")) continue;
        bool removed = false;
        foreach (QString banned_pattern, Config.value("Banlist/Cards").toStringList()) {
            removed = matchExpPattern(banned_pattern, nullptr, card);
			if (removed) break;
        }
        if (removed) continue;
        //card->clearFlags();
		QString pn = card->getPackage();
		if(pn == "New3v3Card"){
			if(using_2012_3v3 || using_2013_3v3)
				list << card->getId();
		}else if(pn == "New3v3_2013Card"){
			if(using_2013_3v3)
				list << card->getId();
		}else if(pn == "New1v1Card"){
			if(Config.GameMode.mode_id == "02_1v1")
                list << card->getId();
		}else{
			if(ServerInfo.BanPackages.contains(pn)) continue;
			if(Config.GameMode.mode_id == "06_3v3"){
				if(!Config.value("3v3/UsingExtension").toBool() && !pn.startsWith("standard_")) continue;
			}else if(Config.GameMode.mode_id == "02_1v1"){
				if(!Config.value("1v1/UsingCardExtension").toBool()) continue;
			}
			list << card->getId();
		}
    }
    if (using_2013_3v3) {
        list.removeOne(53);
        list.removeOne(54);
        list.removeOne(98);
    }else if(using_2012_3v3)
        list.removeOne(98);
    qShuffle(list);
    return list;
}

QString Engine::getRandomGeneralName() const
{
    return generals.keys().at(qrand()%generals.size());
}

bool Engine::playSystemAudioEffect(const QString &name, bool superpose) const
{
    return playAudioEffect(QString("audio/system/%1.ogg").arg(name), superpose);
}

bool Engine::playAudioEffect(const QString &filename, bool superpose) const
{
    if(filename.isEmpty()||!Config.EnableEffects||!QFile::exists(filename)) return false;
#ifdef AUDIO_SUPPORT
	Audio::play(filename, superpose);
#endif
	return true;
}

void Engine::setAudioType(const QString &general_name, const QString &filename, const QString &types)
{
    audio_type[general_name+filename] = ListS2I(types.split(","));
}

int Engine::revisesAudioType(const QString &general_name, const QString &filename, int type) const
{
	QList<int> ras = audio_type[general_name+filename];
	if(ras.isEmpty()||type>ras.length()) return type;
	if(type>0) return ras[type-1];
	return ras[qrand()%ras.length()];
}

void Engine::playSkillAudioEffect(const QString &skill_name, int index, bool superpose) const
{
    const Skill*skill = skills.value(skill_name, nullptr);
    if (skill) skill->playAudioEffect(index, superpose);
}

const Skill*Engine::getSkill(const QString &skill_name) const
{
    return skills.value(skill_name, nullptr);
}

const Skill*Engine::getSkill(const EquipCard*equip) const
{
    if (equip) return getSkill(equip->objectName());
    return nullptr;
}

QStringList Engine::getSkillNames() const
{
    return skills.keys();
}

Skill*Engine::getRealSkill(const QString &skill_name)
{
    const Skill*skill = getSkill(skill_name);
	if (skill) return const_cast<Skill*>(skill);
	return nullptr;
}

const TriggerSkill*Engine::getTriggerSkill(const QString &skill_name) const
{
    const Skill*skill = getSkill(skill_name);
    if (skill) return qobject_cast<const TriggerSkill*>(skill);
    return nullptr;
}

const ViewAsSkill*Engine::getViewAsSkill(const QString &skill_name) const
{
    const Skill*skill = getSkill(skill_name);
	if(skill){
		if (skill->inherits("ViewAsSkill"))
			return qobject_cast<const ViewAsSkill*>(skill);
		else if (skill->inherits("TriggerSkill"))
			return qobject_cast<const TriggerSkill*>(skill)->getViewAsSkill();
	}
	return nullptr;
}

const ViewAsEquipSkill*Engine::getViewAsEquipSkill(const QString &skill_name) const
{
    const Skill*skill = getSkill(skill_name);
    if (skill) return qobject_cast<const ViewAsEquipSkill*>(skill);
    return nullptr;
}

const CardLimitSkill*Engine::getCardLimitSkill(const QString &skill_name) const
{
    const Skill*skill = getSkill(skill_name);
    if (skill) return qobject_cast<const CardLimitSkill*>(skill);
    return nullptr;
}

const ProhibitSkill*Engine::isProhibited(const Player*from, const Player*to, const Card*card, const QList<const Player*> &others) const
{
	foreach (const ProhibitSkill*skill, getProhibitSkills()) {
        if (skill->isProhibited(from, to, card, others))
            return skill;
    }
    return nullptr;
}

const ProhibitPindianSkill*Engine::isPindianProhibited(const Player*from, const Player*to) const
{
    foreach (const ProhibitPindianSkill*skill, getProhibitPindianSkills()) {
        if (skill->isPindianProhibited(from, to))
            return skill;
    }
    return nullptr;
}

const CardLimitSkill*Engine::isCardLimited(const Player*player, const Card*card, Card::HandlingMethod method, bool isHandcard) const
{
    static QMap<Card::HandlingMethod, QString> method_map;
    if (method_map.isEmpty()) {
        method_map.insert(Card::MethodUse, "use");
        method_map.insert(Card::MethodResponse, "response");
        method_map.insert(Card::MethodDiscard, "discard");
        method_map.insert(Card::MethodRecast, "recast");
        method_map.insert(Card::MethodPindian, "pindian");
        method_map.insert(Card::MethodIgnore, "ignore");
        method_map.insert(Card::MethodEffect, "effect");
    }
    QString method_name = method_map.value(method, "");
	if(method_name=="") return nullptr;

    if (card->inherits("SkillCard") && method == card->getHandlingMethod()) {
        foreach (int id, card->getSubcards()) {
            const Card*c = Sanguosha->getCard(id);
            foreach (const CardLimitSkill*skill, getCardLimitSkills()) {
                if (skill->limitList(player,c).contains(method_name)){
					QString pattern = skill->limitPattern(player,c);
					if(pattern.isEmpty()) continue;
					if(isHandcard) pattern.replace("hand", ".");
					if(matchExpPattern(pattern,player,c)) return skill;
				}
            }
        }
    } else {
        foreach (const CardLimitSkill*skill, getCardLimitSkills()) {
            if (skill->limitList(player,card).contains(method_name)){
				QString pattern = skill->limitPattern(player,card);
				if(pattern.isEmpty()) continue;
				if(isHandcard) pattern.replace("hand", ".");
				if(matchExpPattern(pattern,player,card)) return skill;
			}
        }
    }
    return nullptr;
}

int Engine::correctDistance(const Player*from, const Player*to, bool fixed) const
{
    int correct = 0;
	if (fixed){
		foreach (const DistanceSkill*skill, getDistanceSkills()) {
            int f = skill->getFixed(from, to);
            if (f > correct) correct = f;
		}
	}else{
		foreach (const DistanceSkill*skill, getDistanceSkills())
			correct += skill->getCorrect(from, to);
	}
	return correct;
}

int Engine::correctMaxCards(const Player*target, bool fixed) const
{
    int ex = -1;
    if (fixed) {
        foreach (const MaxCardsSkill*skill, getMaxCardsSkills()) {
			int f = skill->getFixed(target);
            if (f > ex) ex = f;
        }
    } else {
        ex++;
        foreach(const MaxCardsSkill*skill, getMaxCardsSkills())
            ex += skill->getExtra(target);
    }
	return ex;
}

int Engine::correctCardTarget(const TargetModSkill::ModType type, const Player*from, const Card*card, const Player*to) const
{
    if (!from || !card) return 0;
    int x = 0;
	QStringList subcardNames;
	if (card->isVirtualCard()){
        foreach (const Card*e, from->getEquips()){
            if (card->getSubcards().contains(e->getId()))
				subcardNames << e->objectName();
		}
	}
    if (type == TargetModSkill::Residue) {
        foreach (const TargetModSkill*skill, getTargetModSkills()) {
			if (subcardNames.contains(skill->objectName())) continue;
            if (matchExpPattern(skill->getPattern(),from, card)) {
                x += skill->getResidueNum(from, card, to);
                if (x > 500) break;
            }
        }
    } else if (type == TargetModSkill::DistanceLimit) {
        foreach (const TargetModSkill*skill, getTargetModSkills()) {
			if (subcardNames.contains(skill->objectName())) continue;
            if (matchExpPattern(skill->getPattern(),from, card)) {
                x += skill->getDistanceLimit(from, card, to);
                if (x > 500) break;
            }
        }
    } else if (type == TargetModSkill::ExtraTarget) {
        foreach (const TargetModSkill*skill, getTargetModSkills()) {
			if (subcardNames.contains(skill->objectName())) continue;
            if (matchExpPattern(skill->getPattern(),from, card)){
                x += skill->getExtraTargetNum(from, card);
                if (x > 500) break;
			}
        }
    }
    return x;
}

bool Engine::correctSkillValidity(const Player*player, const Skill*skill) const
{
    foreach (const InvaliditySkill*is, getInvaliditySkills()) {
        if (is->isSkillValid(player, skill)) continue;
		return false;
    }
    return true;
}

int Engine::correctAttackRange(const Player*target, bool include_weapon, bool fixed) const
{
    int extra = -1;
    if (fixed) {
		foreach (const AttackRangeSkill*skill, getAttackRangeSkills()) {
            int f = skill->getFixed(target, include_weapon);
            if (f > extra) extra = f;
		}
	} else {
		extra++;
		foreach (const AttackRangeSkill*skill, getAttackRangeSkills())
            extra += skill->getExtra(target, include_weapon);
    }
	return extra;
}

QString Engine::removeNumberInQString(const QString &str) const
{
    QString _str;
    for (int i = 0; i < str.length(); i++) {
        if (QString(str[i]).toInt() > 0) break;
        else _str.append(str[i]);
    }
    return _str;
}

#ifdef LOGNETWORK
void Engine::handleNetworkMessage(QString s)
{
    QTextStream out(&logFile);
    out << s << "\n";
}
#endif // LOGNETWORK

void Engine::godLottery(QStringList &list) const
{
	if(Config.AddGodGeneral)
		return;
	qDebug("godLottery");

	qsrand(QDateTime::currentMSecsSinceEpoch());
	Config.beginGroup("godlottery");
	foreach (const General*general, generals) {
		if(general->getKingdom()=="god"&&general->objectName().contains("shen")){
			if(qrand()%10000<=Config.value(general->objectName()).toInt()) {
                //qDebug((general->objectName()+"被抽中").toUtf8().data());
				list.append(general->objectName());
			}//else
				//qDebug((general->objectName()+"没中").toUtf8().data());
		}
	}
	Config.endGroup();
}

void Engine::godLottery(QSet<QString> &generalSet) const
{
	QStringList list = generalSet.values();
	godLottery(list);
	generalSet = QSet<QString>(list.begin(), list.end());
}

