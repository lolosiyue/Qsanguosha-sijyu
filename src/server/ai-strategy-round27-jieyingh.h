#ifndef AI_STRATEGY_ROUND27_JIEYINGH_H
#define AI_STRATEGY_ROUND27_JIEYINGH_H
#include <QString>
#include <QtGlobal>
#include <memory>
class Room;
class ServerPlayer;
class Card;
struct AIRequest;
struct AICardConversionView;
// Observer only. Every real use-card question (including nested other questions)
// pushes a boundary; only the finite native Jieyingh trigger can authorize it.
class AIRound27JieyinghQuestion {
public:
    AIRound27JieyinghQuestion(Room &, ServerPlayer *, const QString &pattern,
                             const QString &prompt, int method);
    ~AIRound27JieyinghQuestion();
    AIRound27JieyinghQuestion(const AIRound27JieyinghQuestion &) = delete;
    AIRound27JieyinghQuestion &operator=(const AIRound27JieyinghQuestion &) = delete;
private:
    struct Data;
    std::unique_ptr<Data> d;
    friend struct AIRound27JieyinghAccess;
};
void projectAIRound27JieyinghQuestion(AIRequest &, Room &, ServerPlayer *);
// Fresh exact native V1 product. Caller owns the returned virtual card.
Card *createAIRound27JieyinghProduct(const AIRequest &, Room &, ServerPlayer *,
                                    quint64 serial, QString *error = nullptr);
Card *rebuildAIRound27JieyinghProduct(const AIRequest &, Room &, ServerPlayer *,
                                     const AICardConversionView &);
#endif
