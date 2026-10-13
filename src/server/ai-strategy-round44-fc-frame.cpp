#include "ai-strategy-round44-fc-frame.h"
#include "ai-strategy-room-ledger.h"
#include "ai.h"
#include "card.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "structs.h"
#include <QObject>
#include <QPointer>
namespace {
const char *ledgerName = "_ai_round44_fc_use_frames";
struct Frame { quint64 serial = 0; const QVariant *data = nullptr; QPointer<Card> card; QPointer<ServerPlayer> from; qint64 history = 0; };
class Frames final : public AIRoomLedger {
public:
 explicit Frames(Room &room):AIRoomLedger(room){setObjectName(ledgerName);}
 quint64 next = 0;
 QList<Frame> stack;
};
Frames *frames(Room &room, bool create = false) {
 for(auto *child:room.children()) if(child->objectName()==ledgerName) return dynamic_cast<Frames *>(child);
 return create ? new Frames(room) : nullptr;
}
bool question(const AIRequest &r) {
 const auto k=r.choiceOptions.reason;
 if(k=="sp_hengsaoqianjun") return r.kind==AIRequest::Choice||r.kind==AIRequest::PlayersChosen;
 if(k=="sp_lvezhen"||k=="fcjielianhuan") return r.kind==AIRequest::PlayerChosen;
 return k=="fcmoujiang"&&r.kind==AIRequest::PlayersChosen;
}
bool currentOwner(const AIRequest &r,Room &room,const ServerPlayer *p) {
 return p&&p->getRoom()==&room&&r.viewerObjectName==p->objectName()
  &&r.stateRevision==room.roomRuntime()->stateRevision()&&question(r)&&p->hasSkill(r.choiceOptions.reason);
}
bool validUse(Room &room,const CardUseStruct &u) {
 if(!u.card||!u.from||!room.getAllPlayers(true).contains(u.from))return false;
 for(auto *t:u.to)if(!t||!room.getAllPlayers(true).contains(t))return false;
 return true;
}
}
AIRound44FCUseFrame::AIRound44FCUseFrame(Room &room,int event,ServerPlayer *target,const QVariant &data) {
 if(event!=PreCardUsed&&event!=CardUsed&&event!=TargetSpecified&&event!=CardFinished)return;
 auto *state=frames(room,true); Frame f; f.serial=++state->next; f.data=&data;
 if(data.canConvert<CardUseStruct>()) {
  const auto use=data.value<CardUseStruct>();
  if(validUse(room,use)&&use.from==target) { f.card=const_cast<Card *>(use.card); f.from=use.from; f.history=use.targetModReveal.useHistoryEventId; }
 }
 state->stack.append(f);m_room=&room;m_serial=f.serial;
}
AIRound44FCUseFrame::~AIRound44FCUseFrame() {
 if(!m_room)return;auto *s=frames(*m_room);if(!s)return;
 for(int i=s->stack.size()-1;i>=0;--i)if(s->stack[i].serial==m_serial){s->stack.removeAt(i);break;}
}
void clearAIRound44FCUseFrames(Room &room) { if(auto *s=frames(room))s->stack.clear(); }
void bindAIRound44FCUseFrame(AIRequest &r,Room &room,const ServerPlayer *p) {
 // Immutable native request token: re-projection cannot move it to an inner use.
 if(r.round44FCUseFrameBound||!currentOwner(r,room,p))return;
 const auto *s=frames(room);if(!s||s->stack.isEmpty())return;
 const auto &f=s->stack.last();
 if(f.from==p){r.round44FCUseFrameBound=true;r.round44FCUseFrameSerial=f.serial;}
}
int aiRound44FCUseFrameWitness(const AIRequest &r,Room &room,const ServerPlayer *p,const CardUseStruct &use) {
 const auto *s=frames(room);const bool active=s&&!s->stack.isEmpty();
 if(!r.round44FCUseFrameBound&&!active)return 0;
 if(!active||!currentOwner(r,room,p)||!r.round44FCUseFrameBound)return -1;
 const auto &f=s->stack.last();
 if(r.round44FCUseFrameSerial!=f.serial||!f.card||f.from!=p||!f.data
  ||!f.data->canConvert<CardUseStruct>())return -1;
 const auto live=f.data->value<CardUseStruct>();
 if(!validUse(room,live)||!validUse(room,use)||live.card!=f.card.data()||live.from!=f.from.data()
  ||live.targetModReveal.useHistoryEventId!=f.history||use.card!=live.card||use.from!=live.from
  ||use.targetModReveal.useHistoryEventId!=live.targetModReveal.useHistoryEventId||use.to!=live.to)return -1;
 return 1;
}
