#include <QCoreApplication>
#include <QRegularExpression>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QRegularExpression pattern(QStringLiteral("^@(\\w+)=([^:]+)(:.+)?$"));
    const QRegularExpressionMatch match = pattern.match(QStringLiteral("@KurouCard=."));

    if (!match.hasMatch())
        return 1;

    // Qt 6 omits an unmatched optional capture from capturedTexts().
    if (!match.captured(1).isEmpty() && !match.captured(2).isEmpty()
        && match.captured(3).isEmpty())
        return 0;

    return 2;
}
