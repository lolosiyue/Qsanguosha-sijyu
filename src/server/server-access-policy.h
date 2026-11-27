#ifndef QSAN_SERVER_ACCESS_POLICY_H
#define QSAN_SERVER_ACCESS_POLICY_H

#include <QHostAddress>
#include <QStringList>

struct ServerAccessPolicy
{
    bool allowEnabled = false;
    QStringList allow;
    QStringList deny;

    static QString addressKey(const QString &text)
    {
        QHostAddress address;
        if (!address.setAddress(text))
            return QString();
        // A dual-stack listener can report IPv4 peers as IPv4-mapped IPv6.
        bool isIPv4 = false;
        const quint32 ipv4 = address.toIPv4Address(&isIPv4);
        return isIPv4 ? QHostAddress(ipv4).toString() : address.toString();
    }

    bool accepts(const QString &peer) const
    {
        if (deny.isEmpty() && !allowEnabled)
            return true;
        const QString key = addressKey(peer);
        if (key.isEmpty())
            return false;
        for (const QString &entry : deny)
            if (addressKey(entry) == key)
                return false;
        if (!allowEnabled)
            return true;
        for (const QString &entry : allow)
            if (addressKey(entry) == key)
                return true;
        return false;
    }
};
#endif
