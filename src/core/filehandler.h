#ifndef FILEHANDLER_H
#define FILEHANDLER_H

//#include <QObject>
//#include <QFile>
//#include <QTextStream>
//#include <QFileInfo>
//#include <QDebug>
//#include <QStringList>
//#include <QDir>

class FileHandler : public QObject
{
    Q_OBJECT
public:
    explicit FileHandler(QObject *parent = nullptr);

    // Read file contents.
    Q_INVOKABLE QString readFile(const QString &filePath);

    // Write contents to a file.
    Q_INVOKABLE bool writeFile(const QString &filePath, const QString &content);

    // Check whether a file exists.
    Q_INVOKABLE bool fileExists(const QString &filePath);

    // Get the file size in bytes.
    Q_INVOKABLE qint64 getFileSize(const QString &filePath);
    // List image filenames under a path.
    Q_INVOKABLE QStringList getImageList(const QString& dirPath);

    Q_INVOKABLE int getFileCount(const QString &dirPath);

private:
    // Normalize paths, including spaces and special characters.
    QString processPath(const QString &rawPath);
};

#endif // FILEHANDLER_H