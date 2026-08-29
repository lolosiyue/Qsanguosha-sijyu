#include "oracle_helper.h"
#include "engine.h"
#include "settings.h"

#include <QRegularExpression>

static const QRegularExpression &conceptPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("<a\\s+href=\"([^\"]+)\"[^>]*>(.*)</a>"),
        QRegularExpression::UseUnicodePropertiesOption
            | QRegularExpression::InvertedGreedinessOption);
    return pattern;
}

static QStringList extractConcepts(const QString &html) {
    QStringList concepts;
    if (html.isEmpty()) return concepts;

    QRegularExpressionMatchIterator matches = conceptPattern().globalMatch(html);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QString href = match.captured(1);

        if (!href.startsWith("#")) continue;

        QString translation = Sanguosha->translate(href);
        if (translation.startsWith(":") || translation.isEmpty() || translation == href)
            continue;

        if (!concepts.contains(translation))
            concepts.append(translation);
    }

    return concepts;
}

QString buildOracleTooltip(const QString &oracleText, const QString &skillDescription) {
    if (oracleText.isEmpty() && skillDescription.isEmpty()) return QString();

    QString result;
    if (!oracleText.isEmpty()) {
        result = oracleText;
        result.append("<br/><br/>");
    }
    if (!skillDescription.isEmpty()) {
        result.append(skillDescription);
    }

    if (Config.value("EnableOracleConcepts", true).toBool()) {
        QStringList allConcepts;
        allConcepts.append(extractConcepts(oracleText));
        allConcepts.append(extractConcepts(skillDescription));

        if (!allConcepts.isEmpty()) {
            result.append("<br/><hr/>相关概念：<br/>");
            foreach (QString concept, allConcepts)
                result.append(QString("· %1<br/>").arg(concept));
        }
    }

    return result;
}
