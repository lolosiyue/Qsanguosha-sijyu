#include "serversetupsession.h"
#include "server.h"
#include "customassigndialog.h"
#include "settings.h"
#include "engine.h"
#include "general.h"
#include "card.h"
#include "package.h"
#include "miniscenarios.h"

namespace {
QWidget *dialogParent()
{
    return QApplication::activeWindow();
}

QVariantMap option(const QString &value, const QString &label)
{
    return { { QStringLiteral("value"), value }, { QStringLiteral("label"), label } };
}

QVariantMap modeEntry(const QString &kind, const QString &key, const QString &label,
                      const QVariantList &options = QVariantList())
{
    return { { QStringLiteral("kind"), kind }, { QStringLiteral("key"), key },
             { QStringLiteral("label"), label }, { QStringLiteral("options"), options } };
}

QString packageContents(const Package *package)
{
    QStringList generals;
    foreach (const General *general, package->findChildren<const General *>()) {
        const QString name = general->getBriefName();
        if (!general->isTotallyHidden() && !generals.contains(name))
            generals << name;
    }
    QStringList cards;
    foreach (const Card *card, package->findChildren<const Card *>()) {
        const QString name = Sanguosha->translate(card->objectName());
        if (!cards.contains(name))
            cards << name;
    }
    const QString separator = ServerSetupSession::tr(", ");
    QStringList parts;
    if (!generals.isEmpty())
        parts << ServerSetupSession::tr("<b>Generals</b>: %1").arg(generals.join(separator));
    if (!cards.isEmpty())
        parts << ServerSetupSession::tr("<b>Cards</b>: %1").arg(cards.join(separator));
    return parts.join(QStringLiteral("<br/><br/>"));
}

QVariantMap packageEntry(const Package *package)
{
    const QString name = package->objectName();
    return { { QStringLiteral("name"), name },
             { QStringLiteral("label"), Sanguosha->translate(name) },
             { QStringLiteral("checked"), !Config.BanPackages.contains(name) && !package->isForbid() },
             { QStringLiteral("enabled"), !package->isForbid() } };
}

QVariantMap packageSection(const QString &title, const QVariantList &packages)
{
    return { { QStringLiteral("title"), title }, { QStringLiteral("packages"), packages } };
}
}

ServerSetupSession::ServerSetupSession(QObject *parent)
    : QObject(parent)
{
}

