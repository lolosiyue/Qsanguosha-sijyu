#include "homecontroller.h"
#include "engine.h"
#include "settings.h"
#include <QDir>
#include <QFile>
#include <QRandomGenerator>

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

    if (QFile::exists(absPath)) {
        QUrl url = QUrl::fromLocalFile(absPath);
        url.setQuery(QStringLiteral("v=%1").arg(m_characterVersion));
        return url;
    }

    return QUrl(QStringLiteral("qrc:/QSanguosha/Home/assets/character.png"));
}

QUrl HomeController::logoImage() const
{
    return QUrl::fromLocalFile(
        QDir::current().absoluteFilePath(
            QStringLiteral("image/logo/logo.png")));
}

bool HomeController::hasVideoSupport() const
{
#ifdef HAS_QT_MULTIMEDIA
    return true;
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
