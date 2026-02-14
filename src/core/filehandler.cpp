
#include "FileHandler.h"
//#include <QDir>
#ifdef ANDROID
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
    // 处理路径中的空格和特殊字符（跨平台兼容）
    QString processedPath = rawPath;
    if (rawPath.startsWith("file://")) {
        processedPath = QUrl(rawPath).toLocalFile();
    }/*

    // 安卓平台路径适配
#ifdef ANDROID
    // 如果是相对路径，且不是绝对路径，则使用安卓数据目录
    if (!processedPath.startsWith("/") && !processedPath.contains(":")) {
        QString androidDataPath = AndroidAssets::getWritableDataPath();
        processedPath = androidDataPath + "/" + processedPath;
        // 安卓路径适配
    }
#endif*/

    return processedPath;
}

QStringList FileHandler::getImageList(const QString& dirPath) {
    const QString processedPath = processPath(dirPath);
    QDir directory(processedPath);

    // Qt5.4兼容写法
    QStringList filters;
    filters << "*.jpg" << "*.jpeg";

    QStringList images = directory.entryList(
        filters,           // 文件名过滤器
        QDir::Files,       // 只匹配文件（原QDir::Filter::Files）
        QDir::Name         // 按文件名排序（原QDir::SortFlag::Name）
    );

    // 转换为完整路径
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