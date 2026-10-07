#ifndef BATTLE_STATISTICS_DIALOG_H
#define BATTLE_STATISTICS_DIALOG_H

#include <QDialog>

// Reads the shared C++ projection. The dialog never interprets rule events.
class BattleStatisticsDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit BattleStatisticsDialog(const QString &general, QWidget *parent = nullptr);
};

#endif
