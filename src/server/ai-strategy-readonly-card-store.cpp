#include "ai-strategy-readonly-card-store.h"
#include "ai.h"
#include "ai-strategy-round44-certificate.h"
#include "card.h"
#include "card-lifetime-manager.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "structs.h"
#include <QHash>
#include <QSet>
#include <QPointer>
#include <QUuid>
#include <vector>
#include <functional>

namespace {
// Canonical manager retention exposes tokens for direct Card payloads, but its
// self-leasing struct exemption does not expose nested tokens. Reject those
// opaque shapes at issuance instead of assuming leases defeat direct delete.
bool observableTagShape(const QVariant &value, int depth, int &budget)
{
    if (depth > 32 || --budget < 0) return false;
    if (!value.isValid()) return true;
    const int type = value.userType();
    if (type == QMetaType::QVariantList) {
        for (const auto &item : value.toList())
            if (!observableTagShape(item, depth + 1, budget)) return false;
        return true;
    }
    if (type == QMetaType::QVariantMap) {
        const auto map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            if (!observableTagShape(it.value(), depth + 1, budget)) return false;
        return true;
    }
    if (type == qMetaTypeId<CardTagOwner>() || type == qMetaTypeId<CardEffectStruct>()) return true;
    const char *name = QMetaType::typeName(type);
    if (name && (qstrcmp(name, "Card*") == 0 || qstrcmp(name, "const Card*") == 0)) return true;
    switch (type) {
    case QMetaType::Bool: case QMetaType::Int: case QMetaType::UInt:
    case QMetaType::LongLong: case QMetaType::ULongLong: case QMetaType::Double:
    case QMetaType::Float: case QMetaType::QChar: case QMetaType::QString:
    case QMetaType::QStringList: case QMetaType::QByteArray: return true;
    default: return false;
    }
}
}

struct AIOwnerReadonlyCardStore::State {
    struct Entry {
        Card *card = nullptr;
        QString sourceKey;
        bool ownsCard = false;
        std::vector<CardLifetimeLease> leases;
        std::vector<std::shared_ptr<const CardLifetimeToken>> tokens;
        std::vector<QPointer<Card>> objects;
        ~Entry() {
            // Keep dependencies pinned until deletion is requested; all release
            // and eventual destruction follows the existing lifetime manager.
            auto &manager = globalCardLifetimeManager();
            for (size_t i = 0; i < tokens.size(); ++i)
                if (objects[i].isNull() && tokens[i]->live)
                    manager.invalidateIfObserved(tokens[i]);
            if (ownsCard && card && !tokens.empty() && tokens.front()->live && !objects.front().isNull())
                manager.requestNativeDelete(tokens.front());
            globalCardLifetimeManager().releaseVariantTags(this);
        }
    };
    static std::shared_ptr<Entry> retainCard(const Card *);
    static const Card *liveCard(const std::shared_ptr<Entry> &entry) {
        if (!entry) return nullptr;
        for (size_t i = 0; i < entry->tokens.size(); ++i) {
            const auto &token = entry->tokens[i];
            if (entry->objects[i].isNull()) {
                if (token && token->live) globalCardLifetimeManager().invalidateIfObserved(token);
                return nullptr;
            }
            if (!token || !token->live) return nullptr;
        }
        return entry->card;
    }
    Room *room;
    const AIRequest *request;
    QString viewer;
    quint64 decision, revision;
    const void *ledger;
    QString prefix = QUuid::createUuid().toString(QUuid::WithoutBraces);
    quint64 next = 0;
    QHash<QString, std::shared_ptr<Entry>> entries;
    bool matches(ServerPlayer *player, const AIRequest &r) const {
        return player && player->getRoom()==room && &r==request
            && player->objectName()==viewer && r.viewerObjectName==viewer
            && r.decisionId==decision && r.stateRevision==revision
            && r.conversionLedger.get()==ledger && aiRound44RangeRequestCurrent(*room,r,player);
    }
};

AIOwnerReadonlyCardStore::AIOwnerReadonlyCardStore(Room &room, const AIRequest &r)
    : m_state(new State{&room,&r,r.viewerObjectName,r.decisionId,r.stateRevision,r.conversionLedger.get()}) {}
AIOwnerReadonlyCardStore::~AIOwnerReadonlyCardStore() = default;
void AIOwnerReadonlyCardStore::clear() { m_state->entries.clear(); }

