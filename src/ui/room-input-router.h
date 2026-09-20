#ifndef ROOM_INPUT_ROUTER_H
#define ROOM_INPUT_ROUTER_H

#include <QKeyEvent>

#include <functional>

class RoomInputRouter final
{
public:
    struct Callbacks {
        std::function<bool()> chatFocused;
        std::function<void()> trust;
        std::function<void()> chooseSkill;
        std::function<void()> toggleEmotion;
        std::function<void()> beginSorting;
        std::function<void()> reverseSelection;
        std::function<void()> adjustItems;
        std::function<void(bool)> addRobots;
        std::function<void(const QString &)> selectCard;
        std::function<void(bool, bool)> moveCardSelection;
        std::function<void()> ok;
        std::function<void()> cancel;
        std::function<void()> discard;
        std::function<void()> clearSelection;
        std::function<void(int, bool)> selectTarget;
        std::function<void(int)> selectEquip;
        std::function<void()> showGeneralPile;
        std::function<void()> showDistance;
        std::function<void()> toggleSkillButtons;
        std::function<bool()> playing;
        std::function<bool()> cancelEnabled;
        std::function<bool()> discardEnabled;
        std::function<bool()> choiceDialogCanReject;
        std::function<void()> rejectChoiceDialog;
    };

    explicit RoomInputRouter(Callbacks callbacks);
    bool route(QKeyEvent *event, bool hotkeysEnabled) const;

private:
    Callbacks m_callbacks;
};

#endif
