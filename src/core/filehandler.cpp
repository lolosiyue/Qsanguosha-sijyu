
#include <QtGlobal>
#include "filehandler.h"
//#include <QDir>
#ifdef Q_OS_ANDROID
//#include "android_assets.h"
#endif

FileHandler::FileHandler(QObject *parent) : QObject(parent) {}

QString FileHandler::readFile(const QString &filePath)
{
    const QString processedPath = processPath(filePath);
    QFile file(processedPath);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Read failed:" << processedPath << "| Error:" << file.errorString();
        return "";
    }

    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();

    return content;
}

bool FileHandler::writeFile(const QString &filePath, const QString &content)
{
    const QString processedPath = processPath(filePath);
    QFile file(processedPath);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Write failed:" << processedPath << "| Error:" << file.errorString();
        return false;
    }

    QTextStream stream(&file);
    stream << content;
    file.close();

    return true;
}

bool FileHandler::fileExists(const QString &filePath)
{
    const QString processedPath = processPath(filePath);
    return QFileInfo::exists(processedPath);
}

qint64 FileHandler::getFileSize(const QString &filePath)
{
    const QString processedPath = processPath(filePath);
    return QFileInfo(processedPath).size();
}

QString FileHandler::processPath(const QString &rawPath)
{
    // Handle spaces and special characters in paths across platforms.
    QString processedPath = rawPath;
    if (rawPath.startsWith("file://")) {
        processedPath = QUrl(rawPath).toLocalFile();
    }/*

    // Android path handling.
#ifdef Q_OS_ANDROID
    // Resolve relative paths under the Android data directory.
    if (!processedPath.startsWith("/") && !processedPath.contains(":")) {
        QString androidDataPath = AndroidAssets::getWritableDataPath();
        processedPath = androidDataPath + "/" + processedPath;
        // Android path handling.
    }
#endif*/

    return processedPath;
}

QStringList FileHandler::getImageList(const QString& dirPath) {
    const QString processedPath = processPath(dirPath);
    QDir directory(processedPath);

    // Qt 5.4-compatible form.
    QStringList filters;
    filters << "*.jpg" << "*.jpeg";

    QStringList images = directory.entryList(
        filters,           // Filename filter.
        QDir::Files,       // Match files only (formerly QDir::Filter::Files).
        QDir::Name         // Sort by filename (formerly QDir::SortFlag::Name).
    );

    // Convert to an absolute path.
    for(int i = 0; i < images.size(); ++i) {
        images[i] = directory.filePath(images[i]);
    }

    return images;
}

int FileHandler::getFileCount(const QString &dirPath)
{
    QString realPath = processPath(dirPath);
    QDir dir(realPath);
    if (!dir.exists()) {
        return 0;
    }
    return dir.entryList(QDir::Files).count();
}
