#ifndef AI_STRATEGY_ROUND44_FOCUS_H
#define AI_STRATEGY_ROUND44_FOCUS_H
#include <memory>
#include <QtGlobal>
class Room; class ServerPlayer; class QVariant; struct lua_State;
// Native-only source callback frame. Construct at actual LuaAI::filterEvent entry.
class AIRound44FocusFrame {
public:
    AIRound44FocusFrame(Room &,ServerPlayer *,int,const QVariant &);
    ~AIRound44FocusFrame();
    AIRound44FocusFrame(const AIRound44FocusFrame &)=delete;
    AIRound44FocusFrame &operator=(const AIRound44FocusFrame &)=delete;
private:
    struct Data; std::unique_ptr<Data> d;
    friend struct AIRound44FocusAccess;
};
// Once after normal game SmartAI autoload, before any native filterEvent call.
void installAIRound44FocusCallbacks(Room &);
// Native-only metadata; does not read marks or invoke callbacks.
bool hasAIRound44FocusCallbacks(Room &);
// Native integration evidence, never exported to either policy VM.
quint64 aiRound44FocusExecutionSerial(Room &);
// Metadata-only finite leaf proof for a genuine current filterEvent frame.
// Requires exact installed C closures/Room upvalues and a non-Focus event.
// It does not certify the selected callback, arbitrary C functions, or effects.
bool aiRound44FocusContinuityNoop(Room &,lua_State *,int argumentCount);
// Metadata-only preallocation proof of the two fixed C closures and Room upvalue.
bool aiRound56FocusBindings(Room&,lua_State*) noexcept;
#endif
