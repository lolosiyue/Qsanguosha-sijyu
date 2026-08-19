#include "homecontroller.h"
#include "engine.h"
#include "general.h"
#include "skill.h"
#include "settings.h"
#include "package.h"
#include "heroskincontainer.h"
#include <QCoreApplication>
#include <QSet>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QGuiApplication>
#include <QStyleHints>
#include <QPointer>
#include <QQuickWidget>
#include <QTimer>
#include <QRegularExpression>
#include <QImage>
#include <QImageReader>
#include <QtGlobal>

namespace {

QUrl firstExistingImage(const QStringList &stems)
{
    static const QStringList suffixes = {
        QStringLiteral(".png"), QStringLiteral(".jpg"),
        QStringLiteral(".webp"), QStringLiteral(".jpeg")
    };
    const QStringList roots = HeroSkinContainer::skinSearchRoots();
    for (const QString &root : roots) {
        const QDir rootDir(root);
        for (const QString &stem : stems) {
            for (const QString &suffix : suffixes) {
                const QString path = rootDir.absoluteFilePath(stem + suffix);
                if (QFile::exists(path))
                    return QUrl::fromLocalFile(path);
            }
        }
    }
    return {};
}

QString nicknameOf(const QString &name)
{
    if (!Sanguosha)
        return {};
    QString nickname = Sanguosha->translate("#" + name);
    if (nickname.contains(QLatin1Char('_')))
        nickname = Sanguosha->translate("#" + nickname.split(QLatin1Char('_')).last());
    if (nickname.startsWith(QLatin1Char('#')))
        nickname.clear();
    return nickname;
}

QString genderKeyOf(const General *general)
{
    if (general->isMale())
        return QStringLiteral("male");
    if (general->isFemale())
        return QStringLiteral("female");
    if (general->isNeuter())
        return QStringLiteral("neuter");
    return QStringLiteral("sexless");
}

QString genderDisplayOf(const QString &key)
{
    if (key == QLatin1String("male"))
        return QCoreApplication::translate("GeneralOverview", "Male");
    if (key == QLatin1String("female"))
        return QCoreApplication::translate("GeneralOverview", "Female");
    if (key == QLatin1String("neuter"))
        return QCoreApplication::translate("GeneralOverview", "Neuter");
    if (key == QLatin1String("sexless"))
        return QCoreApplication::translate("GeneralOverview", "Sexless");
    return QCoreApplication::translate("GeneralOverview", "NoGender");
}

QString kingdomDisplayOf(const QString &kingdoms)
{
    if (!Sanguosha)
        return kingdoms;
    QStringList labels;
    const QStringList keys = kingdoms.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    for (const QString &key : keys)
        labels << Sanguosha->translate(key);
    return labels.join(QLatin1Char('/'));
}

QString aliasedGeneralName(const QString &name)
{
    if (!Sanguosha)
        return name;
    const QString alias = Sanguosha->getResourceAlias(QStringLiteral("generals"), name);
    return alias.isEmpty() ? name : alias;
}

QString artworkName(const QString &name)
{
    if (!Sanguosha)
        return name;
    const General *general = Sanguosha->getGeneral(name);
    if (general && !general->getImage().isEmpty())
        return general->getImage();
    return aliasedGeneralName(name);
}

bool globMatch(const QString &pattern, const QString &text)
{
    if (pattern.isEmpty())
        return true;
    QString rx = QRegularExpression::escape(pattern);
    rx.replace(QStringLiteral("\\?"), QStringLiteral("."));
    rx.replace(QStringLiteral("\\*"), QStringLiteral(".*"));
    return QRegularExpression(QRegularExpression::anchoredPattern(rx)).match(text).hasMatch();
}

void appendLine(QVariantList &lines, const QString &title, const QString &text,
                const QString &audio, bool enabled)
{
    QVariantMap item;
    item.insert(QStringLiteral("title"), title);
    item.insert(QStringLiteral("text"), text);
    item.insert(QStringLiteral("audio"), audio);
    item.insert(QStringLiteral("enabled"), enabled);
    lines.append(item);
}

QString overviewTr(const char *source)
{
    return QCoreApplication::translate("GeneralOverview", source);
}

QString missingLine()
{
    return overviewTr("Translation missing.");
}

QString translateSkillTag(const char *key)
{
    if (!Sanguosha)
        return {};
    const QString rawKey = QString::fromUtf8(key);
    QString text = Sanguosha->translate(rawKey);
    if (text.isEmpty() || text == rawKey)
        return {};
    if (text.endsWith(QStringLiteral("，")) || text.endsWith(QLatin1Char(',')))
        text.chop(1);
    return text;
}

QStringList skillTagLabels(const Skill *skill)
{
    QStringList tags;
    if (!skill || !Sanguosha)
        return tags;

    auto add = [&tags](const char *key) {
        const QString label = translateSkillTag(key);
        if (!label.isEmpty() && !tags.contains(label))
            tags << label;
    };

    if (skill->isLordSkill() || skill->isAttachedLordSkill())
        add("lordskill");
    if (skill->isChangeSkill())
        add("changeskill");
    if (skill->isHideSkill())
        add("hideskill");
    if (skill->isShiMingSkill())
        add("shimingskill");
    if (skill->isLimitedSkill())
        add("limitedskill");

    switch (skill->getFrequency()) {
    case Skill::Compulsory:
        add("compulsoryskill");
        break;
    case Skill::Wake:
        add("wakeskill");
        break;
    case Skill::Club:
        if (!tags.contains(QStringLiteral("阵法技")))
            tags << QStringLiteral("阵法技");
        break;
    default:
        break;
    }
    return tags;
}

QString informationOf(const QString &generalName)
{
    if (!Sanguosha)
        return {};
    QString info = Sanguosha->translate("information:" + generalName);
    if (info.contains(QStringLiteral("information:")) && generalName.contains(QLatin1Char('_')))
        info = Sanguosha->translate("information:" + generalName.split(QLatin1Char('_')).last());
    if (info.contains(QStringLiteral("information:")))
        return {};
    return info;
}

void appendSkillLines(QVariantList &lines, const Skill *skill, const QString &generalName, int skinIndex)
{
    if (!skill || !Sanguosha)
        return;

    QStringList sources = skill->getSources();
    bool hasFiles = false;
    if (skinIndex > 0) {
        const QString actualGn = Sanguosha->getResourceAlias(QStringLiteral("heroskin"), generalName);
        const QStringList skinSources = skill->getSources(actualGn, skinIndex);
        if (!skinSources.isEmpty()) {
            sources = skinSources;
            hasFiles = true;
        }
    }

    const QString skillName = Sanguosha->translate(skill->objectName());
    if (sources.isEmpty()) {
        const QString aliasSkill = Sanguosha->getResourceAlias(QStringLiteral("audios"), skill->objectName());
        if (aliasSkill != skill->objectName()) {
            if (const Skill *aliasSk = Sanguosha->getSkill(aliasSkill))
                sources = aliasSk->getSources();
        }
    }

    if (sources.isEmpty()) {
        bool has = false;
        for (int i = 1; i < 99; ++i) {
            const QString skillLine = Sanguosha->translate(
                QStringLiteral("$%1%2").arg(skill->objectName()).arg(i));
            if (skillLine.startsWith(QLatin1Char('$')))
                break;
            appendLine(lines, skillName + QStringLiteral(" (%1)").arg(i), skillLine, {}, false);
            has = true;
        }
        if (!has) {
            QString skillLine = Sanguosha->translate("$" + skill->objectName());
            if (skillLine.startsWith(QLatin1Char('$')))
                skillLine.clear();
            appendLine(lines, skillName, skillLine, {}, false);
        }
        return;
    }

    if (hasFiles) {
        for (int i = 0; i < sources.size(); ++i) {
            QString source = sources.at(i);
            source.chop(4);
            const QString filename = QStringLiteral("%1-%2_%3").arg(source, generalName).arg(skinIndex);
            QString title = skillName;
            if (sources.size() > 1)
                title.append(QStringLiteral(" (%1)").arg(i + 1));
            QString skillLine = Sanguosha->translate("$" + filename);
            if (skillLine == "$" + filename)
                skillLine = missingLine();
            appendLine(lines, title, skillLine, sources.at(i), QFile::exists(sources.at(i)));
        }
        return;
    }

    static const QRegularExpression oggRx(QStringLiteral(".+/(\\w+\\d?)\\.(?:ogg|wav)$"));
    for (int i = 0; i < sources.size(); ++i) {
        const QRegularExpressionMatch match = oggRx.match(sources.at(i));
        if (!match.hasMatch())
            continue;
        QString title = skillName;
        if (sources.size() > 1)
            title.append(QStringLiteral(" (%1)").arg(i + 1));
        const QString filename = match.captured(1);
        QString skillLine = Sanguosha->translate("$" + filename);
        if (skillLine == "$" + filename)
            skillLine = missingLine();
        appendLine(lines, title, skillLine, sources.at(i), QFile::exists(sources.at(i)));
    }
}

void appendCardAudioLines(QVariantList &lines, const QString &generalName, int skinIndex)
{
    if (!Sanguosha)
        return;

    QString actualGn = Sanguosha->getResourceAlias(QStringLiteral("heroskin"), generalName);
    QString cardAudioGn = Sanguosha->getResourceAlias(QStringLiteral("card_audio"), generalName);
    if (cardAudioGn == generalName)
        cardAudioGn = actualGn;

    QStringList foundCardAudios;
    auto collect = [&foundCardAudios](const QString &dirPath) {
        if (!QFile::exists(dirPath))
            return;
        const QDir dir(dirPath);
        const QStringList files = dir.entryList(
            QStringList() << QStringLiteral("*.ogg") << QStringLiteral("*.wav"),
            QDir::Files | QDir::Readable, QDir::Name);
        for (QString file : files) {
            file.chop(4);
            if (!foundCardAudios.contains(file))
                foundCardAudios << file;
        }
    };

    if (skinIndex > 0)
        collect(QStringLiteral("hero-skin/%1/%2/card").arg(cardAudioGn).arg(skinIndex));
    collect(QStringLiteral("audio/card/%1").arg(cardAudioGn));

    for (const QString &cardName : foundCardAudios) {
        QString audioPath;
        if (skinIndex > 0) {
            const QString heroDir = QStringLiteral("hero-skin/%1/%2/card").arg(cardAudioGn).arg(skinIndex);
            if (QFile::exists(heroDir + "/" + cardName + ".ogg"))
                audioPath = heroDir + "/" + cardName + ".ogg";
            else if (QFile::exists(heroDir + "/" + cardName + ".wav"))
                audioPath = heroDir + "/" + cardName + ".wav";
        }
        if (audioPath.isEmpty()) {
            const QString nativeDir = QStringLiteral("audio/card/%1").arg(cardAudioGn);
            if (QFile::exists(nativeDir + "/" + cardName + ".ogg"))
                audioPath = nativeDir + "/" + cardName + ".ogg";
            else if (QFile::exists(nativeDir + "/" + cardName + ".wav"))
                audioPath = nativeDir + "/" + cardName + ".wav";
        }

        QString lineKey;
        QString cardLine;
        if (skinIndex > 0) {
            lineKey = QStringLiteral("$%1-%2_%3").arg(cardName, actualGn).arg(skinIndex);
            cardLine = Sanguosha->translate(lineKey);
        }
        if (cardLine.isEmpty() || cardLine == lineKey) {
            lineKey = QStringLiteral("$%1-%2").arg(cardName, actualGn);
            cardLine = Sanguosha->translate(lineKey);
        }
        if (cardLine.isEmpty() || cardLine == lineKey) {
            lineKey = QStringLiteral("$%1").arg(cardName);
            cardLine = Sanguosha->translate(lineKey);
        }
        if (cardLine.isEmpty() || cardLine == lineKey)
            cardLine = missingLine();

        appendLine(lines, Sanguosha->translate(cardName), cardLine, audioPath, !audioPath.isEmpty());
    }
}

} // namespace

