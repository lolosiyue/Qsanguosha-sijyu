#ifndef AI_STRATEGY_ROUND29_RESPONSE_H
#define AI_STRATEGY_ROUND29_RESPONSE_H
#include <QString>
#include <QJsonObject>
#include <QList>
#include <QtGlobal>
#include <memory>
class Room;
class ServerPlayer;
class Card;
struct AIRequest;
struct AICardConversionView;
// Every actual use-card question masks an outer question. Only the native
// ThWeijing EventPhaseChanging frame can authorize this finite recipient action.
class AIRound29ZhifengQuestion {
public:
    AIRound29ZhifengQuestion(Room &, ServerPlayer *, const QString &, const QString &, int method);
    ~AIRound29ZhifengQuestion();
    AIRound29ZhifengQuestion(const AIRound29ZhifengQuestion &) = delete;
    AIRound29ZhifengQuestion &operator=(const AIRound29ZhifengQuestion &) = delete;
private:
    struct Data;
    std::unique_ptr<Data> d;
    friend struct AIRound29ZhifengAccess;
};
void projectAIRound29ZhifengQuestion(AIRequest &, Room &, ServerPlayer *);
Card *createAIRound29ZhifengProduct(const AIRequest &, Room &, ServerPlayer *, quint64 serial,
                                   const QList<int> &costs, QString *error = nullptr);
Card *rebuildAIRound29ZhifengProduct(const AIRequest &, Room &, ServerPlayer *, const AICardConversionView &);
// Exact Godlongnu source hypotheses, not provider admission or receipts.
bool aiRound29ResponseConstruction(const AIRequest &, const ServerPlayer *, const QJsonObject &);
#endif
