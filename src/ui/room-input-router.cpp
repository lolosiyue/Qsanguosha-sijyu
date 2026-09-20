#include "room-input-router.h"

#include <utility>

RoomInputRouter::RoomInputRouter(Callbacks callbacks) : m_callbacks(std::move(callbacks))
{
}

bool RoomInputRouter::route(QKeyEvent *event, bool hotkeysEnabled) const
{
    if (event == nullptr)
        return false;
#if !defined(QSAN_XP_LEGACY)
    if (event->key() == Qt::Key_I
        && event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
        event->accept();
        return true;
    }
#endif
    if (!hotkeysEnabled || (m_callbacks.chatFocused && m_callbacks.chatFocused()))
        return false;

    const bool control = event->modifiers() & Qt::ControlModifier;
    const bool alt = event->modifiers() & Qt::AltModifier;
    const auto card = [&](const char key) {
        if (m_callbacks.selectCard)
            m_callbacks.selectCard(QString::fromLatin1(&key, 1));
    };
    switch (event->key()) {
    case Qt::Key_F1: if (m_callbacks.trust) m_callbacks.trust(); break;
    case Qt::Key_F2: if (m_callbacks.chooseSkill) m_callbacks.chooseSkill(); break;
    case Qt::Key_F12: if (m_callbacks.toggleEmotion) m_callbacks.toggleEmotion(); break;
    case Qt::Key_F3: if (m_callbacks.beginSorting) m_callbacks.beginSorting(); break;
    case Qt::Key_F4: if (m_callbacks.reverseSelection) m_callbacks.reverseSelection(); break;
    case Qt::Key_F5: if (m_callbacks.adjustItems) m_callbacks.adjustItems(); break;
    case Qt::Key_F7: if (m_callbacks.addRobots) m_callbacks.addRobots(control); break;
    case Qt::Key_F11: if (m_callbacks.showGeneralPile) m_callbacks.showGeneralPile(); break;
    case Qt::Key_Q: card('Q'); break; case Qt::Key_W: card('W'); break;
    case Qt::Key_E: card('E'); break; case Qt::Key_R: card('R'); break;
    case Qt::Key_T: card('T'); break; case Qt::Key_Y: card('Y'); break;
    case Qt::Key_U: card('U'); break; case Qt::Key_I: card('I'); break;
    case Qt::Key_O: card('O'); break; case Qt::Key_P: card('P'); break;
    case Qt::Key_A: card('A'); break; case Qt::Key_S: card('S'); break;
    case Qt::Key_D: card('D'); break; case Qt::Key_F: card('F'); break;
    case Qt::Key_G: card('G'); break; case Qt::Key_H: card('H'); break;
    case Qt::Key_J: card('J'); break; case Qt::Key_K: card('K'); break;
    case Qt::Key_L: card('L'); break; case Qt::Key_Z: card('Z'); break;
    case Qt::Key_X: card('X'); break; case Qt::Key_C: card('C'); break;
    case Qt::Key_V: card('V'); break; case Qt::Key_B: card('B'); break;
    case Qt::Key_N: card('N'); break; case Qt::Key_M: card('M'); break;
    case Qt::Key_Left: if (m_callbacks.moveCardSelection) m_callbacks.moveCardSelection(false, control); break;
    case Qt::Key_Right: if (m_callbacks.moveCardSelection) m_callbacks.moveCardSelection(true, control); break;
    case Qt::Key_Enter:
    case Qt::Key_Return: if (m_callbacks.ok) m_callbacks.ok(); break;
    case Qt::Key_Escape:
        if (m_callbacks.clearSelection) m_callbacks.clearSelection();
        if (m_callbacks.cancelEnabled && m_callbacks.cancelEnabled() && m_callbacks.cancel)
            m_callbacks.cancel();
        break;
    case Qt::Key_Space:
        if (m_callbacks.choiceDialogCanReject && m_callbacks.choiceDialogCanReject()
            && m_callbacks.rejectChoiceDialog)
            m_callbacks.rejectChoiceDialog();
        else if (m_callbacks.cancelEnabled && m_callbacks.cancelEnabled() && m_callbacks.cancel)
            m_callbacks.cancel();
        else if (m_callbacks.discardEnabled && m_callbacks.discardEnabled() && m_callbacks.discard)
            m_callbacks.discard();
        break;
    case Qt::Key_1: case Qt::Key_2: case Qt::Key_3: case Qt::Key_4:
        if (alt && m_callbacks.selectEquip) m_callbacks.selectEquip(event->key() - Qt::Key_0);
        break;
    case Qt::Key_5: case Qt::Key_6: case Qt::Key_7: case Qt::Key_8: case Qt::Key_9:
        if (m_callbacks.selectTarget) m_callbacks.selectTarget(event->key() - Qt::Key_0, control);
        break;
    case Qt::Key_F9: if (m_callbacks.showDistance) m_callbacks.showDistance(); break;
    case Qt::Key_F10: if (m_callbacks.toggleSkillButtons) m_callbacks.toggleSkillButtons(); break;
    default: return false;
    }
    event->accept();
    return true;
}