QVariantMap ServerSetupSession::load() const
{
    QVariantMap values;
    values.insert("ServerName", Config.ServerName);
    values.insert("OperationTimeout", Config.OperationTimeout);
    values.insert("OperationNoLimit", Config.OperationNoLimit);
    values.insert("GameMode", Config.GameMode.mode_id);

    values.insert("RandomSeat", Config.RandomSeat);
    values.insert("EnableCheat", Config.EnableCheat);
    values.insert("FreeChoose", Config.FreeChoose);
    values.insert("FreeAssign", Config.value("FreeAssign").toBool());
    values.insert("FreeAssignSelf", Config.FreeAssignSelf);
    values.insert("PileSwappingLimitation", Config.value("PileSwappingLimitation", 5).toInt());
    values.insert("WithoutLordskill", Config.value("WithoutLordskill", false).toBool());
    values.insert("EnableSPConvert", Config.value("EnableSPConvert", true).toBool());
    values.insert("MaxChoice", Config.value("MaxChoice", 5).toInt());
    values.insert("LordMaxChoice", Config.value("LordMaxChoice", -1).toInt());
    values.insert("NonLordMaxChoice", Config.value("NonLordMaxChoice", 2).toInt());
    values.insert("ForbidSIMC", Config.ForbidSIMC);
    values.insert("DisableChat", Config.DisableChat);
    values.insert("Enable2ndGeneral", Config.Enable2ndGeneral);
    values.insert("MaxHpScheme", Config.MaxHpScheme);
    values.insert("Scheme0Subtraction", Config.Scheme0Subtraction);
    values.insert("PreventAwakenBelow3", Config.PreventAwakenBelow3);
    values.insert("EnableHegemony", Config.EnableHegemony);
    values.insert("EnableMeleeMode", Config.EnableMeleeMode);
    values.insert("HegemonyMaxChoice", Config.value("HegemonyMaxChoice", 7).toInt());
    values.insert("RewardTheFirstShowingPlayer", Config.value("RewardTheFirstShowingPlayer", true).toBool());
    values.insert("Address", Config.Address);
    values.insert("ServerPort", Config.ServerPort);
    values.insert("serverconfig/upnp", Config.value("serverconfig/upnp", true).toBool());
    values.insert("serverconfig/addtolistserver", Config.value("serverconfig/addtolistserver", false).toBool());

    values.insert("CountDownSeconds", Config.CountDownSeconds);
    values.insert("NullificationCountDown", Config.NullificationCountDown);
    values.insert("LuckCardTimes", Config.value("LuckCardTimes", -1).toInt());
    values.insert("EnableMinimizeDialog", Config.EnableMinimizeDialog);
    values.insert("SurrenderAtDeath", Config.SurrenderAtDeath);
    values.insert("EnableAI", Config.EnableAI);
    values.insert("JevHybrid50P", Config.value("JevHybrid50P", false).toBool());
    values.insert("AIChat", Config.value("AIChat", true).toBool());
    values.insert("AIHumanized", Config.value("AIHumanized", true).toBool());
    values.insert("OriginAIDelay", Config.OriginAIDelay);
    values.insert("AlterAIDelayAD", Config.AlterAIDelayAD);
    values.insert("AIDelayAD", Config.AIDelayAD);

    values.insert("DisableLua", Config.DisableLua);
    values.insert("AddGodGeneral", Config.AddGodGeneral);
    values.insert("GeneralVersionDedup", Config.GeneralVersionDedup);

    values.insert("1v1/Rule", Config.value("1v1/Rule", "2013").toString());
    values.insert("1v1/UsingExtension", Config.value("1v1/UsingExtension", false).toBool());
    values.insert("1v1/UsingCardExtension", Config.value("1v1/UsingCardExtension", false).toBool());
    values.insert("3v3/UsingExtension", Config.value("3v3/UsingExtension", false).toBool());
    values.insert("3v3/OfficialRule", Config.value("3v3/OfficialRule", "2013").toString());
    values.insert("3v3/ExcludeDisasters", Config.value("3v3/ExcludeDisasters", true).toBool());
    values.insert("3v3/RoleChoose", Config.value("3v3/RoleChoose", "Normal").toString());
    values.insert("XMode/RoleChooseX", Config.value("XMode/RoleChooseX", "Normal").toString());
    return values;
}

QVariantList ServerSetupSession::modes() const
{
    QVariantList entries;
    QMap<QString, GameModeStruct> modes = Sanguosha->getAvailableModes();
#if defined(Q_OS_ANDROID) || defined(QSAN_XP_LEGACY)
    // M1's large-room presentation is available only in the modern desktop GUI.
    modes.remove(QStringLiteral("50p"));
#endif

    QSet<QString> groupNameSet;
    for (auto it = modes.cbegin(); it != modes.cend(); ++it) {
        const QString groupName = Sanguosha->getModeGroup(it.key());
        if (!groupName.isEmpty())
            groupNameSet.insert(groupName);
    }
    QStringList groupNames = groupNameSet.values();
    groupNames.sort();

    QSet<QString> groupedModes;
    foreach (const QString &groupName, groupNames) {
        QVariantList options;
        foreach (const QString &modeId, Sanguosha->getGroupModes(groupName)) {
            if (!modes.contains(modeId))
                continue;
            options << option(modeId, Sanguosha->getModeName(modeId));
            groupedModes.insert(modeId);
        }
        if (!options.isEmpty())
            entries << modeEntry(QStringLiteral("group"), groupName, groupName, options);
    }

    for (auto it = modes.cbegin(); it != modes.cend(); ++it) {
        if (!groupedModes.contains(it.key()))
            entries << modeEntry(QStringLiteral("mode"), it.key(), Sanguosha->getModeName(it.key()));
    }

    QVariantList scenarios;
    foreach (const QString &name, Sanguosha->getModScenarioNames()) {
        const Scenario *scenario = Sanguosha->getScenario(name);
        scenarios << option(name, ServerDialog::tr("%1 (%2 persons)")
            .arg(Sanguosha->translate(name)).arg(scenario->getPlayerCount()));
    }
    entries << modeEntry(QStringLiteral("scenario"), QStringLiteral("scenario"),
                         ServerDialog::tr("Scenario mode"), scenarios);

    QVariantList miniScenes;
    const int stage = qMin(Sanguosha->getMiniSceneCounts(), Config.value("MiniSceneStage", 1).toInt());
    for (int i = 1; i <= stage; ++i) {
        const QString name = QString(MiniScene::S_KEY_MINISCENE).arg(i);
        const Scenario *scenario = Sanguosha->getScenario(name);
        miniScenes << option(name, ServerDialog::tr("%1 (%2 persons)")
            .arg(Sanguosha->translate(name)).arg(scenario->getPlayerCount()));
    }
    entries << modeEntry(QStringLiteral("mini"), QStringLiteral("mini"),
                         ServerDialog::tr("Mini Scenes"), miniScenes);
    return entries;
}