HomeGeneralModel::HomeGeneralModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int HomeGeneralModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_shown.size();
}

int HomeGeneralModel::count() const
{
    return m_shown.size();
}

bool HomeGeneralModel::isLoaded() const
{
    return m_loaded;
}

QHash<int, QByteArray> HomeGeneralModel::roleNames() const
{
    return {
        { NameRole, "name" },
        { DisplayNameRole, "displayName" },
        { NicknameRole, "nickname" },
        { KingdomRole, "kingdom" },
        { KingdomsRole, "kingdoms" },
        { GenderRole, "gender" },
        { GenderDisplayRole, "genderDisplay" },
        { KingdomDisplayRole, "kingdomDisplay" },
        { MaxHpRole, "maxHp" },
        { StartHpRole, "startHp" },
        { PackageRole, "package" },
        { PackageNameRole, "packageName" },
        { HiddenRole, "hidden" },
        { LordRole, "lord" }
    };
}

QVariant HomeGeneralModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_shown.size())
        return {};
    const Row &row = m_all.at(m_shown.at(index.row()));
    switch (role) {
    case NameRole: return row.name;
    case DisplayNameRole: return row.displayName;
    case NicknameRole: return row.nickname;
    case KingdomRole: return row.kingdom;
    case KingdomsRole: return row.kingdoms;
    case GenderRole: return row.gender;
    case GenderDisplayRole: return row.genderDisplay;
    case KingdomDisplayRole: return row.kingdomDisplay;
    case MaxHpRole: return row.maxHp;
    case StartHpRole: return row.startHp;
    case PackageRole: return row.package;
    case PackageNameRole: return row.packageName;
    case HiddenRole: return row.hidden;
    case LordRole: return row.lord;
    default: return {};
    }
}

