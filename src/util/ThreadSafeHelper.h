#include <QThread>
#include <QMetaObject>
#include <QSemaphore>
#include <QTimer>
#include <QVariant>
#include <QString>

class ThreadSafeHelper {
public:
    static bool setProperty(QObject* target, const char* name, const QVariant& value) {
        if (!target) return false;

        if (QThread::currentThread() != target->thread()) {
            QString strName(name);
            bool result = false;

#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
            QSemaphore completed;
            QTimer::singleShot(0, target, [target, strName, value, &result, &completed]() {
                result = target->setProperty(strName.toUtf8().constData(), value);
                completed.release();
            });
            completed.acquire();
#else
            QMetaObject::invokeMethod(target, [target, strName, value, &result]() {
                result = target->setProperty(strName.toUtf8().constData(), value);
            }, Qt::BlockingQueuedConnection);
#endif

            return result;
        }
        else {
            return target->setProperty(name, value);
        }
    }
};