std::shared_ptr<AIOwnerReadonlyCardStore::State::Entry>
AIOwnerReadonlyCardStore::State::retainCard(const Card *card)
{
    auto entry=std::make_shared<Entry>();
    auto &manager=globalCardLifetimeManager();
    // Traverse only this producer's already-authorized native object graph.
    // No registry lookup, arbitrary ID lookup or foreign wrapper is performed.
    QSet<const Card *> visiting, retained;
    std::function<bool(const Card *)> retain=[&](const Card *value) {
        if (!value || visiting.contains(value)) return false;
        if (retained.contains(value)) return true;
        if (retained.size()>=128) return false;
        // addChangeEdge can create a non-physical observeLive token. Always
        // upgrade the known live producer object so QObject destruction and
        // native reclamation are tracked, even if that token already exists.
        auto token=manager.observeCard(const_cast<Card *>(value));
        if (!token || !token->live) return false;
        CardLifetimeLease lease(manager,token);
        if (!lease.isValid()) return false;
        entry->leases.push_back(std::move(lease));
        entry->tokens.push_back(token);
        entry->objects.emplace_back(const_cast<Card *>(value));
        visiting.insert(value);retained.insert(value);
        // QVariant values remain entirely native; nested Card payloads need
        // their normal lifecycle pins, not serialization of private tags.
        int tagBudget = 4096;
        if (!observableTagShape(value->tag, 0, tagBudget)
            || !manager.retainVariantTag(entry.get(),QByteArray::number(reinterpret_cast<quintptr>(value)),value->tag)) return false;
        for (const Card *changed:value->getChangeCards()) if (!retain(changed)) return false;
        visiting.remove(value);return true;
    };
    if (!retain(card)) return {};
    // Walk tag edges only through the manager's canonical, already retained
    // tokens. Re-read after each frontier because a referenced Card may itself
    // carry tags; never extract raw pointers from an opaque QVariant here.
    for (;;) {
        bool added = false;
        for (const auto &token : manager.variantTagTokens(entry.get())) {
            if (!token || !token->live) return {};
            const auto *dependency = static_cast<const Card *>(token->address);
            if (!retained.contains(dependency)) {
                if (!retain(dependency)) return {};
                added = true;
            }
        }
        if (!added) break;
    }
    entry->card=const_cast<Card *>(card);
    return entry;
}

QString AIOwnerReadonlyCardStore::issue(ServerPlayer *player, const AIRequest &request,
                                      std::unique_ptr<Card> card, const QString &sourceKey)
{
    if (!m_state->matches(player,request) || !card || card->parent()
        || !card->isVirtualCard()
        || sourceKey.isEmpty() || sourceKey.size()>128 || m_state->entries.size()>=128) return {};
    auto entry=State::retainCard(card.get());
    if (!entry) return {};
    entry->sourceKey=sourceKey;
    entry->card=card.release();
    entry->ownsCard=true;
    const auto key=QStringLiteral("readonly:")+m_state->prefix+QLatin1Char(':')+QString::number(++m_state->next);
    m_state->entries.insert(key,entry);
    return key;
}

QString AIOwnerReadonlyCardStore::issueBorrowed(ServerPlayer *player, const AIRequest &request,
                                               const Card *card, const QString &sourceKey)
{
    if (!m_state->matches(player,request) || !card || sourceKey.isEmpty()
        || sourceKey.size()>128 || m_state->entries.size()>=128) return {};
    auto entry=State::retainCard(card);
    if (!entry) return {};
    entry->sourceKey=sourceKey;
    // The current gameplay owner retains deletion authority. The observation
    // only pins this exact graph through the canonical lifetime manager.
    const auto key=QStringLiteral("readonly:")+m_state->prefix+QLatin1Char(':')+QString::number(++m_state->next);
    m_state->entries.insert(key,entry);
    return key;
}

const Card *AIOwnerReadonlyCardStore::lookup(ServerPlayer *player, const AIRequest &request,
                                          const QString &key) const
{
    if (!m_state->matches(player,request)) return nullptr;
    const auto it=m_state->entries.constFind(key);
    if (it==m_state->entries.cend()) return nullptr;
    return State::liveCard(it.value());
}

QString AIOwnerReadonlyCardStore::sourceKey(ServerPlayer *player, const AIRequest &request,
                                            const QString &key) const
{
    if (!lookup(player, request, key)) return {};
    return m_state->entries.value(key)->sourceKey;
}

AIReadonlyCardLease AIOwnerReadonlyCardStore::retain(ServerPlayer *player, const AIRequest &request,
                                                   const QString &key) const
{
    if (!lookup(player, request, key)) return {};
    // Keep precisely the issued producer's native custody, not a serialized
    // reconstruction. Captured QPointers/generations are checked on every read.
    const auto entry = m_state->entries.value(key);
    return [entry]() { return State::liveCard(entry); };
}

AIReadonlyCardLease AIOwnerReadonlyCardStore::observe(const Card *card)
{
    if (!card || card->isVirtualCard()) return {};
    const auto entry=State::retainCard(card);
    if (!entry) return {};
    return [entry]() { return State::liveCard(entry); };
}

AIReadonlyCardLease AIOwnerReadonlyCardStore::own(std::unique_ptr<Card> card)
{
    if (!card || card->parent() || !card->isVirtualCard()) return {};
    const auto entry=State::retainCard(card.get());
    if (!entry) return {};
    entry->card=card.release(); entry->ownsCard=true;
    return [entry]() { return State::liveCard(entry); };
}