void HomeGeneralModel::ensureLoaded()
{
    if (m_loaded || !Sanguosha)
        return;

    const QList<const General *> list = Sanguosha->getAllGenerals();
    m_all.clear();
    m_all.reserve(list.size());
    for (const General *general : list) {
        if (!general || general->isTotallyHidden())
            continue;
        Row row;
        row.name = general->objectName();
        row.displayName = Sanguosha->translate(row.name);
        row.nickname = nicknameOf(row.name);
        row.kingdom = general->getKingdom();
        row.kingdoms = general->getKingdoms();
        row.gender = genderKeyOf(general);
        row.genderDisplay = genderDisplayOf(row.gender);
        row.kingdomDisplay = kingdomDisplayOf(row.kingdoms);
        row.maxHp = general->getMaxHp();
        row.startHp = qMin(general->getStartHp(), row.maxHp);
        row.package = general->getPackage();
        row.packageName = Sanguosha->translate(general->getPackage());
        row.hidden = Sanguosha->isGeneralHidden(row.name) || general->isHidden();
        row.lord = general->isLord();
        m_all.append(row);
    }
    m_loaded = true;
}

void HomeGeneralModel::applyFilter(const QVariantMap &filters)
{
    ensureLoaded();

    const QString kingdom = filters.value(QStringLiteral("kingdom")).toString();
    const QString search = filters.value(QStringLiteral("search")).toString().trimmed().toLower();
    const QString nickname = filters.value(QStringLiteral("nickname")).toString().trimmed();
    const bool includeHidden = filters.value(QStringLiteral("includeHidden"), true).toBool();
    const int hpMin = filters.value(QStringLiteral("hpMin"), 0).toInt();
    const int hpMax = filters.value(QStringLiteral("hpMax"), 0).toInt();
    const QStringList genders = [&filters]() {
        const QVariant value = filters.value(QStringLiteral("genders"));
        if (value.userType() == QMetaType::QStringList)
            return value.toStringList();
        QStringList out;
        for (const QVariant &item : value.toList())
            out << item.toString();
        return out;
    }();
    const QStringList packages = [&filters]() {
        const QVariant value = filters.value(QStringLiteral("packages"));
        if (value.userType() == QMetaType::QStringList)
            return value.toStringList();
        QStringList out;
        for (const QVariant &item : value.toList())
            out << item.toString();
        return out;
    }();
    const bool allKingdom = kingdom.isEmpty() || kingdom == QLatin1String("all");

    QVector<int> next;
    next.reserve(m_all.size());
    for (int i = 0; i < m_all.size(); ++i) {
        const Row &row = m_all.at(i);
        if (!includeHidden && row.hidden)
            continue;
        if (!allKingdom && !row.kingdoms.split(QLatin1Char('+')).contains(kingdom))
            continue;
        if (!search.isEmpty()) {
            const QString haystack = (row.displayName + QLatin1Char(' ') + row.nickname
                                      + QLatin1Char(' ') + row.name + QLatin1Char(' ')
                                      + row.packageName).toLower();
            if (!haystack.contains(search))
                continue;
        }
        if (!globMatch(nickname, row.nickname))
            continue;
        if (!genders.isEmpty()) {
            bool ok = genders.contains(row.gender);
            if (!ok && genders.contains(QStringLiteral("nogender"))
                    && (row.gender == QLatin1String("neuter")
                        || row.gender == QLatin1String("sexless")))
                ok = true;
            if (!ok)
                continue;
        }
        if (!(hpMin == 0 && hpMax == 0) && (row.maxHp < hpMin || row.maxHp > hpMax))
            continue;
        if (!packages.isEmpty() && !packages.contains(row.package))
            continue;
        next.append(i);
    }

    beginResetModel();
    m_shown = std::move(next);
    endResetModel();
    emit filterChanged();
}

