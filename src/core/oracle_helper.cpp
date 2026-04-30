#include "oracle_helper.h"
#include "engine.h"

static QRegExp *g_conceptRx = nullptr;

static QRegExp &getConceptRx() {
    if (!g_conceptRx) {
        g_conceptRx = new QRegExp("<a\\s+href=\"([^\"]+)\"[^>]*>([^<]*)</a>");
    }
    return *g_conceptRx;
}

static QStringList extractConcepts(const QString &html) {
    QStringList concepts;
    if (html.isEmpty()) return concepts;

    QRegExp &rx = getConceptRx();
    int pos = 0;
    QString processed = html;

    while ((pos = rx.indexIn(processed, pos)) != -1) {
        QString href = rx.cap(1);
        int matchStart = rx.pos();
        int matchLen = rx.matchedLength();
        pos = matchStart + matchLen;

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

    QStringList allConcepts;
    allConcepts.append(extractConcepts(oracleText));
    allConcepts.append(extractConcepts(skillDescription));

    QString result;
    if (!oracleText.isEmpty()) {
        result = oracleText;
        result.append("<br/><br/>");
    }
    if (!skillDescription.isEmpty()) {
        result.append(skillDescription);
    }

    if (!allConcepts.isEmpty()) {
        result.append("<br/><hr/>相关概念：<br/>");
        foreach (QString concept, allConcepts)
            result.append(QString("· %1<br/>").arg(concept));
    }

    return result;
}