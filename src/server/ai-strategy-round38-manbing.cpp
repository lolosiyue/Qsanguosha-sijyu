#include "ai-strategy-round38-manbing.h"
#include "ai-strategy-flag-lifecycle.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QHash>
#include <QObject>
namespace {
struct Owner { qint64 epoch=0; bool diverged=false; bool baselineKnown=true; QHash<QString,bool> values; };
class Ledger final:public QObject {
public:explicit Ledger(Room&r):QObject(&r){}
 bool initialized=false;
 QHash<const ServerPlayer*,Owner> owners;
};
Ledger*ledger(Room&r){for(QObject*c:r.children())if(auto*l=dynamic_cast<Ledger*>(c))return l;return new Ledger(r);}
bool belongs(Room&r,const ServerPlayer*p){return p&&p->getRoom()==&r&&r.getAllPlayers(true).contains(const_cast<ServerPlayer*>(p));}
qint64 epoch(Room&r,const ServerPlayer*p){AIWorldView w;w.self.objectName=p->objectName();projectAIStrategyFlagLifecycle(w,r);return qint64(w.strategyFacts.value("strategy_flag_clear_epochs").toObject().value(p->objectName()).toDouble());}
QJsonObject fail(const char*reason){return{{"kind","manbing_flag"},{"known",false},{"reason",QString::fromLatin1(reason)}};}
Owner&owner(Room&r,const ServerPlayer*p){
 auto*l=ledger(r);const auto e=epoch(r,p);auto it=l->owners.find(p);
 if(it==l->owners.end()){
  QObject::connect(p,&QObject::destroyed,l,[l,p]{l->owners.remove(p);});
  Owner row;row.epoch=e;
  it=l->owners.insert(p,row);
 }
 if(it->epoch!=e){it->epoch=e;it->diverged=false;it->baselineKnown=true;it->values.clear();}
 return it.value();
}
}
void initializeAIRound38Manbing(Room&r){auto*l=ledger(r);if(l->initialized)return;l->initialized=true;for(const auto*p:r.getAllPlayers(true))owner(r,p);}
void invalidateAIRound38Manbing(Room&r){
 // Before any shadow write, current native owner flags are the exact source
 // state, including all startup CloneAI/legacy writes. Only divergence makes
 // subsequent untracked legacy execution ambiguous.
 for(const auto*p:r.getAllPlayers(true)){
  auto&row=owner(r,p);
  if(row.diverged){row.baselineKnown=false;row.values.clear();}
 }
}
void observeAIRound38ManbingFlag(Room&r,const ServerPlayer*p,const QString&flag){
 if(!belongs(r,p))return;
 bool remove=flag.startsWith('-');const QString key=remove?flag.mid(1):flag;
 const QString prefix=QStringLiteral("manbingTo_");if(!key.startsWith(prefix))return;
 const QString target=key.mid(prefix.size());if(!r.findPlayerByObjectName(target,true))return;
 // A real external named operation may supersede speculative writes even
 // when native state already lacks that flag. Fail closed until a new reached
 // write or a true all-flags clear establishes the next source value.
 auto&row=owner(r,p);if(row.diverged){row.baselineKnown=false;row.values.clear();}
}
QJsonObject queryAIRound38Manbing(Room&r,const AIRequest&q,const ServerPlayer*p,const QString&targetName,bool write){
 if(!belongs(r,p)||!r.roomRuntime()||q.viewerObjectName!=p->objectName()
  ||q.worldView.self.objectName!=p->objectName()||q.stateRevision!=r.roomRuntime()->stateRevision())return fail("Manbing active owner/request revision mismatch");
 const auto*t=r.findPlayerByObjectName(targetName,true);
 if(!belongs(r,t)||t->objectName()!=targetName)return fail("Manbing target is outside current roster");
 auto*l=ledger(r);if(!l->initialized)return fail("Manbing baseline was not initialized before policies");
 auto&row=owner(r,p);
 if(!row.diverged){
  if(!write)return{{"kind","manbing_flag"},{"known",true},{"value",p->hasFlag("manbingTo_"+targetName)}};
  row.values.clear();row.baselineKnown=true;
  for(const auto*other:r.getAllPlayers(true))if(p->hasFlag("manbingTo_"+other->objectName()))row.values.insert(other->objectName(),true);
  row.diverged=true;
 }
 if(write)row.values.insert(targetName,true);
 if(!row.values.contains(targetName)&&!row.baselineKnown)return fail("legacy policy interrupted Manbing owner flag continuity");
 return{{"kind","manbing_flag"},{"known",true},{"value",row.values.value(targetName,false)}};
}
