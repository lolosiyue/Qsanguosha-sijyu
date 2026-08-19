#ifndef QSAN_AI_DATA_STORE_H
#define QSAN_AI_DATA_STORE_H

#include <QString>

class AiDataStore
{
public:
    static QString read();
    static bool write(const QString &json, QString *error = nullptr);
};

#endif