bool HomeGeneralModel::containsName(const QString &name) const
{
    for (int idx : m_shown) {
        if (m_all.at(idx).name == name)
            return true;
    }
    return false;
}

QString HomeGeneralModel::nameAt(int row) const
{
    if (row < 0 || row >= m_shown.size())
        return {};
    return m_all.at(m_shown.at(row)).name;
}

int HomeGeneralModel::indexOfName(const QString &name) const
{
    for (int i = 0; i < m_shown.size(); ++i) {
        if (m_all.at(m_shown.at(i)).name == name)
            return i;
    }
    return -1;
}

HomeController::HomeController(QObject *parent)
    : QObject(parent)
{
}

QString HomeController::version() const
{
    return Sanguosha
        ? Sanguosha->getVersionNumber()
        : QString();
}

bool HomeController::updateAvailable() const
{
    return m_updateAvailable;
}

QUrl HomeController::backgroundImage() const
{
    const QString &path = Config.BackgroundImage;
    if (path.isEmpty())
        return QUrl();

    if (QDir::isAbsolutePath(path))
        return QUrl::fromLocalFile(path);

    return QUrl::fromLocalFile(
        QDir::current().absoluteFilePath(path));
}

QUrl HomeController::characterImage() const
{
    const QString absPath = QDir::current().absoluteFilePath(
        QStringLiteral("image/home/character.png"));

    if (!QFile::exists(absPath))
        return QUrl();

    QUrl url = QUrl::fromLocalFile(absPath);
    url.setQuery(QStringLiteral("v=%1").arg(m_characterVersion));
    return url;
}

QUrl HomeController::logoImage() const
{
    const QString absPath = QDir::current().absoluteFilePath(
        QStringLiteral("image/logo/logo.png"));
    if (!QFile::exists(absPath))
        return QUrl();
    return QUrl::fromLocalFile(absPath);
}

QString HomeController::playerName() const
{
    return Config.UserName;
}

QUrl HomeController::playerAvatar() const
{
    if (Config.UserAvatar.isEmpty())
        return {};
    return generalFullImage(Config.UserAvatar);
}

QString HomeController::currentGameModeName() const
{
    if (!Sanguosha || !Config.GameMode.isValid())
        return {};
    return Sanguosha->getModeName(Config.GameMode.mode_id);
}

void HomeController::refreshPlayerInfo()
{
    emit playerInfoChanged();
    emit gameModeChanged();
}

bool HomeController::hasVideoSupport() const
{
#ifdef HAS_QT_MULTIMEDIA
    static const bool backendAvailable = []() {
        const QStringList pluginPaths = QCoreApplication::libraryPaths();
        for (const QString &base : pluginPaths) {
            QDir dir(QStringLiteral("%1/multimedia").arg(base));
            if (!dir.exists())
                continue;
            const QStringList plugins =
                dir.entryList(QStringList() << QStringLiteral("*.dll"), QDir::Files);
            for (const QString &plugin : plugins) {
                if (plugin.contains(QStringLiteral("mediaplugin"), Qt::CaseInsensitive))
                    return true;
            }
        }
        return false;
    }();
    return backendAvailable;
#else
    return false;
#endif
}

void HomeController::quickJoin()
{
    emit quickJoinRequested();
}

void HomeController::joinGame()
{
    emit joinGameRequested();
}

void HomeController::startServer()
{
    emit startServerRequested();
}

void HomeController::switchQmlScene(const QUrl &source)
{
    QObject *window = parent();
    QPointer<QQuickWidget> view = window ? window->findChild<QQuickWidget *>() : nullptr;
    if (!view) {
        emit qmlSceneRequested(source);
        return;
    }

    // QML onClicked 仍在堆疊上時不可同步 setSource（會銷毀呼叫端 root item）。
    QTimer::singleShot(0, this, [this, view, source]() {
        if (!view)
            return;
        view->setSource(source);
        view->setFocus();
        emit qmlSceneRequested(source);
    });
}

QString HomeController::currentPage() const
{
    return m_currentPage;
}

void HomeController::setCurrentPage(const QString &page)
{
    if (m_currentPage == page)
        return;
    m_currentPage = page;
    emit currentPageChanged();
}

void HomeController::openHome()
{
    setCurrentPage(QStringLiteral("home"));
}

void HomeController::openGenerals()
{
    // 先切頁讓 HomeScene 立刻畫出 skeleton；目錄在下一幀才載入。
    setCurrentPage(QStringLiteral("generals"));
}

HomeGeneralModel *HomeController::generalModel()
{
    return &m_generalModel;
}

void HomeController::applyGeneralFilter(const QVariantMap &filters)
{
    m_generalModel.applyFilter(filters);
}

void HomeController::warmGeneralCatalog()
{
    if (m_generalModel.isLoaded())
        return;

    QVariantMap filters;
    filters.insert(QStringLiteral("kingdom"), QStringLiteral("all"));
    filters.insert(QStringLiteral("includeHidden"), true);
    m_generalModel.applyFilter(filters);
}

