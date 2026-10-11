#ifndef AI_STRATEGY_ROUND47B_FATE_RECEIPT_H
#define AI_STRATEGY_ROUND47B_FATE_RECEIPT_H
#include <memory>
#include <QSet>
#include <QString>
class Room; class ServerPlayer; class ViewAsSkillV2; class Card;
struct AIRequest; struct AICardConversionView; struct CardUseStruct; struct ActiveSkillRequest;
class AIRound47FateInquiryState;
class AIRound47FateUseProof;
// Compiled real askForUseCardStruct entry only. Nested questions mask the outer frame.
class AIRound47FateInquiry final {
public:
    AIRound47FateInquiry(Room &, ServerPlayer *, const QString &pattern);
    ~AIRound47FateInquiry();
    AIRound47FateInquiry(const AIRound47FateInquiry &)=delete;
    AIRound47FateInquiry &operator=(const AIRound47FateInquiry &)=delete;
private:
    friend class AIRound47FateUseAccess;
    std::shared_ptr<AIRound47FateInquiryState> m_state, m_previous;
    static thread_local std::shared_ptr<AIRound47FateInquiryState> s_current;
};
class AIRound47FateUseAccess final {
public:
    static bool attach(const AIRequest &, Room &, ServerPlayer *, const AICardConversionView &, CardUseStruct &);
    static bool bind(Room &, const CardUseStruct &, ActiveSkillRequest &);
    static bool extend(const Room &, const ServerPlayer *, const ViewAsSkillV2 *, const ActiveSkillRequest &, QSet<int> &);
    static bool finish(const Room &, const ActiveSkillRequest &, const Card *);
private:
    static bool current(const Room &, const std::shared_ptr<const AIRound47FateUseProof> &);
};
#endif
