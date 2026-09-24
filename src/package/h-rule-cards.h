#ifndef H_RULE_CARDS_H
#define H_RULE_CARDS_H

#include <QList>

class QObject;
class Skill;

// Built-in rule definitions belong to the engine or room, never a general package.
// HegemonyRule still controls when these skills are attached to players.
QList<const Skill *> createHegemonyRuleSkills(QObject *owner);

#endif