QUrl HomeController::prefetchArtUrl(int index) const
{
    return generalFullImage(m_generalModel.nameAt(index));
}

void HomeController::openCards()
{
    emit cardsRequested();
}

void HomeController::openReplays()
{
    emit replaysRequested();
}

void HomeController::openSettings()
{
    emit settingsRequested();
}

void HomeController::openAbout()
{
    emit aboutRequested();
}

void HomeController::checkUpdates()
{
    emit updateCheckRequested();
}

QString HomeController::translate(const QString &key) const
{
    return Sanguosha ? Sanguosha->translate(key) : key;
}

QString HomeController::qtTranslate(const QString &context, const QString &source) const
{
    const QByteArray contextUtf8 = context.toUtf8();
    const QByteArray sourceUtf8 = source.toUtf8();
    return QCoreApplication::translate(contextUtf8.constData(), sourceUtf8.constData());
}

QString HomeController::kingdomColor(const QString &kingdom) const
{
    return Sanguosha ? Sanguosha->getKingdomColor(kingdom) : QString();
}

QVariantList HomeController::kingdoms() const
{
    QVariantList result;
    if (!Sanguosha)
        return result;

    const QStringList list = Sanguosha->getKingdoms();
    for (const QString &kingdom : list) {
        QVariantMap item;
        item.insert(QStringLiteral("key"), kingdom);
        item.insert(QStringLiteral("label"), Sanguosha->translate(kingdom));
        item.insert(QStringLiteral("color"), Sanguosha->getKingdomColor(kingdom));
        item.insert(QStringLiteral("icon"), kingdomIcon(kingdom));
        result.append(item);
    }
    return result;
}

QUrl HomeController::kingdomIcon(const QString &kingdom) const
{
    if (kingdom.isEmpty())
        return {};
    return firstExistingImage({
        QStringLiteral("image/kingdom/icon/%1").arg(kingdom)
    });
}

static QUrl taggedArtUrl(const QUrl &url, const QString &cacheKey, int revision)
{
    if (url.isEmpty())
        return url;
    QUrl tagged = url;
    tagged.setQuery(QStringLiteral("k=%1&r=%2").arg(cacheKey).arg(revision));
    return tagged;
}

static QUrl skinFullUrl(const QString &generalName, int skinIndex)
{
    QUrl url;
    if (skinIndex > 0 && Sanguosha) {
        const QString skinGn = Sanguosha->getResourceAlias(QStringLiteral("heroskin"), generalName);
        url = firstExistingImage({
            QStringLiteral("hero-skin/%1/%2/full").arg(skinGn).arg(skinIndex),
            QStringLiteral("hero-skin/%1/%2/card").arg(skinGn).arg(skinIndex),
            QStringLiteral("hero-skin/%1/%2/full").arg(generalName).arg(skinIndex),
            QStringLiteral("hero-skin/%1/%2/card").arg(generalName).arg(skinIndex),
            QStringLiteral("image/heroskin/fullskin/generals/full/%1_%2").arg(skinGn).arg(skinIndex),
            QStringLiteral("image/heroskin/fullskin/generals/full/%1_%2").arg(generalName).arg(skinIndex)
        });
    }
    if (url.isEmpty()) {
        const QString actual = artworkName(generalName);
        url = firstExistingImage({
            QStringLiteral("image/fullskin/generals/full/%1").arg(actual),
            QStringLiteral("image/fullskin/generals/full/%1").arg(generalName)
        });
    }
    return url;
}

QUrl HomeController::generalCardImage(const QString &generalName) const
{
    if (generalName.isEmpty())
        return {};
    const int skinIndex = Config.value("HeroSkin/" + generalName, 0).toInt();
    const QString cacheKey = QStringLiteral("%1#%2").arg(generalName).arg(skinIndex);
    if (m_cardImageCache.contains(cacheKey))
        return taggedArtUrl(m_cardImageCache.value(cacheKey), cacheKey, m_artRevision);

    QUrl url;
    if (skinIndex > 0 && Sanguosha) {
        const QString skinGn = Sanguosha->getResourceAlias(QStringLiteral("heroskin"), generalName);
        url = firstExistingImage({
            QStringLiteral("hero-skin/%1/%2/card").arg(skinGn).arg(skinIndex),
            QStringLiteral("hero-skin/%1/%2/card").arg(generalName).arg(skinIndex),
            QStringLiteral("image/heroskin/fullskin/generals/card/%1_%2").arg(skinGn).arg(skinIndex),
            QStringLiteral("image/heroskin/fullskin/generals/card/%1_%2").arg(generalName).arg(skinIndex)
        });
    }
    if (url.isEmpty()) {
        const QString actual = artworkName(generalName);
        url = firstExistingImage({
            QStringLiteral("image/general/card/%1").arg(actual),
            QStringLiteral("image/generals/card/%1").arg(actual),
            QStringLiteral("image/card/%1").arg(actual)
        });
    }
    m_cardImageCache.insert(cacheKey, url);
    return taggedArtUrl(url, cacheKey, m_artRevision);
}

QUrl HomeController::generalFullImage(const QString &generalName) const
{
    if (generalName.isEmpty())
        return {};
    const int skinIndex = Config.value("HeroSkin/" + generalName, 0).toInt();
    const QString cacheKey = QStringLiteral("%1#%2").arg(generalName).arg(skinIndex);
    if (m_fullImageCache.contains(cacheKey))
        return taggedArtUrl(m_fullImageCache.value(cacheKey), cacheKey, m_artRevision);

    QUrl url = skinFullUrl(generalName, skinIndex);
    m_fullImageCache.insert(cacheKey, url);
    return taggedArtUrl(url, cacheKey, m_artRevision);
}

