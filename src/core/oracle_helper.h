#ifndef ORACLE_HELPER_H
#define ORACLE_HELPER_H

#include <QString>
#include <QStringList>

QString buildOracleTooltip(const QString &oracleText, const QString &skillDescription);
// The concepts buildOracleTooltip lists for these texts; empty when the option is off.
QStringList oracleConcepts(const QStringList &htmlSources);

#endif // ORACLE_HELPER_H