#include "ai-strategy-round56-host.h"
#include "room.h"
#include <atomic>
#include <typeinfo>
namespace {
std::atomic<quint64> sinkEpoch{0};
std::atomic<bool> sinkKnown{false},defaultAttempted{false};
thread_local AIRound56ReviewedLogConnect *connection=nullptr;
}
void initializeAIRound56DefaultMessageSink() noexcept {
 bool expected=false;
 if(defaultAttempted.compare_exchange_strong(expected,true)&&sinkEpoch.load()==0){
  sinkEpoch.store(1);sinkKnown.store(true);
 }
}
void recordAIRound56ServerMessageSink(bool installed) noexcept {
 sinkKnown.store(false);sinkEpoch.fetch_add(1);sinkKnown.store(installed);
}
quint64 aiRound56ServerMessageSinkEpoch() noexcept {
 return sinkKnown.load()?sinkEpoch.load():0;
}
bool proveAIRound56ServerLogEffects(const Room&r) noexcept {
 return typeid(r)==typeid(Room)
     &&aiRound56ServerMessageSinkEpoch()!=0&&r.round56LogConnectionsReviewed();
}
AIRound56ReviewedLogConnect::AIRound56ReviewedLogConnect(const QObject*s) noexcept
 :m_sender(s),m_previous(connection){connection=this;}
AIRound56ReviewedLogConnect::~AIRound56ReviewedLogConnect(){connection=m_previous;}
bool AIRound56ReviewedLogConnect::matches(const QObject*s) noexcept {
 return connection&&connection->m_sender==s;
}
