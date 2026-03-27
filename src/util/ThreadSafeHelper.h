#include <QThread>
#include <QMetaObject>
#include <QVariant>
#include <QString>

class ThreadSafeHelper {
public:
    static bool setProperty(QObject* target, const char* name, const QVariant& value) {
        if (!target) return false;

        if (QThread::currentThread() != target->thread()) {
            QString strName(name);
            bool result = false;

            QMetaObject::invokeMethod(target, [target, strName, value, &result]() {
                result = target->setProperty(strName.toUtf8().constData(), value);
                }, Qt::BlockingQueuedConnection);

            return result;
        }
        else {
            return target->setProperty(name, value);
        }
    }
};
