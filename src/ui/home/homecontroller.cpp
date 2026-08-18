#include "homecontroller.h"
#include "engine.h"
#include "settings.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QGuiApplication>
#include <QStyleHints>

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
    const QString &avatar = Config.UserAvatar;
    if (avatar.isEmpty())
        return QUrl();

    // 與快速加入對話框相同的頭像圖源
    const QString absPath = QDir::current().absoluteFilePath(
        QStringLiteral("image/fullskin/generals/full/%1.jpg").arg(avatar));

    if (QFile::exists(absPath))
        return QUrl::fromLocalFile(absPath);

    return QUrl();
}

void HomeController::refreshPlayerInfo()
{
    emit playerInfoChanged();
}

bool HomeController::hasVideoSupport() const
{
#ifdef HAS_QT_MULTIMEDIA
    // 執行期偵測 multimedia 後端 plugin 是否真的可載入，
    // 避免編譯期有 QtMultimedia 但部署環境缺少 ffmpegmediaplugin.dll 時，
    // QML Video 仍建立並在控制台噴出一串 "could not load multimedia backend"。
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

void HomeController::openGenerals()
{
    emit generalsRequested();
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
    // 二元切換：目前有效暗→設亮色(1)；有效亮→設暗色(2)
    const int next = isDarkTheme() ? 1 : 2;
    Config.ColorScheme = next;
    Config.setValue("ColorScheme", next);
    applyColorScheme(next);
    emit themeChanged();
}
