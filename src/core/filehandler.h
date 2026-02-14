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

    // 读取文件内容
    Q_INVOKABLE QString readFile(const QString &filePath);

    // 写入内容到文件
    Q_INVOKABLE bool writeFile(const QString &filePath, const QString &content);

    // 检查文件是否存在
    Q_INVOKABLE bool fileExists(const QString &filePath);

    // 获取文件大小（字节）
    Q_INVOKABLE qint64 getFileSize(const QString &filePath);
    //获取路径下所有图片名字
    Q_INVOKABLE QStringList getImageList(const QString& dirPath);

    Q_INVOKABLE int getFileCount(const QString &dirPath);

private:
    // 统一处理文件路径（处理空格和特殊字符）
    QString processPath(const QString &rawPath);
};

#endif // FILEHANDLER_H