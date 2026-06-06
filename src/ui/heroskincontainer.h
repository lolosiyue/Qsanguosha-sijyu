#ifndef HEROSKINCONTAINER_H
#define HEROSKINCONTAINER_H

#include <QGraphicsObject>
#include <QDir>
#include <QHash>
#include <QStringList>
#include <algorithm>
#include "engine.h"

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

        if (!cache.contains(generalName)) {
            QList<int> indices;
            QStringList skinDirs;
            
            if (Sanguosha) {
                skinDirs = Sanguosha->getResourceAliasList("heroskin", generalName);
                QString primaryAlias = Sanguosha->getResourceAlias("heroskin", generalName);
                if (primaryAlias != generalName && !skinDirs.contains(primaryAlias)) {
                    skinDirs.prepend(primaryAlias);
                }
            }
            if (skinDirs.isEmpty()) {
                skinDirs << generalName;
            }

            foreach (const QString &skinDir, skinDirs) {
                QDir dir(QString("hero-skin/%1").arg(skinDir));
                if (dir.exists()) {
                    QStringList subdirs = dir.entryList(QDir::AllDirs | QDir::NoDotAndDotDot, QDir::Name);
                    foreach (const QString &subdir, subdirs) {
                        bool ok;
                        int index = subdir.toInt(&ok);
                        if (ok && index > 0 && !indices.contains(index)) {
                            indices << index;
                        }
                    }
                }
            }
            std::sort(indices.begin(), indices.end());
            cache[generalName] = indices;
        }
        
        return cache[generalName];
    }

    static int getNextSkinIndex(const QString &generalName, int currentIndex)
    {
        QList<int> indices = getAvailableSkinIndices(generalName);
        
        if (indices.isEmpty())
            return 0;

        if (currentIndex == 0)
            return indices.first();

        for (int i = 0; i < indices.size(); ++i) {
            if (indices[i] > currentIndex) {
                return indices[i];
            }
        }

        return 0;
    }
};

#endif // HEROSKINCONTAINER_H
