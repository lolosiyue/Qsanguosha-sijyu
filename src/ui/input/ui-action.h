#ifndef QSAN_UI_ACTION_H
#define QSAN_UI_ACTION_H

#include <QString>
#include <Qt>

// Semantic navigation actions shared by gamepads, TV remotes and test runners.
// Buttons are positional (Accept is always the bottom face button); glyph styles
// only change how a button is labelled, never what it does.
enum class UiAction : int
{
    Up,
    Down,
    Left,
    Right,
    Accept,         // South face button: activate / toggle the focused item
    Back,           // East face button: clear the draft, then cancel
    Confirm,        // Start / Options / +: submit the current draft
    Secondary,      // West face button: finish the play phase
    Details,        // North face button: information card (BP-1f)
    PreviousGroup,  // Left shoulder
    NextGroup,      // Right shoulder
    PreviousPage,   // Left trigger
    NextPage,       // Right trigger
    View,           // Back / View / Share / -: battle log drawer (BP-2)
    Menu            // Guide button: room menu (BP-2)
};

enum class GlyphStyle : int
{
    Xbox,
    PlayStation,
    Nintendo,
    Keyboard
};

// Key events substituted for actions on surfaces that only understand keys.
// Generic follows the shared big-picture convention (arrows, Enter, Escape,
// PageUp/PageDown); Table matches the existing native table keys.
enum class UiKeyMap : int
{
    Generic,
    Table
};

namespace QSanInput {

QString uiActionName(UiAction action);
bool uiActionFromName(const QString &name, UiAction *action);
bool isDirection(UiAction action);

// Returns 0 when the action has no keyboard equivalent in that map.
int uiActionKey(UiAction action, UiKeyMap map, Qt::KeyboardModifiers *modifiers = nullptr);

// Short label printed in a button legend ("A", "✕", "Enter").
QString uiActionGlyph(UiAction action, GlyphStyle style);
QString glyphStyleName(GlyphStyle style);

// Style of the most recently used controller; Xbox until a device reports otherwise.
GlyphStyle currentGlyphStyle();
void setCurrentGlyphStyle(GlyphStyle style);

} // namespace QSanInput

#endif
