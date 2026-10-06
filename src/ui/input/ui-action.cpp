#include "ui-action.h"

namespace {
struct ActionName { UiAction action; const char *name; };

const ActionName kActionNames[] = {
    { UiAction::Up, "up" },
    { UiAction::Down, "down" },
    { UiAction::Left, "left" },
    { UiAction::Right, "right" },
    { UiAction::Accept, "accept" },
    { UiAction::Back, "back" },
    { UiAction::Confirm, "confirm" },
    { UiAction::Secondary, "secondary" },
    { UiAction::Details, "details" },
    { UiAction::PreviousGroup, "previous_group" },
    { UiAction::NextGroup, "next_group" },
    { UiAction::PreviousPage, "previous_page" },
    { UiAction::NextPage, "next_page" },
    { UiAction::View, "view" },
    { UiAction::Menu, "menu" },
};

GlyphStyle g_glyphStyle = GlyphStyle::Xbox;
}

namespace QSanInput {

QString uiActionName(UiAction action)
{
    for (const auto &entry : kActionNames)
        if (entry.action == action) return QString::fromLatin1(entry.name);
    return QString();
}

bool uiActionFromName(const QString &name, UiAction *action)
{
    for (const auto &entry : kActionNames) {
        if (name == QLatin1String(entry.name)) {
            if (action) *action = entry.action;
            return true;
        }
    }
    return false;
}

bool isDirection(UiAction action)
{
    return action == UiAction::Up || action == UiAction::Down
        || action == UiAction::Left || action == UiAction::Right;
}

int uiActionKey(UiAction action, UiKeyMap map, Qt::KeyboardModifiers *modifiers)
{
    if (modifiers) *modifiers = Qt::NoModifier;
    switch (action) {
    case UiAction::Up: return Qt::Key_Up;
    case UiAction::Down: return Qt::Key_Down;
    case UiAction::Left: return Qt::Key_Left;
    case UiAction::Right: return Qt::Key_Right;
    case UiAction::Accept: return map == UiKeyMap::Table ? Qt::Key_Space : Qt::Key_Return;
    case UiAction::Back: return Qt::Key_Escape;
    case UiAction::Confirm: return Qt::Key_Return;
    case UiAction::Secondary: return map == UiKeyMap::Table ? 0 : Qt::Key_Space;
    case UiAction::PreviousGroup:
        if (map == UiKeyMap::Generic) return Qt::Key_PageUp;
        if (modifiers) *modifiers = Qt::ShiftModifier;
        return Qt::Key_Backtab;
    case UiAction::NextGroup: return map == UiKeyMap::Table ? Qt::Key_Tab : Qt::Key_PageDown;
    case UiAction::PreviousPage: return Qt::Key_Home;
    case UiAction::NextPage: return Qt::Key_End;
    case UiAction::Details:
    case UiAction::View:
    case UiAction::Menu:
        return 0;
    }
    return 0;
}

QString uiActionGlyph(UiAction action, GlyphStyle style)
{
    switch (action) {
    case UiAction::Up:
    case UiAction::Down:
    case UiAction::Left:
    case UiAction::Right:
        return style == GlyphStyle::Keyboard ? QStringLiteral("←↑→↓") : QStringLiteral("✥");
    default:
        break;
    }
    switch (style) {
    case GlyphStyle::Xbox:
        switch (action) {
        case UiAction::Accept: return QStringLiteral("A");
        case UiAction::Back: return QStringLiteral("B");
        case UiAction::Secondary: return QStringLiteral("X");
        case UiAction::Details: return QStringLiteral("Y");
        case UiAction::Confirm: return QStringLiteral("☰");
        case UiAction::View: return QStringLiteral("⧉");
        case UiAction::PreviousGroup: return QStringLiteral("LB");
        case UiAction::NextGroup: return QStringLiteral("RB");
        case UiAction::PreviousPage: return QStringLiteral("LT");
        case UiAction::NextPage: return QStringLiteral("RT");
        case UiAction::Menu: return QStringLiteral("Guide");
        default: break;
        }
        break;
    case GlyphStyle::PlayStation:
        switch (action) {
        case UiAction::Accept: return QStringLiteral("✕");
        case UiAction::Back: return QStringLiteral("○");
        case UiAction::Secondary: return QStringLiteral("□");
        case UiAction::Details: return QStringLiteral("△");
        case UiAction::Confirm: return QStringLiteral("OPTIONS");
        case UiAction::View: return QStringLiteral("SHARE");
        case UiAction::PreviousGroup: return QStringLiteral("L1");
        case UiAction::NextGroup: return QStringLiteral("R1");
        case UiAction::PreviousPage: return QStringLiteral("L2");
        case UiAction::NextPage: return QStringLiteral("R2");
        case UiAction::Menu: return QStringLiteral("PS");
        default: break;
        }
        break;
    case GlyphStyle::Nintendo:
        // Positional: the bottom face button is labelled B on Nintendo pads.
        switch (action) {
        case UiAction::Accept: return QStringLiteral("B");
        case UiAction::Back: return QStringLiteral("A");
        case UiAction::Secondary: return QStringLiteral("Y");
        case UiAction::Details: return QStringLiteral("X");
        case UiAction::Confirm: return QStringLiteral("+");
        case UiAction::View: return QStringLiteral("−");
        case UiAction::PreviousGroup: return QStringLiteral("L");
        case UiAction::NextGroup: return QStringLiteral("R");
        case UiAction::PreviousPage: return QStringLiteral("ZL");
        case UiAction::NextPage: return QStringLiteral("ZR");
        case UiAction::Menu: return QStringLiteral("Home");
        default: break;
        }
        break;
    case GlyphStyle::Keyboard:
        switch (action) {
        case UiAction::Accept: return QStringLiteral("Space");
        case UiAction::Back: return QStringLiteral("Esc");
        case UiAction::Confirm: return QStringLiteral("Enter");
        case UiAction::PreviousGroup: return QStringLiteral("Shift+Tab");
        case UiAction::NextGroup: return QStringLiteral("Tab");
        case UiAction::PreviousPage: return QStringLiteral("Home");
        case UiAction::NextPage: return QStringLiteral("End");
        case UiAction::View: return QStringLiteral("F6");
        default: break;
        }
        break;
    }
    return QString();
}

QString glyphStyleName(GlyphStyle style)
{
    switch (style) {
    case GlyphStyle::Xbox: return QStringLiteral("xbox");
    case GlyphStyle::PlayStation: return QStringLiteral("playstation");
    case GlyphStyle::Nintendo: return QStringLiteral("nintendo");
    case GlyphStyle::Keyboard: return QStringLiteral("keyboard");
    }
    return QString();
}

GlyphStyle currentGlyphStyle()
{
    return g_glyphStyle;
}

void setCurrentGlyphStyle(GlyphStyle style)
{
    g_glyphStyle = style;
}

} // namespace QSanInput
