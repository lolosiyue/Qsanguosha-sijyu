#include "excel-view.h"

#include "../client/core/client-core.h"
#include "../client/client-log-formatter.h"
#include "../core/audio.h"
#include "../core/card.h"
#include "../core/engine.h"
#include "../core/general.h"
#include "../core/settings.h"
#include "../server/server-config.h"
#include "../core/skill.h"
#include "../package/package.h"
#include "../core/protocol.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>

namespace {

QJsonArray strings(const QStringList &values)
{
    QJsonArray result;
    for (const QString &value : values)
        result.append(value);
    return result;
}

QJsonObject row(const QString &id, const QString &label, bool enabled = true)
{
    QJsonObject result{{QStringLiteral("id"), id}, {QStringLiteral("rowid"), id},
                       {QStringLiteral("label"), label},
                       {QStringLiteral("enabled"), enabled}};
    return result;
}

QString plainText(QString text)
{
    // Workbook cells are plain text: remove rich-text markup and control bytes.
    text.replace(QRegularExpression(QStringLiteral("<br\\s*/?>"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("\n"));
    text.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    text.remove(QRegularExpression(QStringLiteral("[\\x00-\\x08\\x0b\\x0c\\x0e-\\x1f\\x7f]")));
    return text.left(30000);
}

QString imagePath(const QString &root, const QString &name)
{
    if (root.isEmpty() || name.isEmpty())
        return QString();
    const QString base = QDir(root).absoluteFilePath(QStringLiteral("image"));
    const QString candidate = QDir::fromNativeSeparators(QFileInfo(
        QDir(base).absoluteFilePath(name)).canonicalFilePath());
    const QString canonicalBase = QDir::fromNativeSeparators(QFileInfo(base).canonicalFilePath());
    if (candidate.isEmpty() || canonicalBase.isEmpty()
        || !candidate.startsWith(canonicalBase + QLatin1Char('/'), Qt::CaseInsensitive))
        return QString();
    return QFileInfo(candidate).isFile() ? candidate : QString();
}

QString faceImage(const QString &root, const QString &name, bool general)
{
    if (name.isEmpty()) return QString();
    const QStringList prefixes = general
        ? QStringList{QStringLiteral("generals/card/"), QStringLiteral("generals/avatar/")}
        : QStringList{QStringLiteral("card/"), QStringLiteral("big-card/")};
    for (const QString &prefix : prefixes) {
        for (const QString &suffix : {QStringLiteral(".jpg"), QStringLiteral(".png")}) {
            const QString path = imagePath(root, prefix + name + suffix);
            if (!path.isEmpty()) return path;
        }
    }
    return QString();
}

QString generalImage(const QString &root, const General *general)
{
    if (!general) return QString();
    const QString alias = general->getImage();
    if (!alias.isEmpty()) {
        const QString explicitPath = imagePath(root, alias);
        if (!explicitPath.isEmpty()) return explicitPath;
        const QString aliased = faceImage(root, alias, true);
        if (!aliased.isEmpty()) return aliased;
    }
    return faceImage(root, general->objectName(), true);
}

QJsonObject primitiveSettings()
{
    QVariantMap values = defaultServerConfigValues();
    const QStringList keys = Config.allKeys();
    for (const QString &key : keys) {
        const QString lower = key.toLower();
        if (lower.contains(QStringLiteral("token")) || lower.contains(QStringLiteral("secret"))
            || lower.contains(QStringLiteral("password")) || lower.contains(QStringLiteral("credential")))
            continue;
        const QVariant value = Config.value(key);
        const int type = value.userType();
        if (type == QMetaType::Bool || type == QMetaType::Int
            || type == QMetaType::UInt || type == QMetaType::LongLong
            || type == QMetaType::ULongLong || type == QMetaType::QVariantList
            || type == QMetaType::QVariantMap
            || type == QMetaType::Double || type == QMetaType::QString
            || type == QMetaType::QStringList)
            values.insert(key, value);
    }
    return QJsonObject::fromVariantMap(values);
}

QJsonObject cardDetails(const ClientCore &core, const QString &key, const QString &assetRoot)
{
    bool ok = false;
    const int id = key.toInt(&ok);
    if (!ok || id < 0)
        return QJsonObject();
    QVariantMap value = core.state()->card(id);
    if (value.isEmpty())
        return QJsonObject();
    value.remove(QStringLiteral("image"));
    const bool visible = (!core.state()->selfName().isEmpty()
        && value.value(QStringLiteral("owner")).toString() == core.state()->selfName())
        || (value.contains(QStringLiteral("place"))
            && value.value(QStringLiteral("place")).toInt() != 0);
    if (!visible) {
        // Do not enrich a hidden record from the engine or retain stale faces.
        return {{QStringLiteral("id"), id}, {QStringLiteral("hidden"), true},
            {QStringLiteral("owner"), value.value(QStringLiteral("owner")).toString()}};
    }
    if (visible && Sanguosha != nullptr && id < Sanguosha->getCardCount()) {
        const Card *card = Sanguosha->getEngineCard(id);
        if (card != nullptr) {
            value.insert(QStringLiteral("id"), id);
            QString name = card->objectName();
            if (value.value(QStringLiteral("modified")).toBool()) {
                name = value.value(QStringLiteral("object_name")).toString();
                if (name.isEmpty()) name = value.value(QStringLiteral("card_name"), card->objectName()).toString();
            }
            value.insert(QStringLiteral("name"), name);
            value.insert(QStringLiteral("object_name"), name);
            value.insert(QStringLiteral("label"), plainText(Sanguosha->translate(name)));
            value.insert(QStringLiteral("package"), card->getPackage());
            value.insert(QStringLiteral("class"), card->getClassName());
            if (!value.value(QStringLiteral("modified")).toBool()) {
                value.insert(QStringLiteral("suit"), card->getSuitString());
                value.insert(QStringLiteral("number"), card->getNumber());
            }
            value.insert(QStringLiteral("description"), plainText(Sanguosha->translate(QStringLiteral(":") + name)));
            const QString image = faceImage(assetRoot, name, false);
            if (!image.isEmpty()) value.insert(QStringLiteral("image"), image);
        }
    }
    value.insert(QStringLiteral("rowid"), key);
    value.insert(QStringLiteral("enabled"), true);
    return QJsonObject::fromVariantMap(value);
}

QString safePlayerName(const ClientCore &core, const QString &name)
{
    const QVariantMap player = core.state()->player(name);
    const QString screen = player.value(QStringLiteral("screen_name")).toString();
    return screen.isEmpty() ? name : screen;
}

ClientLogFormatRequest logRequest(const QVariantMap &payload)
{
    ClientLogFormatRequest request;
    request.type = payload.value(QStringLiteral("log_type")).toString();
    request.from = payload.value(QStringLiteral("from_player")).toString();
    request.tos = payload.value(QStringLiteral("to_players")).toStringList();
    request.cardString = payload.value(QStringLiteral("card_string")).toString();
    const QStringList args = payload.value(QStringLiteral("arguments")).toStringList();
    if (args.size() > 0) request.arg = args.at(0);
    if (args.size() > 1) request.arg2 = args.at(1);
    if (args.size() > 2) request.arg3 = args.at(2);
    if (args.size() > 3) request.arg4 = args.at(3);
    if (args.size() > 4) request.arg5 = args.at(4);
    return request;
}

ClientLogFormatStyle logStyle(ClientCore &core)
{
    ClientLogFormatStyle style;
    style.phrases = engineUseCardPhrases();
    style.cardJoin = QStringLiteral(", ");
    style.toJoin = QStringLiteral(", ");
    style.translate = [](const QString &key) {
        if (Sanguosha == nullptr) return key;
        const QString value = Sanguosha->translate(key);
        return value.isEmpty() ? key : value;
    };
    style.cardById = [](int id, bool roomCard) -> const Card * {
        if (Sanguosha == nullptr) return nullptr;
        return roomCard ? Sanguosha->getCard(id) : Sanguosha->getEngineCard(id);
    };
    style.cardLogName = [](const Card *card) {
        return card == nullptr ? QString() : card->getLogName();
    };
    style.playerName = [&core](const QString &name) { return safePlayerName(core, name); };
    return style;
}

} // namespace

namespace ExcelView {

QJsonObject interactionUi(const ClientCore &core, const QString &assetRoot,
                         const QJsonObject &selection)
{
    if (!core.hasActiveRequest()) return {};
    const InteractionRequest &request = core.activeRequest();
    const QJsonObject payload = request.toJson().value(QStringLiteral("payload")).toObject();
    QJsonArray options, cards, players, skills, declarations;
    const auto label = [](const QString &name) {
        return plainText(Sanguosha ? Sanguosha->translate(name) : name);
    };
    for (const QJsonValue &entry : payload.value(QStringLiteral("options")).toArray()) {
        const QJsonObject source = entry.toObject();
        const QString id = source.value(QStringLiteral("response_value")).toString(
            source.value(QStringLiteral("value")).toString());
        const QString name = source.value(QStringLiteral("skill")).toString(id);
        QJsonObject item = row(id, label(source.value(QStringLiteral("label")).toString(name)),
            source.value(QStringLiteral("enabled")).toBool(true));
        if (Sanguosha) {
            const General *general = Sanguosha->getGeneral(id);
            if (general) item.insert(QStringLiteral("image"), generalImage(assetRoot, general));
        }
        item.insert(QStringLiteral("detail"), label(QStringLiteral(":") + name));
        options.append(item);
    }
    QList<int> offered, disabled;
    const auto appendIds = [](const QJsonArray &array, QList<int> *output) {
        for (const QJsonValue &entry : array)
            if (entry.isDouble() && !output->contains(entry.toInt())) output->append(entry.toInt());
    };
    for (const QString &key : {QStringLiteral("selectable_cards"), QStringLiteral("visible_cards"), QStringLiteral("cards")})
        appendIds(payload.value(key).toArray(), &offered);
    appendIds(payload.value(QStringLiteral("disabled_cards")).toArray(), &disabled);
    const QList<int> explicitlyOffered = offered;
    appendIds(selection.value(QStringLiteral("selectable_cards")).toArray(), &offered);
    appendIds(selection.value(QStringLiteral("draft")).toObject().value(QStringLiteral("cards")).toArray(), &offered);
    if (const auto *value = request.payloadAs<CardInteractionPayload>()) {
        if (!value->selection.enumerated)
            for (int id : core.state()->cardsForPlayer(core.state()->selfName(), 0))
                if (!offered.contains(id)) offered.append(id);
        if (value->hiddenHandCount > 0 && request.type == InteractionType::ChooseCard) offered.append(-1);
    }
    for (int id : offered) {
        QJsonObject item;
        if (id == -1) item = row(QStringLiteral("-1"), QStringLiteral("隨機選取一張暗置手牌"));
        else {
            item = cardDetails(core, QString::number(id), assetRoot);
            // Explicitly offered faces (AG, Gongxin, Guanxing) are authorized
            // by this request even when there is no persistent zone record.
            if ((item.isEmpty() || item.value(QStringLiteral("hidden")).toBool())
                && explicitlyOffered.contains(id) && Sanguosha && id >= 0 && id < Sanguosha->getCardCount()) {
                const Card *card = Sanguosha->getEngineCard(id);
                if (card) {
                    item = row(QString::number(id), label(card->objectName()));
                    item.insert(QStringLiteral("image"), faceImage(assetRoot, card->objectName(), false));
                    item.insert(QStringLiteral("detail"), label(QStringLiteral(":") + card->objectName()));
                }
            }
            if (item.isEmpty() || item.value(QStringLiteral("hidden")).toBool()) continue;
            item.insert(QStringLiteral("id"), QString::number(id));
            item.insert(QStringLiteral("enabled"), !disabled.contains(id));
            if (const auto *gongxin = request.payloadAs<GongxinInteractionPayload>())
                item.insert(QStringLiteral("enabled"), gongxin->selectableCards.contains(id));
        }
        cards.append(item);
    }
    QStringList candidates;
    for (const QString &key : {QStringLiteral("selectable_players"), QStringLiteral("target_players"), QStringLiteral("players")})
        for (const QJsonValue &entry : payload.value(key).toArray())
            if (entry.isString() && !candidates.contains(entry.toString())) candidates.append(entry.toString());
    if (candidates.isEmpty()) candidates = core.state()->playerNames();
    for (const QString &name : candidates) players.append(row(name, plainText(safePlayerName(core, name))));
    QJsonArray skillSources = selection.value(QStringLiteral("skills")).toArray();
    if (skillSources.isEmpty()) skillSources = payload.value(QStringLiteral("skill_candidates")).toArray();
    for (const QJsonValue &entry : skillSources) {
        QJsonObject item = entry.toObject();
        const QString name = item.value(QStringLiteral("name")).toString(item.value(QStringLiteral("skill")).toString());
        if (name.isEmpty()) continue;
        item.insert(QStringLiteral("name"), name);
        item.insert(QStringLiteral("id"), name + QLatin1Char(':') + QString::number(item.value(QStringLiteral("instance_id")).toInt()));
        item.insert(QStringLiteral("label"), label(name));
        item.insert(QStringLiteral("enabled"), item.value(QStringLiteral("available")).toBool(true));
        item.insert(QStringLiteral("detail"), label(QStringLiteral(":") + name));
        skills.append(item);
    }
    for (const QJsonValue &entry : selection.value(QStringLiteral("declarations")).toArray())
        declarations.append(row(entry.toString(), label(entry.toString())));
    return {{QStringLiteral("options"), options}, {QStringLiteral("cards"), cards},
        {QStringLiteral("players"), players}, {QStringLiteral("skills"), skills},
        {QStringLiteral("declarations"), declarations}};
}

QJsonObject catalog(const ClientCore &, const QString &assetRoot, bool legacy)
{
    QJsonObject result;
    QJsonArray modes;
    if (Sanguosha != nullptr) {
        const QMap<QString, GameModeStruct> available = Sanguosha->getAvailableModes();
        for (auto it = available.cbegin(); it != available.cend(); ++it) {
            const int count = Sanguosha->getPlayerCount(it.key());
            QJsonObject item = row(it.key(), plainText(Sanguosha->getModeName(it.key())),
                count >= 2 && (!legacy || count <= 10));
            item.insert(QStringLiteral("player_count"), count);
            item.insert(QStringLiteral("roles"), it.value().roles);
            modes.append(item);
        }
    }
    QJsonArray packages;
    if (Sanguosha != nullptr) {
        for (const Package *package : Sanguosha->getPackages()) {
            if (package == nullptr) continue;
            QJsonObject item = row(package->objectName(), plainText(Sanguosha->translate(package->objectName())), !package->isForbid());
            item.insert(QStringLiteral("type"), static_cast<int>(package->getType()));
            item.insert(QStringLiteral("metadata"), QJsonObject{{QStringLiteral("adder"), package->adderName()}});
            packages.append(item);
        }
    }
    QJsonArray generals;
    if (Sanguosha != nullptr) {
        for (const General *general : Sanguosha->getAllGenerals()) {
            if (general == nullptr || general->isTotallyHidden()) continue;
            QJsonObject item = row(general->objectName(), plainText(Sanguosha->translate(general->objectName())));
            item.insert(QStringLiteral("metadata"), QJsonObject{{QStringLiteral("kingdom"), general->getKingdom()},
                {QStringLiteral("package"), general->getPackage()}, {QStringLiteral("max_hp"), general->getMaxHp()}});
            const QString image = generalImage(assetRoot, general);
            if (!image.isEmpty()) item.insert(QStringLiteral("image"), image);
            generals.append(item);
        }
    }
    result.insert(QStringLiteral("modes"), modes);
    result.insert(QStringLiteral("packages"), packages);
    result.insert(QStringLiteral("settings"), primitiveSettings());
    result.insert(QStringLiteral("generals"), generals);
    result.insert(QStringLiteral("runtime_tier"), legacy ? QStringLiteral("legacy") : QStringLiteral("modern"));
    // Zero is the shared admission contract for no frontend-specific limit.
    result.insert(QStringLiteral("max_players"), legacy ? 10 : 0);
    return result;
}

QJsonObject snapshotView(const ClientCore &core, const QString &assetRoot, const QStringList &logs)
{
    QJsonObject view;
    const ClientGameState *state = core.state();
    view.insert(QStringLiteral("status"), state->gameValue(QStringLiteral("status")).toString());
    view.insert(QStringLiteral("prompt"), core.hasActiveRequest() ? plainText(core.activeRequest().prompt) : QString());
    QJsonArray players;
    for (const QString &name : state->playerNames()) {
        const QVariantMap source = state->player(name);
        QJsonObject item = QJsonObject::fromVariantMap(source);
        item.insert(QStringLiteral("id"), name);
        item.insert(QStringLiteral("label"), plainText(safePlayerName(core, name)));
        item.insert(QStringLiteral("seat"), source.value(QStringLiteral("seat")).toInt());
        // Hidden opponent cards have no face records; retain the public count.
        item.insert(QStringLiteral("hand_count"), source.value(QStringLiteral("hand_count"),
            name == state->selfName() ? state->cardsForPlayer(name, 0).size() : 0).toInt());
        item.remove(QStringLiteral("hand"));
        for (const QString &field : {QStringLiteral("general"), QStringLiteral("deputy_general")}) {
            const QString general = source.value(field).toString();
            if (general.isEmpty() || !Sanguosha) continue;
            item.insert(field + QStringLiteral("_label"), plainText(Sanguosha->translate(general)));
            const QString image = generalImage(assetRoot, Sanguosha->getGeneral(general));
            if (!image.isEmpty()) item.insert(field + QStringLiteral("_image"), image);
        }
        QJsonArray equip;
        for (int id : state->cardsForPlayer(name, 1))
            equip.append(cardDetails(core, QString::number(id), assetRoot));
        item.insert(QStringLiteral("equip"), equip);
        players.append(item);
    }
    QJsonArray hand;
    QJsonArray publicCards;
    for (int id : state->cardsForPlayer(state->selfName(), 0)) {
        hand.append(cardDetails(core, QString::number(id), assetRoot));
    }
    // Card records from the reducer are already authorized; expose only public
    // zones here and never copy an opponent's face-down hand into the view.
    for (int id : state->cardIds()) {
        const QVariantMap source = state->card(id);
        if (!source.contains(QStringLiteral("place")) || source.value(QStringLiteral("place")).toInt() == 0)
            continue;
        publicCards.append(cardDetails(core, QString::number(id), assetRoot));
    }
    QJsonArray skills;
    QSet<QString> instancedSkills;
    const QVariantMap instances = state->playerValue(state->selfName(), QStringLiteral("skill_instances")).toMap();
    for (auto it = instances.constBegin(); it != instances.constEnd(); ++it) {
        const QVariantMap source = it.value().toMap();
        const QString name = source.value(QStringLiteral("skill_name")).toString();
        const int instance = source.value(QStringLiteral("instance_id")).toInt();
        if (name.isEmpty() || instance <= 0) continue;
        instancedSkills.insert(name);
        if (!source.value(QStringLiteral("visible"), true).toBool()) continue;
        QJsonObject item = QJsonObject::fromVariantMap(source);
        item.insert(QStringLiteral("id"), name);
        item.insert(QStringLiteral("rowid"), name + QLatin1Char(':') + QString::number(instance));
        item.insert(QStringLiteral("name"), name);
        item.insert(QStringLiteral("skill_instance_id"), instance);
        item.insert(QStringLiteral("label"), plainText(Sanguosha ? Sanguosha->translate(name) : name));
        // Native preflight determines activation legality; presentation does not.
        item.insert(QStringLiteral("enabled"), true);
        skills.append(item);
    }
    const QVariantList skillList = state->playerValue(state->selfName(), QStringLiteral("skills")).toList();
    for (const QVariant &entry : skillList) {
        const QVariantMap source = entry.toMap();
        if (!source.isEmpty()) {
            QJsonObject item = QJsonObject::fromVariantMap(source);
            const QString name = source.value(QStringLiteral("name"),
                                              source.value(QStringLiteral("skill"))).toString();
            if (!name.isEmpty() && !instancedSkills.contains(name)) {
                item.insert(QStringLiteral("id"), name);
                item.insert(QStringLiteral("label"),
                    plainText(Sanguosha ? Sanguosha->translate(name) : name));
                item.insert(QStringLiteral("enabled"), source.value(QStringLiteral("enabled"), true).toBool());
                item.insert(QStringLiteral("instance_id"), 0);
                skills.append(item);
            }
        } else {
            const QString name = entry.toString();
            if (!name.isEmpty() && !instancedSkills.contains(name)) {
                QJsonObject item = row(name, plainText(Sanguosha ? Sanguosha->translate(name) : name));
                item.insert(QStringLiteral("name"), name);
                item.insert(QStringLiteral("instance_id"), 0);
                skills.append(item);
            }
        }
    }
    view.insert(QStringLiteral("players"), players);
    view.insert(QStringLiteral("hand"), hand);
    view.insert(QStringLiteral("cards"), publicCards);
    view.insert(QStringLiteral("skills"), skills);
    QStringList formattedLogs;
    for (const QString &line : logs) formattedLogs.append(plainText(line));
    view.insert(QStringLiteral("logs"), strings(formattedLogs));
    view.insert(QStringLiteral("game_over"), state->gameValue(QStringLiteral("game_over")).toBool());
    return view;
}

QJsonObject details(const ClientCore &core, const QString &assetRoot, const QString &kind,
                    const QString &key, QString *error)
{
    QJsonObject result;
    if (error) error->clear();
    if (kind == QLatin1String("card")) result = cardDetails(core, key, assetRoot);
    else if (Sanguosha != nullptr && kind == QLatin1String("general")) {
        const General *value = Sanguosha->getGeneral(key);
        if (value) result = QJsonObject{{QStringLiteral("id"), key}, {QStringLiteral("label"), plainText(Sanguosha->translate(key))},
            {QStringLiteral("kingdom"), value->getKingdom()}, {QStringLiteral("max_hp"), value->getMaxHp()},
            {QStringLiteral("description"), plainText(value->getSkillDescription(true))}};
        if (value) {
            QStringList skills;
            for (const Skill *skill : value->getVisibleSkillList()) skills.append(skill->objectName());
            result.insert(QStringLiteral("skills"), strings(skills));
            const QString image = generalImage(assetRoot, value);
            if (!image.isEmpty()) result.insert(QStringLiteral("image"), image);
        }
    } else if (Sanguosha != nullptr && kind == QLatin1String("skill")) {
        const Skill *value = Sanguosha->getSkill(key);
        if (value) result = QJsonObject{{QStringLiteral("id"), key}, {QStringLiteral("label"), plainText(Sanguosha->translate(key))},
            {QStringLiteral("description"), plainText(value->getDescription())}};
    } else if (kind == QLatin1String("player")) {
        for (const QJsonValue &player : snapshotView(core, assetRoot).value(QStringLiteral("players")).toArray()) {
            if (player.toObject().value(QStringLiteral("id")).toString() == key) {
                result = player.toObject(); break;
            }
        }
    } else if (kind == QLatin1String("pile")) {
        // Named piles are reducer player fields; -1 remains a hidden back.
        QString owner = core.state()->selfName();
        QString pile = key;
        const int separator = key.indexOf(QLatin1Char(':'));
        if (separator >= 0 && core.state()->playerNames().contains(key.left(separator))) {
            owner = key.left(separator);
            pile = key.mid(separator + 1);
        }
        const QVariantMap piles = core.state()->playerValue(owner, QStringLiteral("piles")).toMap();
        if (piles.contains(pile)) {
            QJsonArray cards;
            for (const QVariant &entry : piles.value(pile).toList()) {
                const int id = entry.toInt();
                cards.append(id < 0 ? QJsonObject{{QStringLiteral("hidden"), true}}
                    : cardDetails(core, QString::number(id), assetRoot));
            }
            result = {{QStringLiteral("id"), key}, {QStringLiteral("owner"), owner},
                {QStringLiteral("label"), plainText(Sanguosha ? Sanguosha->translate(pile) : pile)},
                {QStringLiteral("cards"), cards}, {QStringLiteral("count"), cards.size()}};
        }
    }
    if (result.isEmpty() && error) *error = QStringLiteral("detail_not_found");
    return result;
}

QString presentationText(ClientCore &core, int command, const QString &fallback, const QVariant &payload)
{
    if (command == QSanProtocol::S_COMMAND_LOG_SKILL) {
        const QString text = formatClientLog(logRequest(payload.toMap()), logStyle(core));
        return plainText(text);
    }
    if (command == QSanProtocol::S_COMMAND_LOG_EVENT) {
        const QVariantMap event = payload.toMap();
        ClientLogFormatRequest request;
        request.from = event.value(QStringLiteral("player_name")).toString();
        switch (event.value(QStringLiteral("event")).toInt()) {
        case QSanProtocol::S_GAME_EVENT_PLAYER_QUITDYING: request.type = QStringLiteral("#QuitDying"); break;
        case QSanProtocol::S_GAME_EVENT_PLAYER_REFORM: request.type = QStringLiteral("#PlayerReform"); break;
        case QSanProtocol::S_GAME_EVENT_CHANGE_HERO:
            request.type = QStringLiteral("#ChangeHero");
            request.arg = event.value(QStringLiteral("general_name")).toString();
            break;
        case QSanProtocol::S_GAME_EVENT_HUASHEN:
            request.type = QStringLiteral("#HuaShen");
            request.arg = event.value(QStringLiteral("skill_name")).toString();
            request.arg2 = event.value(QStringLiteral("general_name")).toString();
            break;
        default: return QString();
        }
        return plainText(formatClientLog(request, logStyle(core)));
    }
    if (command == QSanProtocol::S_COMMAND_SPEAK) {
        const QVariantMap chat = payload.toMap();
        const QString text = chat.value(QStringLiteral("text")).toString().trimmed();
        return text.isEmpty() ? QString() : plainText(QStringLiteral("%1: %2").arg(chat.value(QStringLiteral("speaker")).toString(), text));
    }
    return plainText(fallback);
}

bool playPresentationAudio(int command, const QVariant &payload, const QString &assetRoot)
{
    if (command != QSanProtocol::S_COMMAND_PLAY_AUDIO) return false;
    const QString path = payload.toMap().value(QStringLiteral("path")).toString();
    if (path.isEmpty()) return false;
    const QFileInfo info(path);
    const QString root = QDir::fromNativeSeparators(QFileInfo(assetRoot).canonicalFilePath());
    const QString absolute = QDir::fromNativeSeparators(info.isAbsolute() ? info.canonicalFilePath() : QFileInfo(QDir(assetRoot).absoluteFilePath(path)).canonicalFilePath());
    if (root.isEmpty() || absolute.isEmpty() || !absolute.startsWith(root + QLatin1Char('/'), Qt::CaseInsensitive) || !QFileInfo(absolute).isFile()) return false;
    // Audio::play's second argument is superposition, not looping.
    if (payload.toMap().value(QStringLiteral("loop")).toBool()) Audio::playBGM(absolute);
    else Audio::play(absolute);
    return true;
}

} // namespace ExcelView