QUrl HomeController::magatamaImage(int index) const
{
    const int clamped = qBound(0, index, 5);
    return QUrl::fromLocalFile(QDir::current().absoluteFilePath(
        QStringLiteral("image/system/magatamas/%1.png").arg(clamped)));
}

QUrl HomeController::hujiaImage() const
{
    return firstExistingImage({
        QStringLiteral("image/mark/@HuJia"),
        QStringLiteral("image/mark/@default")
    });
}

QUrl HomeController::lordIcon() const
{
    return QUrl::fromLocalFile(QDir::current().absoluteFilePath(
        QStringLiteral("image/system/roles/lord.png")));
}

QUrl HomeController::navButtonImage(const QString &name) const
{
    if (name.isEmpty())
        return {};
    return firstExistingImage({
        QStringLiteral("qml/home/nav/%1").arg(name)
    });
}

qreal HomeController::generalOverlayLuma(const QString &generalName) const
{
    const QUrl url = generalFullImage(generalName);
    if (!url.isLocalFile())
        return 0.5;

    QImageReader reader(url.toLocalFile());
    const QSize sz = reader.size();
    if (!sz.isValid() || sz.width() <= 0 || sz.height() <= 0)
        return 0.5;

    const int scaledW = 48;
    const int scaledH = qMax(8, scaledW * sz.height() / sz.width());
    reader.setScaledSize(QSize(scaledW, scaledH));
    const QImage img = reader.read();
    if (img.isNull() || img.width() <= 0 || img.height() <= 0)
        return 0.5;

    const int y0 = img.height() * 55 / 100;
    qint64 sum = 0;
    int n = 0;
    for (int y = y0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = img.pixel(x, y);
            if (qAlpha(px) < 40)
                continue;
            sum += qRed(px) * 299 + qGreen(px) * 587 + qBlue(px) * 114;
            ++n;
        }
    }
    if (n <= 0)
        return 0.5;
    return (sum / 1000.0 / n) / 255.0;
}

void HomeController::playAudio(const QString &path) const
{
    if (Sanguosha && !path.isEmpty())
        Sanguosha->playAudioEffect(path, false);
}

QVariantList HomeController::generalPackages() const
{
    QVariantList result;
    if (!Sanguosha)
        return result;

    const QStringList extensions = Sanguosha->getExtensions();
    for (const QString &extension : extensions) {
        const Package *package = Sanguosha->getPackage(extension);
        if (!package || package->getType() != Package::GeneralPack)
            continue;
        QVariantMap item;
        item.insert(QStringLiteral("key"), package->objectName());
        item.insert(QStringLiteral("label"), Sanguosha->translate(package->objectName()));
        result.append(item);
    }
    return result;
}

QVariantList HomeController::generals() const
{
    QVariantList result;
    if (!Sanguosha)
        return result;

    const QList<const General *> list = Sanguosha->getAllGenerals();
    for (const General *general : list) {
        if (!general || general->isTotallyHidden())
            continue;

        QVariantMap item;
        const QString name = general->objectName();
        item.insert(QStringLiteral("name"), name);
        item.insert(QStringLiteral("displayName"), Sanguosha->translate(name));
        item.insert(QStringLiteral("nickname"), nicknameOf(name));
        item.insert(QStringLiteral("kingdom"), general->getKingdom());
        item.insert(QStringLiteral("kingdoms"), general->getKingdoms());
        item.insert(QStringLiteral("gender"), genderKeyOf(general));
        item.insert(QStringLiteral("maxHp"), general->getMaxHp());
        item.insert(QStringLiteral("package"), general->getPackage());
        item.insert(QStringLiteral("packageName"), Sanguosha->translate(general->getPackage()));
        item.insert(QStringLiteral("hidden"), Sanguosha->isGeneralHidden(name) || general->isHidden());
        item.insert(QStringLiteral("lord"), general->isLord());
        result.append(item);
    }
    return result;
}

