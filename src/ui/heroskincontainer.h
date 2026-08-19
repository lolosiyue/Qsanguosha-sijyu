#ifndef HEROSKINCONTAINER_H
#define HEROSKINCONTAINER_H

#include "engine.h"

#include <QCoreApplication>
#include <QDir>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>

class HeroSkinContainer
{
public:
    static bool hasSkin(const QString &generalName)
    {
        return !getAvailableSkinIndices(generalName).isEmpty();
    }

    static QList<int> getAvailableSkinIndices(const QString &generalName)
    {
        static QHash<QString, QList<int>> cache;
        if (generalName.isEmpty())
            return {};

        if (cache.contains(generalName))
            return cache.value(generalName);

        QList<int> indices;
        QStringList skinDirs;
        skinDirs << generalName;
        if (Sanguosha) {
            const QString primaryAlias = Sanguosha->getResourceAlias(QStringLiteral("heroskin"), generalName);
            if (!primaryAlias.isEmpty() && !skinDirs.contains(primaryAlias))
                skinDirs << primaryAlias;
            const QStringList aliases = Sanguosha->getResourceAliasList(QStringLiteral("heroskin"), generalName);
            for (const QString &alias : aliases) {
                if (!alias.isEmpty() && !skinDirs.contains(alias))
                    skinDirs << alias;
            }
            const QString generalAlias = Sanguosha->getResourceAlias(QStringLiteral("generals"), generalName);
            if (!generalAlias.isEmpty() && !skinDirs.contains(generalAlias))
                skinDirs << generalAlias;
        }

        const QStringList roots = skinSearchRoots();
        for (const QString &skinDir : skinDirs) {
            for (const QString &root : roots) {
                collectDirSkinIndices(QDir(root).filePath(QStringLiteral("hero-skin/%1").arg(skinDir)), indices);
                collectLegacySkinIndices(QDir(root).filePath(QStringLiteral("image/heroskin/fullskin/generals/full")),
                                         skinDir, indices);
            }
        }

        std::sort(indices.begin(), indices.end());
        cache.insert(generalName, indices);
        return indices;
    }

    static int getNextSkinIndex(const QString &generalName, int currentIndex)
    {
        const QList<int> indices = getAvailableSkinIndices(generalName);
        if (indices.isEmpty())
            return 0;
        if (currentIndex == 0)
            return indices.first();
        for (int index : indices) {
            if (index > currentIndex)
                return index;
        }
        return 0;
    }

    static QStringList skinSearchRoots()
    {
        QStringList roots;
        const auto add = [&roots](const QString &raw) {
            const QString path = QDir::cleanPath(raw);
            if (path.isEmpty() || !QDir(path).exists())
                return;
            for (const QString &existing : roots) {
                if (QString::compare(existing, path, Qt::CaseInsensitive) == 0)
                    return;
            }
            roots << path;
        };
        add(QDir::currentPath());
        add(QCoreApplication::applicationDirPath());
        add(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("..")));
        return roots;
    }

private:
    static void collectDirSkinIndices(const QString &dirPath, QList<int> &indices)
    {
        const QDir dir(dirPath);
        if (!dir.exists())
            return;
        const QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &subdir : subdirs) {
            bool ok = false;
            const int index = subdir.toInt(&ok);
            if (ok && index > 0 && !indices.contains(index))
                indices << index;
        }
    }

    static void collectLegacySkinIndices(const QString &dirPath, const QString &generalName, QList<int> &indices)
    {
        const QDir dir(dirPath);
        if (!dir.exists() || generalName.isEmpty())
            return;
        const QRegularExpression re(
            QStringLiteral("^%1_(\\d+)\\.(png|jpg|jpeg|webp)$")
                .arg(QRegularExpression::escape(generalName)),
            QRegularExpression::CaseInsensitiveOption);
        const QStringList files = dir.entryList(QDir::Files, QDir::Name);
        for (const QString &file : files) {
            const QRegularExpressionMatch match = re.match(file);
            if (!match.hasMatch())
                continue;
            const int index = match.captured(1).toInt();
            if (index > 0 && !indices.contains(index))
                indices << index;
        }
    }
};

#endif // HEROSKINCONTAINER_H
