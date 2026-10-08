#include "general-authoring.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDebug>
#include <cstdlib>
using namespace GeneralAuthoring;
#define CHECK(condition) do { if (!(condition)) { qCritical() << "Failed at" << __LINE__ << #condition; std::exit(1); } } while (false)

QJsonArray messages(const Document &doc, const QString &instruction = "change")
{
    QString error;
    const auto body = doc.preview("fixture-model", instruction, &error);
    CHECK(!body.isEmpty());
    const auto payload = QJsonDocument::fromJson(body).object();
    CHECK(payload.keys() == QStringList({"messages", "model", "stream"}));
    CHECK(payload.value("model") == "fixture-model");
    CHECK(payload.value("stream") == false);
    return payload.value("messages").toArray();
}
QJsonObject content(const QJsonArray &value, int i)
{
    return QJsonDocument::fromJson(value[i].toObject().value("content").toString().toUtf8()).object();
}
QByteArray response(const QJsonObject &usage = {}, bool extra = false)
{
    QJsonObject result{{"schema_version", 1}, {"skills_lua", "local diy_demo_hero_skill = sgs.CreateTriggerSkillV2 { name = \"diy_demo_hero_skill\" }"}, {"notes", "fixture"}};
    if (extra) result["extra"] = true;
    const QJsonObject message{{"content", QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact))}};
    const QJsonObject choice{{"finish_reason", "stop"}, {"message", message}};
    return QJsonDocument(QJsonObject{{"usage", usage}, {"choices", QJsonArray{choice}}}).toJson();
}
void prefixTests()
{
    Document a, b;
    CHECK(a.context.isValid());
    const auto first = messages(a);
    CHECK(first.size() == 3);
    CHECK(first[0].toObject().value("role") == "system");
    CHECK(first[1].toObject().value("role") == "user");
    CHECK(first[2].toObject().value("role") == "user");
    auto spec = b.spec; spec["display_name"] = "修訂名稱";
    b.setSpec(spec); b.setReviewed("-- reviewed manual edits");
    b.originalSpec = a.spec;
    const auto next = messages(b, "preserve my edits");
    CHECK(first[0] == next[0]); CHECK(first[1] == next[1]); CHECK(first[2] != next[2]);
    const auto dynamic = content(next, 2);
    CHECK(dynamic.value("current_spec") == b.spec);
    CHECK(dynamic.value("original_spec") == a.spec);
    CHECK(dynamic.value("reviewed_code") == b.reviewed);
    CHECK(dynamic.value("correction_request") == "preserve my edits");
    CHECK(dynamic.value("diagnostics").isArray());
    CHECK(!dynamic.contains("engine_context"));
    const auto stable = content(first, 1);
    CHECK(stable.value("schema_version") == 1);
    CHECK(stable.value("prompt_contract_version") == 1);
    CHECK(stable.value("prompt_contract_sha256").toString().size() == 64);
    CHECK(QJsonDocument::fromJson(stable.value("engine_context").toString().toUtf8())
        == QJsonDocument::fromJson(a.context.text.toUtf8()));
    // Whitespace/object order changes in the same JSON contract retain bytes.
    a.context.text = "{\"z\":1,\"a\":{\"y\":2,\"x\":3},\"list\":[2,1]}";
    b.context.text = "{ \"list\": [2,1], \"a\": {\"x\":3,\"y\":2}, \"z\": 1 }";
    CHECK(messages(a)[1] == messages(b)[1]);
    b.context.text = "{\"z\":1,\"a\":{\"y\":2,\"x\":4},\"list\":[2,1]}";
    CHECK(content(messages(a), 1).value("prompt_contract_sha256") != content(messages(b), 1).value("prompt_contract_sha256"));
    b.context.text = "{\"z\":1,\"a\":{\"y\":2,\"x\":3},\"list\":[1,2]}";
    CHECK(messages(a)[1] != messages(b)[1]);
    QString error;
    CHECK(a.preview("", "", &error).isEmpty());
    CHECK(a.preview(QString(201, 'm'), "", &error).isEmpty());
    CHECK(a.preview("m", QString(8001, 'i'), &error).isEmpty());
    a.rememberSecret("fixture-secret-12345678");
    CHECK(a.preview("m", "fixture-secret-12345678", &error).isEmpty());
}
void usageTests()
{
    CHECK(responseUsage({}).value("cache_read_tokens").isNull());
    QJsonObject raw{{"prompt_tokens", 100}, {"completion_tokens", 10}, {"total_tokens", 110},
        {"prompt_tokens_details", QJsonObject{{"cached_tokens", 0}, {"cache_write_tokens", 64}}}};
    auto u = responseUsage({{"usage", raw}});
    CHECK(u.value("status") == "reported"); CHECK(u.value("cache_read_tokens") == 0);
    CHECK(u.value("cache_write_tokens") == 64); CHECK(u.value("cache_miss_tokens").isNull());
    raw.remove("prompt_tokens_details"); raw["prompt_cache_hit_tokens"] = 64; raw["prompt_cache_miss_tokens"] = 36;
    u = responseUsage({{"usage", raw}});
    CHECK(u.value("cache_read_tokens") == 64); CHECK(u.value("cache_miss_tokens") == 36);
    CHECK(u.value("cache_write_tokens").isNull());
    for (const auto &bad : QJsonArray{true, -1, 1.5, "64", 101, QJsonArray{}, QJsonObject{}}) {
        auto invalid = raw; invalid["prompt_cache_hit_tokens"] = bad;
        u = responseUsage({{"usage", invalid}});
        CHECK(u.value("status") == "invalid"); CHECK(u.value("cache_read_tokens").isNull());
    }
    for (const auto &bad : QJsonArray{true, -1, 1.5, "64", 101, QJsonArray{}, QJsonObject{}}) {
        auto invalid = raw; invalid["prompt_tokens_details"] = QJsonObject{{"cache_write_tokens", bad}};
        CHECK(responseUsage({{"usage", invalid}}).value("status") == "invalid");
    }
    auto conflict = raw; conflict["prompt_tokens_details"] = QJsonObject{{"cached_tokens", 63}};
    CHECK(responseUsage({{"usage", conflict}}).value("status") == "invalid");
    conflict = raw; conflict["prompt_cache_miss_tokens"] = 35;
    CHECK(responseUsage({{"usage", conflict}}).value("status") == "invalid");
    conflict = raw; conflict["total_tokens"] = 111;
    CHECK(responseUsage({{"usage", conflict}}).value("status") == "invalid");
    CHECK(responseUsage({{"usage", true}}).value("status") == "invalid");
    CHECK(responseUsage({{"usage", QJsonObject{{"input_tokens", 100}, {"cache_read_input_tokens", 50}}}}).value("status") == "unknown");
}
void lifecycleTests()
{
    Document doc;
    QString error;
    const QJsonObject raw{{"prompt_tokens", 100}, {"completion_tokens", 10},
        {"prompt_tokens_details", QJsonObject{{"cached_tokens", 64}}}};
    auto id = doc.beginRequest();
    CHECK(doc.receive(id, response(raw), &error));
    CHECK(doc.lastUsage().value("cache_read_tokens") == 64);
    CHECK(!doc.candidate.isEmpty()); CHECK(doc.reviewed.isEmpty());
    id = doc.beginRequest();
    CHECK(doc.lastUsage().value("cache_read_tokens").isNull());
    CHECK(!doc.receive(id, response(raw, true), &error)); // Exact output schema retained.
    CHECK(doc.candidate.isEmpty());
    id = doc.beginRequest(); doc.cancel();
    CHECK(!doc.receive(id, response(raw), &error));
    CHECK(doc.lastUsage().value("cache_read_tokens").isNull());
    id = doc.beginRequest(); doc.setReviewed("-- new edit");
    CHECK(!doc.receive(id, response(raw), &error));
    CHECK(doc.reviewed == "-- new edit");
    CHECK(doc.lastUsage().value("cache_read_tokens").isNull());
    id = doc.beginRequest(); CHECK(doc.receive(id, response(), &error)); // Missing usage does not alter output contract.
    CHECK(doc.lastUsage().value("cache_read_tokens").isNull());
    const auto saved = QJsonDocument::fromJson(doc.project(&error)).object();
    CHECK(!saved.contains("usage")); // Receipts stay transient; never export provider data.
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    prefixTests(); usageTests(); lifecycleTests();
    qInfo() << "authoring prefix, usage and lifecycle fixtures passed";
}
