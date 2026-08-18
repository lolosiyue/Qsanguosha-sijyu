#include "homecontroller.h"
#include "engine.h"
#include "general.h"
#include "skill.h"
#include "settings.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QGuiApplication>
#include <QStyleHints>
#include <QQuickWidget>

HomeController::HomeController(QObject *parent)
    : QObject(parent)
{
}

QString HomeController::version() const
{
    return Sanguosha ? Sanguosha->getVersionNumber() : QString();
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
    return QUrl::fromLocalFile(QDir::current().absoluteFilePath(path));
}

QUrl HomeController::characterImage() const
{
    const QString absPath = QDir::current().absoluteFilePath(QStringLiteral("image/home/character.png"));
    if (QFile::exists(absPath)) {
        QUrl url = QUrl::fromLocalFile(absPath);
        url.setQuery(QStringLiteral("v=%1").arg(m_characterVersion));
        return url;
    }
    return QUrl(QStringLiteral("qrc:/QSanguosha/Home/assets/character.png"));
}

QUrl HomeController::logoImage() const
{
    return QUrl::fromLocalFile(QDir::current().absoluteFilePath(QStringLiteral("image/logo/logo.png")));
}

QString HomeController::playerName() const
{
    return Config.UserName;
}

QUrl HomeController::playerAvatar() const
{
    const QString &avatar = Config.UserAvatar;
    if (avatar.isEmpty())
        return QUrl();
    const QString absPath = QDir::current().absoluteFilePath(
        QStringLiteral("image/fullskin/generals/full/%1.jpg").arg(avatar));
    return QFile::exists(absPath) ? QUrl::fromLocalFile(absPath) : QUrl();
}

void HomeController::refreshPlayerInfo()
{
    emit playerInfoChanged();
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
            const QStringList plugins = dir.entryList(QStringList() << QStringLiteral("*.dll"), QDir::Files);
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

void HomeController::quickJoin() { emit quickJoinRequested(); }
void HomeController::joinGame() { emit joinGameRequested(); }
void HomeController::startServer() { emit startServerRequested(); }

void HomeController::switchQmlScene(const QUrl &source)
{
    // HomeController is parented by MainWindow; the home QQuickWidget is a descendant.
    // Switching its source keeps navigation inside the existing page stack instead of
    // opening another top-level QWidget dialog.
    if (QObject *window = parent()) {
        if (QQuickWidget *view = window->findChild<QQuickWidget *>()) {
            view->setSource(source);
            view->setFocus();
            emit qmlSceneRequested(source);
            return;
        }
    }
    emit qmlSceneRequested(source);
}

void HomeController::openHome()
{
    switchQmlScene(QUrl(QStringLiteral("qrc:/QSanguosha/Home/HomeScene.qml")));
}

void HomeController::openGenerals()
{
    switchQmlScene(QUrl(QStringLiteral("qrc:/QSanguosha/Home/GeneralScene.qml")));
}

void HomeController::openCards() { emit cardsRequested(); }
void HomeController::openReplays() { emit replaysRequested(); }
void HomeController::openSettings() { emit settingsRequested(); }
void HomeController::openAbout() { emit aboutRequested(); }
void HomeController::checkUpdates() { emit updateCheckRequested(); }

QUrl HomeController::generalPortrait(const QString &generalName) const
{
    if (generalName.isEmpty())
        return QUrl();

    QString actual = generalName;
    if (Sanguosha) {
        const QString alias = Sanguosha->getResourceAlias("generals", generalName);
        if (!alias.isEmpty())
            actual = alias;
    }

    const QString base = QDir::current().absoluteFilePath(
        QStringLiteral("image/fullskin/generals/full/%1").arg(actual));
    const QStringList suffixes = { QStringLiteral(".jpg"), QStringLiteral(".png"),
                                   QStringLiteral(".webp"), QStringLiteral(".jpeg") };
    for (const QString &suffix : suffixes) {
        const QString candidate = base + suffix;
        if (QFile::exists(candidate))
            return QUrl::fromLocalFile(candidate);
    }
    return QUrl();
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
        QString nickname = Sanguosha->translate("#" + name);
        if (nickname.contains('_'))
            nickname = Sanguosha->translate("#" + nickname.split('_').last());
        if (nickname.startsWith('#'))
            nickname.clear();

        item.insert(QStringLiteral("name"), name);
        item.insert(QStringLiteral("displayName"), Sanguosha->translate(name));
        item.insert(QStringLiteral("nickname"), nickname);
        item.insert(QStringLiteral("kingdom"), general->getKingdom());
        item.insert(QStringLiteral("kingdoms"), general->getKingdoms());
        item.insert(QStringLiteral("maxHp"), general->getMaxHp());
        item.insert(QStringLiteral("package"), general->getPackage());
        item.insert(QStringLiteral("packageName"), Sanguosha->translate(general->getPackage()));
        item.insert(QStringLiteral("hidden"), Sanguosha->isGeneralHidden(name));
        item.insert(QStringLiteral("lord"), general->isLord());
        item.insert(QStringLiteral("portrait"), generalPortrait(name));
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

    result.insert(QStringLiteral("name"), generalName);
    result.insert(QStringLiteral("displayName"), Sanguosha->translate(generalName));
    result.insert(QStringLiteral("kingdom"), general->getKingdom());
    result.insert(QStringLiteral("kingdoms"), general->getKingdoms());
    result.insert(QStringLiteral("maxHp"), general->getMaxHp());
    result.insert(QStringLiteral("package"), Sanguosha->translate(general->getPackage()));
    result.insert(QStringLiteral("portrait"), generalPortrait(generalName));
    result.insert(QStringLiteral("oracleText"), general->getOracleText());
    result.insert(QStringLiteral("skillDescription"), general->getSkillDescription(true));

    QString nickname = Sanguosha->translate("#" + generalName);
    if (nickname.contains('_'))
        nickname = Sanguosha->translate("#" + nickname.split('_').last());
    if (nickname.startsWith('#'))
        nickname.clear();
    result.insert(QStringLiteral("nickname"), nickname);

    QVariantList skills;
    for (const Skill *skill : general->getVisibleSkillList()) {
        if (!skill)
            continue;
        QVariantMap entry;
        entry.insert(QStringLiteral("name"), skill->objectName());
        entry.insert(QStringLiteral("displayName"), Sanguosha->translate(skill->objectName()));
        entry.insert(QStringLiteral("description"), skill->getDescription());
        entry.insert(QStringLiteral("oracleText"), skill->getOracleText());
        skills.append(entry);
    }
    result.insert(QStringLiteral("skills"), skills);
    return result;
}

QUrl HomeController::randomBackdrop() const
{
    QDir dir(QStringLiteral("image/system/backdrop"));
    const QStringList files = dir.entryList(QDir::Files);
    if (files.isEmpty())
        return QUrl();
    const int index = QRandomGenerator::global()->bounded(files.size());
    return QUrl::fromLocalFile(dir.absoluteFilePath(files.at(index)));
}

void HomeController::refreshCharacterImage()
{
    ++m_characterVersion;
    emit characterImageChanged();
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