QVariantMap HomeController::generalDetails(const QString &generalName) const
{
    QVariantMap result;
    if (!Sanguosha)
        return result;

    const General *general = Sanguosha->getGeneral(generalName);
    if (!general)
        return result;

    const int skinIndex = Config.value("HeroSkin/" + generalName, 0).toInt();

    result.insert(QStringLiteral("name"), generalName);
    result.insert(QStringLiteral("displayName"), Sanguosha->translate(generalName));
    result.insert(QStringLiteral("hasSkin"), HeroSkinContainer::hasSkin(generalName));
    result.insert(QStringLiteral("banned"),
                  Config.value(QStringLiteral("Banlist/Roles")).toStringList().contains(generalName));
    result.insert(QStringLiteral("isAvatar"), Config.UserAvatar == generalName);
    result.insert(QStringLiteral("skinIndex"), skinIndex);
    result.insert(QStringLiteral("nickname"), nicknameOf(generalName));
    result.insert(QStringLiteral("lord"), general->isLord());
    result.insert(QStringLiteral("hidden"),
                  Sanguosha->isGeneralHidden(generalName) || general->isHidden());
    result.insert(QStringLiteral("kingdom"), general->getKingdom());
    result.insert(QStringLiteral("kingdoms"), general->getKingdoms());
    const int maxHp = general->getMaxHp();
    result.insert(QStringLiteral("maxHp"), maxHp);
    result.insert(QStringLiteral("startHp"), qMin(general->getStartHp(), maxHp));
    result.insert(QStringLiteral("startHujia"), general->getStartHujia());
    result.insert(QStringLiteral("package"), Sanguosha->translate(general->getPackage()));
    result.insert(QStringLiteral("companions"), general->getCompanions());

    QString mapping;
    if (!general->getImage().isEmpty() && general->getImage() != generalName)
        mapping = Sanguosha->translate(general->getImage());
    else {
        const QString alias = Sanguosha->getResourceAlias(QStringLiteral("generals"), generalName);
        if (alias != generalName)
            mapping = Sanguosha->translate(alias);
    }
    result.insert(QStringLiteral("mapping"), mapping);

    QString designer = Sanguosha->translate("designer:" + generalName);
    if (designer.contains(QStringLiteral("designer:")))
        designer = overviewTr("Official");
    result.insert(QStringLiteral("designer"), designer);

    QString cv = Sanguosha->translate("cv:" + generalName);
    if (cv.contains(QStringLiteral("cv:")))
        cv = Sanguosha->translate("cv:" + generalName.split(QLatin1Char('_')).last());
    if (cv.contains(QStringLiteral("cv:")))
        cv = overviewTr("Official");
    result.insert(QStringLiteral("cv"), cv);

    QString illustrator;
    if (skinIndex > 0) {
        illustrator = Sanguosha->translate(
            QStringLiteral("illustrator:%1_%2").arg(generalName).arg(skinIndex));
        if (illustrator.startsWith(QStringLiteral("illustrator:")))
            illustrator.clear();
    }
    if (illustrator.isEmpty()) {
        illustrator = Sanguosha->translate("illustrator:" + generalName);
        if (illustrator.startsWith(QStringLiteral("illustrator:")))
            illustrator = Sanguosha->translate(QStringLiteral("DefaultIllustrator"));
    }
    result.insert(QStringLiteral("illustrator"), illustrator);

    result.insert(QStringLiteral("information"), informationOf(generalName));
    result.insert(QStringLiteral("oracleText"), general->getOracleText());

    QList<const Skill *> skills = general->getVisibleSkillList();
    QSet<QString> nativeSkillNames;
    for (const Skill *skill : skills) {
        if (skill)
            nativeSkillNames.insert(skill->objectName());
    }
    for (const Skill *skill : skills) {
        if (!skill || skill->getWakedSkills().isEmpty())
            continue;
        const QStringList waked = skill->getWakedSkills().split(QLatin1Char(','));
        for (const QString &skn : waked) {
            const Skill *sk = Sanguosha->getSkill(skn);
            if (sk && sk->isVisible() && !skills.contains(sk))
                skills << sk;
        }
    }
    for (const QString &skn : general->getRelatedSkillNames()) {
        const Skill *skill = Sanguosha->getSkill(skn);
        if (skill && skill->isVisible() && !skills.contains(skill))
            skills << skill;
    }

    QVariantList skillItems;
    for (const Skill *skill : skills) {
        if (!skill)
            continue;
        QVariantMap item;
        item.insert(QStringLiteral("name"), skill->objectName());
        item.insert(QStringLiteral("displayName"), Sanguosha->translate(skill->objectName()));
        item.insert(QStringLiteral("description"), skill->getDescription());
        item.insert(QStringLiteral("oracleText"), skill->getOracleText());
        item.insert(QStringLiteral("tags"), skillTagLabels(skill));
        item.insert(QStringLiteral("related"),
                    !nativeSkillNames.contains(skill->objectName()));
        skillItems.append(item);
    }
    result.insert(QStringLiteral("skills"), skillItems);

    QVariantList lines;
    for (const Skill *skill : skills)
        appendSkillLines(lines, skill, generalName, skinIndex);
    appendCardAudioLines(lines, generalName, skinIndex);

    QString oggPath = QStringLiteral("audio/death/%1.ogg").arg(generalName);
    QString lastWord = Sanguosha->translate("~" + generalName);
    const QString actualGn = Sanguosha->getResourceAlias(QStringLiteral("heroskin"), generalName);
    const QString aliased = Sanguosha->getResourceAlias(QStringLiteral("generals"), generalName);
    if (aliased != generalName) {
        const QString aliasOgg = QStringLiteral("audio/death/%1.ogg").arg(aliased);
        const QString aliasWord = Sanguosha->translate("~" + aliased);
        if (QFile::exists(aliasOgg) || !aliasWord.startsWith(QLatin1Char('~'))) {
            oggPath = aliasOgg;
            lastWord = aliasWord;
        }
    }
    if (skinIndex > 0) {
        const QString heroSkin = Sanguosha->translate(
            QStringLiteral("~%1-%2_%3").arg(actualGn, actualGn).arg(skinIndex));
        if (!heroSkin.startsWith(QLatin1Char('~'))) {
            oggPath = QStringLiteral("hero-skin/%1/%2/death.ogg").arg(actualGn).arg(skinIndex);
            lastWord = heroSkin;
        }
    }
    if (lastWord.startsWith(QLatin1Char('~')) && generalName.contains(QLatin1Char('_'))) {
        const QString shortName = generalName.split(QLatin1Char('_')).last();
        oggPath = QStringLiteral("audio/death/%1.ogg").arg(shortName);
        lastWord = Sanguosha->translate("~" + shortName);
    }
    if (lastWord.startsWith(QLatin1Char('~')))
        lastWord.clear();
    else if (lastWord == QLatin1String(" "))
        lastWord = missingLine();
    appendLine(lines, overviewTr("Death"), lastWord, oggPath, QFile::exists(oggPath));

    const QString winPath = QStringLiteral("audio/win/%1.ogg").arg(generalName);
    if (QFile::exists(winPath)) {
        appendLine(lines, overviewTr("Victory"),
                   Sanguosha->translate("$" + generalName), winPath, true);
    } else if (generalName.contains(QStringLiteral("caocao"))) {
        appendLine(lines, overviewTr("Victory"),
                   overviewTr("Six dragons lead my chariot, "
                              "I will ride the wind with the greatest speed."
                              "With all of the feudal lords under my command,"
                              "to rule the world with one name!"),
                   QStringLiteral("audio/win/caocao.ogg"),
                   QFile::exists(QStringLiteral("audio/win/caocao.ogg")));
    }

    if (generalName == QLatin1String("shenlvbu1")
            || generalName == QLatin1String("shenlvbu2")
            || generalName == QLatin1String("shenlvbu3")) {
        appendLine(lines, overviewTr("Stage Change"),
                   overviewTr("Trashes, the real fun is just beginning!"),
                   QStringLiteral("audio/system/stagechange.ogg"),
                   QFile::exists(QStringLiteral("audio/system/stagechange.ogg")));
    }

    result.insert(QStringLiteral("lines"), lines);
    return result;
}

