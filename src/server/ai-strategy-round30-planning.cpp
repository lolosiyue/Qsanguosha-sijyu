#include "ai-strategy-round30-planning.h"
#include "ai-strategy-round53-ainoto.h"
void initializeAIRound30TargetRevises(Room&r){initializeAIRound53AiNoTo(r);}
void beginAIRound30TargetRevises(Room&r,const QString&viewer){beginAIRound53AiNoTo(r,viewer);}
void invalidateAIRound30TargetRevises(Room&r){retireAIRound53AiNoTo(r);}
QJsonObject queryAIRound30TargetRevises(Room&r,const AIRequest&q,const ServerPlayer*p,const QJsonObject&s){return queryAIRound53AiNoTo(r,q,p,s);}
