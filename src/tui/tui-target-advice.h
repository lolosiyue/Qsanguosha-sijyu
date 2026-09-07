#ifndef TUI_TARGET_ADVICE_H
#define TUI_TARGET_ADVICE_H

#include "runtime/client-target-evaluator.h"

#include <QString>

#include <functional>

// Keep the existing TUI surface stable while the rule calculation moves into a
// frontend-neutral client runtime boundary that Web/WASM can reuse.
using TuiPlayerLookup = ClientRules::PlayerLookup;
using TuiTargetStep = ClientRules::TargetStep;
using TuiNameText = std::function<QString(const QString &)>;

// pool is the prompt's own target list: a player the server withheld never
// becomes a candidate just because the engine would allow it.
TuiTargetStep tuiTargetStep(const Card *card, const QStringList &chosen,
                            const QStringList &pool, const TuiPlayerLookup &lookup,
                            const Player *self);

// The verdict on a finished answer, worded for the player. Empty means send it.
// The engine-facing validation itself is presentation-neutral and lives in
// ClientRules::validateTargets().
QString tuiValidateTargets(const Card *card, const QStringList &targets,
                           const TuiPlayerLookup &lookup, const Player *self,
                           const TuiNameText &cardName, const TuiNameText &playerName,
                           bool *incomplete = nullptr);

#endif