QUrl HomeController::randomBackdrop() const
{
    static const QStringList imageSuffixes = {
        QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("png"), QStringLiteral("webp"),
        QStringLiteral("bmp"), QStringLiteral("gif")
    };
    QDir dir(QStringLiteral("image/system/backdrop"));
    const QStringList files = dir.entryList(QDir::Files);
    QStringList images;
    images.reserve(files.size());
    for (const QString &file : files) {
        const QString suffix = QFileInfo(file).suffix().toLower();
        if (imageSuffixes.contains(suffix))
            images << file;
    }
    if (images.isEmpty())
        return QUrl();

    const int index = QRandomGenerator::global()->bounded(images.size());
    return QUrl::fromLocalFile(dir.absoluteFilePath(images.at(index)));
}

void HomeController::refreshCharacterImage()
{
    ++m_characterVersion;
    emit characterImageChanged();
}

qreal HomeController::uiScale() const
{
    return Config.UIScale;
}

QString HomeController::visualMode() const
{
    return Config.VisualMode;
}

void HomeController::notifyVisualSettings()
{
    emit visualSettingsChanged();
}

bool HomeController::isDarkTheme() const
{
    const int scheme = Config.ColorScheme;
    if (scheme == 2)
        return true;
    if (scheme == 1)
        return false;
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

void HomeController::toggleTheme()
{
    const int next = isDarkTheme() ? 1 : 2;
    Config.ColorScheme = next;
    Config.setValue("ColorScheme", next);
    applyColorScheme(next);
    emit themeChanged();
}

int HomeController::artRevision() const
{
    return m_artRevision;
}

void HomeController::setHeroSkin(const QString &generalName, int skinIndex)
{
    if (generalName.isEmpty())
        return;

    Config.beginGroup("HeroSkin");
    if (skinIndex <= 0)
        Config.remove(generalName);
    else
        Config.setValue(generalName, skinIndex);
    Config.endGroup();

    if (skinIndex > 0 && Sanguosha) {
        const General *general = Sanguosha->getGeneral(generalName);
        if (general)
            general->tryLoadingSkinTranslation(skinIndex);
    }

    ++m_artRevision;
    emit artRevisionChanged();
    if (Config.UserAvatar == generalName)
        emit playerInfoChanged();
}

QVariantList HomeController::heroSkinList(const QString &generalName) const
{
    QVariantList result;
    if (generalName.isEmpty() || !HeroSkinContainer::hasSkin(generalName))
        return result;

    const int current = Config.value("HeroSkin/" + generalName, 0).toInt();
    QList<int> indices;
    indices << 0;
    for (int index : HeroSkinContainer::getAvailableSkinIndices(generalName)) {
        if (!indices.contains(index))
            indices << index;
    }

    for (int index : indices) {
        QVariantMap item;
        item.insert(QStringLiteral("index"), index);
        item.insert(QStringLiteral("current"), index == current);
        item.insert(QStringLiteral("image"),
                    taggedArtUrl(skinFullUrl(generalName, index),
                                 QStringLiteral("%1#%2").arg(generalName).arg(index),
                                 m_artRevision));
        QString label;
        if (index <= 0) {
            label = QCoreApplication::translate("GeneralOverview", "Default skin");
        } else {
            label = Sanguosha ? Sanguosha->translate(
                        QStringLiteral("illustrator:%1_%2").arg(generalName).arg(index)) : QString();
            if (label.isEmpty() || label.startsWith(QStringLiteral("illustrator:")))
                label = QString::number(index);
        }
        item.insert(QStringLiteral("label"), label);
        result.append(item);
    }
    return result;
}

void HomeController::setGeneralBanned(const QString &generalName, bool banned)
{
    if (generalName.isEmpty())
        return;

    QStringList roles = Config.value(QStringLiteral("Banlist/Roles")).toStringList();
    if (banned) {
        if (!roles.contains(generalName))
            roles << generalName;
    } else {
        roles.removeAll(generalName);
    }
    Config.setValue(QStringLiteral("Banlist/Roles"), roles);
}

void HomeController::setUserAvatar(const QString &generalName)
{
    if (generalName.isEmpty())
        return;

    Config.UserAvatar = generalName;
    Config.setValue(QStringLiteral("UserAvatar"), generalName);
    emit playerInfoChanged();
}

int HomeController::generalGridColumns() const
{
    return Config.value(QStringLiteral("Home/GeneralGridColumns"), 0).toInt();
}

void HomeController::setGeneralGridColumns(int columns)
{
    Config.setValue(QStringLiteral("Home/GeneralGridColumns"), columns);
}