QVariantList ServerSetupSession::packageSections() const
{
    QVariantList sections;
    QHash<QString, const Package *> packagesByAdder;
    foreach (const Package *package, Sanguosha->getPackages()) {
        if (!package->adderName().isEmpty())
            packagesByAdder.insert(package->adderName(), package);
    }

    const QMap<QString, QStringList> packageMap = Sanguosha->getPackageMap();
    for (auto it = packageMap.cbegin(); it != packageMap.cend(); ++it) {
        if (it.key() == QStringLiteral("g_special_play"))
            continue;
        QVariantList packages;
        foreach (const QString &adderName, it.value()) {
            if (const Package *package = packagesByAdder.value(adderName, nullptr))
                packages << packageEntry(package);
        }
        if (!packages.isEmpty())
            sections << packageSection(Sanguosha->translate(it.key()), packages);
    }

    QVariantList luaGenerals;
    QVariantList luaCards;
    const QStringList luaPackages = Config.value("LuaPackages").toString()
        .split("+", Qt::SkipEmptyParts);
    foreach (const QString &packageName, luaPackages) {
        const Package *package = Sanguosha->findChild<const Package *>(packageName);
        if (!package)
            continue;
        if (package->getType() == Package::CardPack)
            luaCards << packageEntry(package);
        else
            luaGenerals << packageEntry(package);
    }
    if (!luaGenerals.isEmpty())
        sections << packageSection(Sanguosha->translate(QStringLiteral("lua_package")), luaGenerals);
    if (!luaCards.isEmpty())
        sections << packageSection(Sanguosha->translate(QStringLiteral("lua_card")), luaCards);
    return sections;
}

QString ServerSetupSession::packageTooltip(const QString &name) const
{
    const Package *package = Sanguosha->findChild<const Package *>(name);
    return package ? packageContents(package) : QString();
}

QString ServerSetupSession::detectAddress() const
{
    const QHostInfo hostInfo = QHostInfo::fromName(QHostInfo::localHostName());
    foreach (const QHostAddress &address, hostInfo.addresses()) {
        if (!address.isNull() && address != QHostAddress::LocalHost
            && address.protocol() == QAbstractSocket::IPv4Protocol)
            return address.toString();
    }
    return QString();
}

void ServerSetupSession::editBanlist()
{
    BanlistDialog dialog(dialogParent());
    dialog.exec();
}

void ServerSetupSession::select3v3Generals()
{
    Select3v3GeneralDialog dialog(dialogParent());
    dialog.exec();
}

bool ServerSetupSession::editCustomMiniScene()
{
    bool changed = false;
    CustomAssignDialog dialog(dialogParent());
    connect(&dialog, &CustomAssignDialog::scenario_changed, this, [&changed]() { changed = true; });
    dialog.exec();
    return changed;
}

void ServerSetupSession::editBossMode()
{
    BossModeCustomAssignDialog dialog(dialogParent());
    dialog.config();
}

