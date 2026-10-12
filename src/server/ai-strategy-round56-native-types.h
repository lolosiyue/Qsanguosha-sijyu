#ifndef AI_STRATEGY_ROUND56_NATIVE_TYPES_H
#define AI_STRATEGY_ROUND56_NATIVE_TYPES_H
#include <typeindex>
#include <type_traits>
#include <unordered_map>
class Skill;
// Keep all existing domain bits unchanged. Additional source metadata uses a
// separate exact-type map and can never upgrade a correction/metadata bit.
class AIRound56NativeTypes : public std::unordered_map<std::type_index,unsigned> {
public:
    std::unordered_map<std::type_index,unsigned> activateSourceIds;
    template<class T> void recordActivateSource(unsigned id) {
        activateSourceIds.emplace(std::type_index(typeid(T)), id);
    }
};
#define AI_R56_REQUIRE_DECLARATION(Type, Method, Owner) \
    static_assert(std::is_same<decltype(&Type::Method), decltype(&Owner::Method)>::value, \
        "R56 source method declaring class changed: " #Type "::" #Method)
// Exact compiled type registration. Bit1: complete correction-family type;
// bit2: the reviewed frequency/equipment/quota virtual implementation union.
// Bit4: exact native Pindian prohibition body, requiring the independent
// complete public metadata/correction union for its nested Player queries.
// No bit proves captured Lua callbacks, allocation or host descendants.
unsigned aiRound56NativeEffectClass(const Skill*);
struct AIRound56ActivateNativeMethodSource {
    const char *method;
    const char *source;
    bool scalarBodyProved; // BODY ONLY; no caller, lineage or execution grant
};
struct AIRound56ActivateNativeTypeSource {
    const char *typeSpelling; // diagnostic, never used for lookup/authorization
    const char *registrationFile;
    const AIRound56ActivateNativeMethodSource *methods;
    unsigned methodCount;
};
const AIRound56ActivateNativeTypeSource *aiRound56ActivateNativeTypeSource(const Skill *);
#endif
