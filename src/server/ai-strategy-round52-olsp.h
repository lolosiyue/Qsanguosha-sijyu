#ifndef AI_STRATEGY_ROUND52_OLSP_H
#define AI_STRATEGY_ROUND52_OLSP_H
#include <QJsonObject>
#include <memory>
class Room; class ServerPlayer; class QString;
struct SkillContext; struct AIRequest; struct AiSkillActionContext;
// Native callsite-only scope around the two fixed OL/SP response inquiries.
class AIRound52OlspQuestionScope final {
public:
 AIRound52OlspQuestionScope(Room&, const SkillContext&, const QString &skill);
 ~AIRound52OlspQuestionScope();
 AIRound52OlspQuestionScope(const AIRound52OlspQuestionScope&)=delete;
 AIRound52OlspQuestionScope& operator=(const AIRound52OlspQuestionScope&)=delete;
private: struct Data; std::unique_ptr<Data> d;
};
// Pre-request instance admission and resolved-root equality are separate because
// makeRequest projects scope rows before attaching its selected action context.
bool aiRound52OlspInstance(Room&,const ServerPlayer*,const QString&,int instanceID);
bool aiRound52OlspAction(Room&,const ServerPlayer*,const AiSkillActionContext&);
bool aiRound52OlspQuestion(const AIRequest&,const ServerPlayer*);
bool aiRound52OlspEvaluation(const AIRequest&,const ServerPlayer*,const QJsonObject&);
#endif
