#ifndef QML_ELEMENT_PATH_H
#define QML_ELEMENT_PATH_H

#include <QString>

namespace QmlElementPath
{
// Lexical rule shared by Room and the client layer: a relative ".qml" path that
// stays inside the game folder. The client also checks that the file exists.
bool isAllowed(const QString &path, QString *error = nullptr);
}

#endif