void ServerSetupSession::start(const QVariantMap &values, int acceptType)
{
    commit(values);
    emit startRequested(acceptType);
}

void ServerSetupSession::commit(const QVariantMap &values)
{
    Config.ServerName = values.value("ServerName").toString();
    Config.OperationTimeout = values.value("OperationTimeout").toInt();
    Config.OperationNoLimit = values.value("OperationNoLimit").toBool();
    Config.RandomSeat = values.value("RandomSeat").toBool();
    Config.EnableCheat = values.value("EnableCheat").toBool();
    Config.FreeChoose = Config.EnableCheat && values.value("FreeChoose").toBool();
    Config.FreeAssignSelf = Config.EnableCheat && values.value("FreeAssignSelf").toBool();
    Config.ForbidSIMC = values.value("ForbidSIMC").toBool();
    Config.DisableChat = values.value("DisableChat").toBool();
    Config.Enable2ndGeneral = values.value("Enable2ndGeneral").toBool();
    Config.EnableHegemony = values.value("EnableHegemony").toBool();
    if (Config.EnableHegemony) {
        // Hegemony always owns the dual-general concealed setup.
        Config.Enable2ndGeneral = true;
    }
    Config.EnableMeleeMode = values.value("EnableMeleeMode").toBool();
    Config.MaxHpScheme = values.value("MaxHpScheme").toInt();
    if (Config.MaxHpScheme == 0) {
        Config.Scheme0Subtraction = values.value("Scheme0Subtraction").toInt();
        Config.PreventAwakenBelow3 = false;
    } else {
        Config.Scheme0Subtraction = 3;
        Config.PreventAwakenBelow3 = values.value("PreventAwakenBelow3").toBool();
    }
    Config.Address = values.value("Address").toString();
    Config.CountDownSeconds = values.value("CountDownSeconds").toInt();
    Config.NullificationCountDown = values.value("NullificationCountDown").toInt();
    Config.EnableMinimizeDialog = values.value("EnableMinimizeDialog").toBool();
    Config.EnableAI = values.value("EnableAI").toBool();
    Config.OriginAIDelay = values.value("OriginAIDelay").toInt();
    Config.AIDelay = Config.OriginAIDelay;
    Config.AIDelayAD = values.value("AIDelayAD").toInt();
    Config.AlterAIDelayAD = values.value("AlterAIDelayAD").toBool();
    Config.ServerPort = values.value("ServerPort").toInt();
    Config.DisableLua = values.value("DisableLua").toBool();
    Config.AddGodGeneral = values.value("AddGodGeneral").toBool();
    Config.GeneralVersionDedup = values.value("GeneralVersionDedup").toBool();
    Config.SurrenderAtDeath = values.value("SurrenderAtDeath").toBool();

    // A missing GameMode means no mode was selected; keep the current one.
    if (values.contains("GameMode"))
        Config.GameMode = Sanguosha->getGameMode(values.value("GameMode").toString());

    Config.setValue("ServerName", Config.ServerName);
    Config.setValue("GameMode", Config.GameMode.mode_id);
    Config.setValue("OperationTimeout", Config.OperationTimeout);
    Config.setValue("OperationNoLimit", Config.OperationNoLimit);
    Config.setValue("RandomSeat", Config.RandomSeat);
    Config.setValue("EnableCheat", Config.EnableCheat);
    Config.setValue("FreeChoose", Config.FreeChoose);
    Config.setValue("FreeAssign", Config.EnableCheat && values.value("FreeAssign").toBool());
    Config.setValue("FreeAssignSelf", Config.FreeAssignSelf);
    Config.setValue("PileSwappingLimitation", values.value("PileSwappingLimitation").toInt());
    Config.setValue("WithoutLordskill", values.value("WithoutLordskill").toBool());
    Config.setValue("EnableSPConvert", values.value("EnableSPConvert").toBool());
    Config.setValue("MaxChoice", values.value("MaxChoice").toInt());
    Config.setValue("LordMaxChoice", values.value("LordMaxChoice").toInt());
    Config.setValue("NonLordMaxChoice", values.value("NonLordMaxChoice").toInt());
    Config.setValue("ForbidSIMC", Config.ForbidSIMC);
    Config.setValue("DisableChat", Config.DisableChat);
    Config.setValue("Enable2ndGeneral", Config.Enable2ndGeneral);
    Config.setValue("EnableHegemony", Config.EnableHegemony);
    Config.setValue("EnableMeleeMode", Config.EnableMeleeMode);
    Config.setValue("HegemonyMaxChoice", values.value("HegemonyMaxChoice").toInt());
    Config.setValue("RewardTheFirstShowingPlayer", values.value("RewardTheFirstShowingPlayer").toBool());
    Config.setValue("MaxHpScheme", Config.MaxHpScheme);
    Config.setValue("Scheme0Subtraction", Config.Scheme0Subtraction);
    Config.setValue("PreventAwakenBelow3", Config.PreventAwakenBelow3);
    Config.setValue("CountDownSeconds", Config.CountDownSeconds);
    Config.setValue("NullificationCountDown", Config.NullificationCountDown);
    Config.setValue("EnableMinimizeDialog", Config.EnableMinimizeDialog);
    Config.setValue("EnableAI", Config.EnableAI);
    // Remember the player's preference even while another mode temporarily
    // disables the control; the room gate applies it only to AI-enabled 50p.
    Config.setValue("JevHybrid50P", values.value("JevHybrid50P").toBool());
    Config.setValue("AIChat", values.value("AIChat").toBool());
    Config.setValue("AIHumanized", values.value("AIHumanized").toBool());
    Config.setValue("OriginAIDelay", Config.OriginAIDelay);
    Config.setValue("AlterAIDelayAD", Config.AlterAIDelayAD);
    Config.setValue("AIDelayAD", Config.AIDelayAD);
    Config.setValue("SurrenderAtDeath", Config.SurrenderAtDeath);
    Config.setValue("LuckCardTimes", values.value("LuckCardTimes").toInt());
    Config.setValue("ServerPort", Config.ServerPort);
    Config.setValue("Address", Config.Address);
    Config.setValue("DisableLua", Config.DisableLua);
    Config.setValue("AddGodGeneral", Config.AddGodGeneral);
    Config.setValue("GeneralVersionDedup", Config.GeneralVersionDedup);
    Config.setValue("serverconfig/upnp", values.value("serverconfig/upnp").toBool());
    Config.setValue("serverconfig/addtolistserver", values.value("serverconfig/addtolistserver").toBool());

    Config.beginGroup("3v3");
    Config.setValue("UsingExtension", values.value("3v3/UsingExtension").toBool());
    Config.setValue("RoleChoose", values.value("3v3/RoleChoose").toString());
    Config.setValue("ExcludeDisasters", values.value("3v3/ExcludeDisasters").toBool());
    Config.setValue("OfficialRule", values.value("3v3/OfficialRule").toString());
    Config.endGroup();

    Config.beginGroup("1v1");
    Config.setValue("Rule", values.value("1v1/Rule").toString());
    Config.setValue("UsingExtension", values.value("1v1/UsingExtension").toBool());
    Config.setValue("UsingCardExtension", values.value("1v1/UsingCardExtension").toBool());
    Config.endGroup();

    Config.beginGroup("XMode");
    Config.setValue("RoleChooseX", values.value("XMode/RoleChooseX").toString());
    Config.endGroup();

    Config.EnabledPackages = values.value("EnabledPackages").toStringList();
    Config.BanPackages = values.value("BanPackages").toStringList();
    const QStringList specialPackageAdders =
        Sanguosha->getPackageMap().value(QStringLiteral("g_special_play"));
    foreach (const Package *package, Sanguosha->getPackages()) {
        if ((package->inherits("Scenario")
             || specialPackageAdders.contains(package->adderName()))
            && !Config.BanPackages.contains(package->objectName())) {
            Config.BanPackages << package->objectName();
        }
    }
    Config.setValue("EnabledPackages", Config.EnabledPackages);
    Config.setValue("EnabledPackagesMigrationVersion", 2);
    Config.remove("BanPackages");
    Config.sync();
}
