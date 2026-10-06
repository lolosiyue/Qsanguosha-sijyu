#pragma once

#include <QString>
#include <QMetaType>

// Device-independent actions. Only navigation repeats; activation is an edge.
enum class ControllerAction {
    Up, Down, Left, Right, Activate, Submit, Back, Inspect,
    PreviousGroup, NextGroup, PreviousPage, NextPage, Menu, Recover,
    MoveEarlier, MoveLater
};
Q_DECLARE_METATYPE(ControllerAction)

inline QString controllerActionName(ControllerAction action)
{
    static const char *names[] = {"up", "down", "left", "right", "activate", "submit",
        "back", "inspect", "previous-group", "next-group", "previous-page", "next-page",
        "menu", "recover", "move-earlier", "move-later"};
    return QString::fromLatin1(names[static_cast<int>(action)]);
}

inline bool controllerActionFromName(const QString &name, ControllerAction *action)
{
    for (int i = 0; i <= static_cast<int>(ControllerAction::MoveLater); ++i) {
        auto candidate = static_cast<ControllerAction>(i);
        if (controllerActionName(candidate) == name) { *action = candidate; return true; }
    }
    return false;
}
