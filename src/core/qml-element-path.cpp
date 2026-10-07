#include "qml-element-path.h"

#include <QDir>

bool QmlElementPath::isAllowed(const QString &path, QString *error)
{
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
    const auto fail = [&](const QString &why) {
        if (error)
            *error = QStringLiteral("\"%1\" %2").arg(path, why);
        return false;
    };
    if (clean.isEmpty() || clean == QLatin1String("."))
        return fail(QStringLiteral("is empty"));
    // ':' also rules out "qrc:", "http:" and Windows drive letters.
    if (QDir::isAbsolutePath(clean) || clean.contains(QLatin1Char(':'))
        || clean == QLatin1String("..") || clean.startsWith(QLatin1String("../")))
        return fail(QStringLiteral("must be a path inside the game folder"));
    if (!clean.endsWith(QLatin1String(".qml"), Qt::CaseInsensitive))
        return fail(QStringLiteral("is not a .qml file"));
    return true;
}
