#ifndef SYNCHRONIZED_BAN_PACKAGES_H
#define SYNCHRONIZED_BAN_PACKAGES_H
#include <QStringList>
#include <QVariant>
#include <QMutex>
#include <QMutexLocker>
// One resource only: existing package-selection writers retain their ordered
// list semantics. Reads return detached value snapshots; no mutable alias exits.
// No Lua, QObject, signal or user callback executes under this storage lock.
class SynchronizedBanPackages final {
public:
 SynchronizedBanPackages()=default;
 SynchronizedBanPackages(const SynchronizedBanPackages &other):values(other.snapshot()){}
 SynchronizedBanPackages &operator=(const SynchronizedBanPackages &other){return *this=other.snapshot();}
 SynchronizedBanPackages &operator=(const QStringList &next){QMutexLocker lock(&mutex);values=next;return *this;}
 QStringList snapshot()const{QMutexLocker lock(&mutex);return values;}
 operator QStringList()const{return snapshot();}
 operator QVariant()const{return QVariant(snapshot());}
 bool contains(const QString &name)const{QMutexLocker lock(&mutex);return values.contains(name);}
 void clear(){QMutexLocker lock(&mutex);values.clear();}
 void append(const QString &name){QMutexLocker lock(&mutex);values.append(name);}
 SynchronizedBanPackages &operator<<(const QString &name){append(name);return *this;}
private:
 mutable QMutex mutex;
 QStringList values;
};
#endif
