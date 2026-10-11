#include "ai-strategy-response-observer.h"
#include "ai-strategy-public-action-flags.h"
#include "ai-strategy-round47-xiedou.h"
#include "ai-strategy-card-identity.h"
#include "card-lifetime-manager.h"
#include "card.h"
#include "room.h"
#include "serverplayer.h"
#include <QHash>
#include <QUuid>
namespace {
struct Entry { std::weak_ptr<const CardLifetimeToken> token; QString label; };
struct ViewerIdentity {
    QHash<const CardLifetimeToken *, Entry> entries;
    QString prefix = QUuid::createUuid().toString(QUuid::WithoutBraces);
    quint64 next = 0;
};
class IdentityStore final : public QObject {
public:
    explicit IdentityStore(Room &room) : QObject(&room) {}
    QHash<QString, ViewerIdentity> viewers;
};
thread_local Room *activeRoom = nullptr;
thread_local ServerPlayer *activeViewer = nullptr;
}
QString aiLegacyCardIdentity(Room &room, ServerPlayer *viewer,
                             const std::shared_ptr<const CardLifetimeToken> &token)
{
    if (!viewer || viewer->getRoom() != &room || !token || !token->live) return {};
    IdentityStore *store = nullptr;
    for (QObject *child : room.children()) if ((store = dynamic_cast<IdentityStore *>(child))) break;
    if (!store) store = new IdentityStore(room);
    auto &space = store->viewers[viewer->objectName()];
    auto &entries = space.entries;
    auto found = entries.find(token.get());
    if (found != entries.end() && found->token.lock() == token) return found->label;
    // Weak references neither keep cards alive nor confuse recycled addresses.
    for (auto i = entries.begin(); i != entries.end();) {
        if (i->token.expired()) i = entries.erase(i); else ++i;
    }
    const QString label = QStringLiteral("native:") + space.prefix + QLatin1Char(':') + QString::number(++space.next);
    entries.insert(token.get(), Entry{token, label}); return label;
}
QString aiLegacyCardIdentity(Room &room, ServerPlayer *viewer, const Card *card)
{
    if (!card || !viewer || viewer->getRoom() != &room) return {};
    auto &lifetime = globalCardLifetimeManager();
    auto token = lifetime.liveToken(card);
    // A lawful fresh native clone may not have crossed SWIG yet. Observe the
    // actual live object exactly as SWIG does; do not adopt it or hold a lease.
    if (!token) token = lifetime.observeCard(const_cast<Card *>(card));
    return aiLegacyCardIdentity(room, viewer, token);
}
QString aiScopedLegacyCardIdentity(const Card *card)
{
    return activeRoom ? aiLegacyCardIdentity(*activeRoom, activeViewer, card) : QString();
}
AIIdentityProjectionScope::AIIdentityProjectionScope(Room &room, ServerPlayer *viewer)
    : previousRoom(activeRoom), previousViewer(activeViewer)
{ activeRoom = &room; activeViewer = viewer; }
AIIdentityProjectionScope::~AIIdentityProjectionScope()
{ activeRoom = previousRoom; activeViewer = previousViewer; }

QJsonObject readAIStrategyScopedPublicCardFlags(const Card *card)
{
    if (!activeRoom || !activeViewer) return {};
    auto flags = readAIStrategyPublicCardFlags(*activeRoom, activeViewer, card);
    const auto fresh = readAIRound47XiedouFlags(*activeRoom, activeViewer, card);
    if (fresh.contains("nosjiefan-slash")) flags.insert("nosjiefan-slash", fresh.value("nosjiefan-slash"));
    const auto observation = readAIResponseCardObservation(*activeRoom, activeViewer, card);
    if (observation.contains("dwqingshidamage")) flags.insert("dwqingshidamage", observation.value("dwqingshidamage"));
    return flags;
}
