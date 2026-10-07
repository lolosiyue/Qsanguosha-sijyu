#include "general-authoring.h"
#include "general-authoring-dialog.h"
#include "general-authoring-provider.h"
#include "package-catalog.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QQueue>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>
#include <iostream>
#include <memory>

using namespace GeneralAuthoring;

namespace {
int failures = 0;

void check(bool value, const char *description)
{
    if (value) return;
    ++failures;
    std::cerr << "FAIL: " << description << '\n';
}

QString skillBody(const QJsonObject &spec, int drawCount = 1, const QString &extra = {})
{
    const QString id = spec.value("skills").toArray().first().toObject().value("id").toString();
    return QStringLiteral(
        "local %1 = sgs.CreateTriggerSkillV2 {\n"
        "  name = \"%1\",\n"
        "  events = { sgs.TurnStart },\n"
        "  can_trigger = function(skill, event, room, player, data)\n"
        "    return player and player:isAlive() and skill:objectName() or false\n"
        "  end,\n"
        "  on_cost = function(skill, event, room, player, ctx)\n"
        "    return room:askForSkillInvoke(player, skill:objectName(), ctx.original_data)\n"
        "  end,\n"
        "  on_effect = function(skill, event, room, player, ctx)\n"
        "    player:drawCards(%2, skill:objectName())\n"
        "  end,\n"
        "}\n%3")
        .arg(id).arg(drawCount).arg(extra);
}

QByteArray apiResponse(const QString &skills, const QString &notes = {})
{
    return QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{
        {"finish_reason", "stop"}, {"message", QJsonObject{{"content", QString::fromUtf8(
            QJsonDocument(QJsonObject{{"schema_version", 1}, {"skills_lua", skills}, {"notes", notes}})
                .toJson(QJsonDocument::Compact))}}}
    }}}}).toJson(QJsonDocument::Compact);
}

bool acceptSkills(Document &document, const QString &skills, QString *error)
{
    const quint64 id = document.beginRequest();
    return document.receive(id, apiResponse(skills), error);
}

QString validReviewed(const QJsonObject &spec, int draws = 1)
{
    return assemble(spec, skillBody(spec, draws));
}

QString codeAtSize(const QJsonObject &spec, const QString &skills, int targetBytes)
{
    QString code = assemble(spec, skills);
    const int remaining = targetBytes - code.toUtf8().size();
    if (remaining < 0) return {};
    return assemble(spec, skills + QString(remaining, QLatin1Char(' ')));
}

QString longDelimiterComment(int contentBytes)
{
    const QString opening = QStringLiteral("--[==[\n");
    const QString closing = QStringLiteral("]==]\n");
    const QString decoy = QStringLiteral("]=] ]===] ]====] [==[ sgs.FakeLongComment\n");
    QString body = opening;
    while (body.size() + decoy.size() + closing.size() <= contentBytes) body += decoy;
    body += QString(qMax(0, contentBytes - body.size() - closing.size()), QLatin1Char(' '));
    body += closing;
    return body;
}

class ScriptedReply final : public QNetworkReply
{
public:
    ScriptedReply(const QNetworkRequest &request, QByteArray body, int status, int delayMs,
                  QObject *parent)
        : QNetworkReply(parent), m_body(std::move(body))
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::PostOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (status >= 300 && status < 400)
            setAttribute(QNetworkRequest::RedirectionTargetAttribute, QUrl(QStringLiteral("https://elsewhere.invalid/")));
        open(QIODevice::ReadOnly);
        QTimer::singleShot(delayMs, this, [this] {
            if (m_aborted) return;
            setFinished(true);
            emit readyRead();
            emit finished();
        });
    }

    void abort() override
    {
        if (m_aborted) return;
        m_aborted = true;
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("mock cancelled"));
        QTimer::singleShot(0, this, [this] {
            setFinished(true);
            emit finished();
        });
    }

    qint64 bytesAvailable() const override
    {
        return m_body.size() - m_offset + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 available = m_body.size() - m_offset;
        if (available <= 0) return -1;
        const qint64 count = qMin(available, maxSize);
        memcpy(data, m_body.constData() + m_offset, size_t(count));
        m_offset += count;
        return count;
    }

private:
    QByteArray m_body;
    qint64 m_offset = 0;
    bool m_aborted = false;
};

class ScriptedManager final : public QNetworkAccessManager
{
public:
    struct Response { QByteArray body; int status = 200; int delayMs = 0; };
    struct Request { Operation operation; QNetworkRequest request; QByteArray body; };
    QQueue<Response> responses;
    QList<Request> requests;

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData = nullptr) override
    {
        requests.append({operation, request, outgoingData ? outgoingData->readAll() : QByteArray()});
        const Response response = responses.isEmpty() ? Response{{}, 500, 0} : responses.dequeue();
        return new ScriptedReply(request, response.body, response.status, response.delayMs, this);
    }
};

void runUntilDone(QEventLoop &loop, int timeoutMs = 1500)
{
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
}

bool waitForCondition(const std::function<bool()> &condition, int timeoutMs = 1500)
{
    if (condition()) return true;
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(5);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (condition()) loop.quit(); });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    poll.start();
    loop.exec();
    poll.stop();
    return condition();
}

void testSpecAndCodeContracts()
{
    const auto spec = defaultSpec();
    check(validateSpec(spec).isEmpty(), "default structured specification is valid");
    check(!validateSpec(spec, {"diy_demo_hero_skill"}).isEmpty(), "occupied identifier collision is rejected");

    auto invalid = spec;
    invalid["general_id"] = "../outside";
    check(!validateSpec(invalid).isEmpty(), "path traversal in general identifier is rejected");
    invalid = spec;
    invalid["general_id"] = QStringLiteral("diy_demo_hero\n");
    check(!validateSpec(invalid).isEmpty(), "identifier must not accept a final newline");
    invalid = spec;
    invalid["max_hp"] = 2.5;
    check(!validateSpec(invalid).isEmpty(), "fractional HP is rejected");
    invalid = spec;
    invalid["max_hp"] = 1.0e100;
    check(!validateSpec(invalid).isEmpty(), "huge out-of-range HP is rejected");
    invalid = spec;
    invalid["start_hp"] = -1.0e100;
    check(!validateSpec(invalid).isEmpty(), "huge negative starting HP is rejected");
    invalid = spec;
    auto skills = invalid.value("skills").toArray();
    auto skill = skills.first().toObject();
    skill["id"] = "other_general_skill";
    skills[0] = skill;
    invalid["skills"] = skills;
    check(!validateSpec(invalid).isEmpty(), "skill ID outside the general namespace is rejected");
    invalid = spec;
    invalid["unexpected"] = true;
    check(!validateSpec(invalid).isEmpty(), "unknown specification fields are rejected");

    const auto context = bundledContext();
    check(context.isValid(), "bundled API context is available");
    const QString good = validReviewed(spec);
    check(validateCode(spec, good, context).isEmpty(), "engine V2 skill example passes syntax and API lint");

    auto quotedMetadata = spec;
    quotedMetadata["display_name"] = QStringLiteral("Hero \"quoted\"\n新将");
    quotedMetadata["title"] = QStringLiteral("The \"Brave\"\n名号");
    auto quotedSkills = quotedMetadata.value("skills").toArray();
    auto quotedSkill = quotedSkills.first().toObject();
    quotedSkill["title"] = QStringLiteral("Skill \"one\"");
    quotedSkill["description"] = QStringLiteral("Line one\nLine two: \"draw\".");
    quotedSkills[0] = quotedSkill;
    quotedMetadata["skills"] = quotedSkills;
    const QString escaped = validReviewed(quotedMetadata);
    check(validateSpec(quotedMetadata).isEmpty() && validateCode(quotedMetadata, escaped, context).isEmpty(),
          "generated metadata safely escapes quotes, newlines and Unicode translation text");
    check(escaped.contains("\\034") || escaped.contains("\\010"),
          "translation control characters use inert Lua byte escapes");

    QString broken = good;
    broken.replace("  end,\n  on_cost", "  on_cost");
    const auto syntaxErrors = validateCode(spec, broken, context);
    check(!syntaxErrors.isEmpty() && syntaxErrors.join('\n').contains("Lua syntax"), "invalid Lua response is rejected by parser");

    QString unknownGlobal = skillBody(spec);
    unknownGlobal.replace("sgs.TurnStart", "sgs.NonexistentEvent");
    check(validateCode(spec, assemble(spec, unknownGlobal), context).join('\n').contains("Undeclared engine API"),
          "unknown engine enum/API is rejected");
    QString unknownMethod = skillBody(spec);
    unknownMethod.replace("player:isAlive()", "player:inventedMethod()");
    check(validateCode(spec, assemble(spec, unknownMethod), context).join('\n').contains("Undeclared engine method"),
          "unknown engine method is rejected");

    QString unknownBareFunction = skillBody(spec);
    unknownBareFunction.replace("player:drawCards(1, skill:objectName())", "nonexistentFunction()");
    check(validateCode(spec, assemble(spec, unknownBareFunction), context).join('\n').contains("Undeclared Lua function"),
          "unknown bare Lua function is rejected");

    QString unknownLibraryFunction = skillBody(spec);
    unknownLibraryFunction.replace("player:drawCards(1, skill:objectName())", "math.invented(1)");
    check(validateCode(spec, assemble(spec, unknownLibraryFunction), context).join('\n').contains("Undeclared library function"),
          "unknown dotted library function is rejected");

    QString localHelper = skillBody(spec);
    localHelper.prepend(QStringLiteral("local helper = function(target) return target : isAlive() and math.floor(1) end\n"));
    localHelper.replace("player:isAlive()", "helper(player)");
    check(validateCode(spec, assemble(spec, localHelper), context).isEmpty(),
          "declared local helper, allowlisted library call and spaced engine method pass");

    const QString sid = spec.value("skills").toArray().first().toObject().value("id").toString();
    const QString commentedFake = QStringLiteral(
        "-- local %1 = sgs.CreateTriggerSkillV2 { name = \"%1\", }\n").arg(sid);
    const auto fakeErrors = validateCode(spec, assemble(spec, commentedFake), context).join('\n');
    check(fakeErrors.contains("Missing literal V2 skill binding"),
          "commented-out fake skill binding does not satisfy declaration lint");

    const QString duplicate = skillBody(spec) + skillBody(spec);
    const auto duplicateErrors = validateCode(spec, assemble(spec, duplicate), context).join('\n');
    check(duplicateErrors.contains("Missing literal V2 skill binding"),
          "duplicate declarations of a declared skill name are rejected");

    const QString extraConstructor = skillBody(spec)
        + QStringLiteral("\nlocal diy_demo_extra = sgs.CreateTriggerSkillV2 { name = \"diy_demo_extra\", }\n");
    const auto extraErrors = validateCode(spec, assemble(spec, extraConstructor), context).join('\n');
    check(extraErrors.contains("Missing literal V2 skill binding"),
          "constructor beyond the declared skill set is rejected");

    QTemporaryDir temporary;
    const QString marker = temporary.filePath(QStringLiteral("executed"));
    QString shellPayload = skillBody(spec);
    shellPayload.replace("player:drawCards(1, skill:objectName())",
                         QStringLiteral("os.execute(\"touch %1\")").arg(marker));
    const auto shellErrors = validateCode(spec, assemble(spec, shellPayload), context);
    check(!shellErrors.isEmpty() && shellErrors.join('\n').contains("External loading"),
          "host execution APIs are rejected by lint");
    check(!QFileInfo::exists(marker), "syntax/lint validation does not execute os.execute payload");

    // This is deliberately executable if a consumer were to run the source.
    // The validation path must only compile it; CTest's timeout bounds any accidental execution.
    const QString infiniteLoop = assemble(spec, QStringLiteral("while true do end\n") + skillBody(spec));
    check(validateCode(spec, infiniteLoop, context).isEmpty(),
          "top-level infinite loop is parsed without being evaluated");
}

void testLuaLexicalMasking()
{
    const QJsonObject spec = defaultSpec();
    const Context context = bundledContext();
    const QString id = spec.value("skills").toArray().first().toObject().value("id").toString();

    const QString shortQuotes = QString::fromUtf8(R"lua(
local double_quoted = "sgs.FakeStringApi\" quote and \\ slash \
continued sgs.FakeStringApi"
local single_quoted = 'sgs.FakeSingleApi\' quote'
)lua");
    check(validateCode(spec, assemble(spec, skillBody(spec) + shortQuotes), context).isEmpty(),
          "escaped short quotes, backslashes and escaped newlines are masked without hiding syntax");

    const QString longLiterals = QString::fromUtf8(R"lua(
local long_string = [===[
sgs.FakeLongStringApi ]=] ]====] [=[ nested-looking opener
]===]
--[==[ sgs.FakeLongCommentApi ]=] ]===] still inside the comment ]==]
-- sgs.FakeCrLfCommentApi
local crlf_line = 1
)lua");
    const auto literalErrors = validateCode(spec, assemble(spec, skillBody(spec) + longLiterals), context).join('\n');
    check(!literalErrors.contains("FakeLongStringApi") && !literalErrors.contains("FakeLongCommentApi")
              && !literalErrors.contains("FakeCrLfCommentApi") && !literalErrors.contains("Lua syntax"),
          "long string/comment delimiter levels and CRLF line comments mask only their contents");

    const QString closedLiteralsThenApi = QString::fromUtf8(R"lua(
local text = [=[sgs.FakeStringApi ]==] ]====] still a long string]=]
--[==[ sgs.FakeCommentApi ]=] ]===] still a long comment ]==]
local actual = sgs.NotARealEvent
)lua");
    const auto afterCloseErrors = validateCode(spec, assemble(spec, skillBody(spec) + closedLiteralsThenApi), context).join('\n');
    check(afterCloseErrors.contains("Undeclared engine API: NotARealEvent")
              && !afterCloseErrors.contains("FakeStringApi") && !afterCloseErrors.contains("FakeCommentApi"),
          "lint masks fake API names but diagnoses an actual API reference after each closing delimiter");

    const QString overlappingLevelZeroCloser = QString::fromUtf8(
        "local overlap = [[sgs.FakeOverlapApi]=]]\nlocal actual = sgs.NotARealEvent\n");
    const auto overlapErrors = validateCode(
        spec, assemble(spec, skillBody(spec) + overlappingLevelZeroCloser), context).join('\n');
    check(overlapErrors.contains("Undeclared engine API: NotARealEvent")
              && !overlapErrors.contains("FakeOverlapApi") && !overlapErrors.contains("Lua syntax"),
          "level-zero long-string closer survives an overlapping failed ]=] candidate");

    const QString crOnlyLineComment = QString::fromUtf8(
        "-- sgs.FakeCrOnlyApi\rlocal actual = sgs.NotARealEvent\r");
    const auto crOnlyErrors = validateCode(
        spec, assemble(spec, skillBody(spec) + crOnlyLineComment), context).join('\n');
    check(crOnlyErrors.contains("Undeclared engine API: NotARealEvent")
              && !crOnlyErrors.contains("FakeCrOnlyApi"),
          "CR-only line endings terminate a line comment where the Lua lexer does");

    const QString fakeCommentBinding = QStringLiteral(
        "--[=[ local %1 = sgs.CreateTriggerSkillV2 { name = \"%1\", } ]=]\r\n").arg(id);
    check(validateCode(spec, assemble(spec, fakeCommentBinding), context).join('\n')
              .contains("Missing literal V2 skill binding"),
          "skill binding inside a long comment never satisfies declaration lint");
    const QString fakeStringBinding = QStringLiteral(
        "local decoy = \"local %1 = sgs.CreateTriggerSkillV2 { name = \\\"%1\\\", }\"\n").arg(id);
    check(validateCode(spec, assemble(spec, fakeStringBinding), context).join('\n')
              .contains("Missing literal V2 skill binding"),
          "skill binding inside a short string never satisfies declaration lint");

    const QStringList malformed = {
        skillBody(spec) + QStringLiteral("\nlocal bad = [==[ mismatched closer ]=]\n"),
        skillBody(spec) + QStringLiteral("\n--[==[ mismatched long comment closer ]=]\n"),
        skillBody(spec) + QStringLiteral("\nlocal bad = \"unterminated short string\n")
    };
    for (const QString &source : malformed)
        check(validateCode(spec, assemble(spec, source), context).join('\n').contains("Lua syntax"),
              "mismatched or unterminated Lua lexical forms are rejected by the parser");

    const QString highLevelEquals(4096, QLatin1Char('='));
    const QString highLevelCommentOpen = QStringLiteral("--[") + highLevelEquals + QStringLiteral("[\n");
    const QString shortCloser = QStringLiteral("]") + highLevelEquals.left(4095) + QStringLiteral("]");
    const QString longCloser = QStringLiteral("]") + highLevelEquals + QStringLiteral("==]");
    const QString highLevelDecoys = QStringLiteral("sgs.FakeHighLevelComment ")
        + (shortCloser + QLatin1Char(' ') + longCloser + QLatin1Char('\n')).repeated(4);
    const QString highLevelComment = highLevelCommentOpen + highLevelDecoys
        + QStringLiteral("]") + highLevelEquals + QStringLiteral("]\n");
    check(validateCode(spec, assemble(spec, skillBody(spec) + highLevelComment), context).isEmpty(),
          "4096-equals long-comment opener ignores repeated shorter and longer closing decoys");

    const QString nearLimitSkills = skillBody(spec) + QStringLiteral("\nlocal short_token = 1\n");
    const QString nearLimit = codeAtSize(spec, nearLimitSkills, MaxCodeBytes);
    check(!nearLimit.isEmpty() && nearLimit.toUtf8().size() == MaxCodeBytes
              && validateCode(spec, nearLimit, context).isEmpty(),
          "valid source padded with ordinary spaces and short tokens is accepted at the byte limit");

    const QString actualAfterLongComment = longDelimiterComment(56000)
        + QStringLiteral("local actual = sgs.NotARealEvent\n");
    const QString adversarialAtLimit = codeAtSize(
        spec, skillBody(spec) + actualAfterLongComment, MaxCodeBytes);
    const auto adversarialErrors = validateCode(spec, adversarialAtLimit, context).join('\n');
    check(adversarialAtLimit.toUtf8().size() == MaxCodeBytes
              && adversarialErrors.contains("Undeclared engine API: NotARealEvent")
              && !adversarialErrors.contains("FakeLongComment"),
          "near-limit long-bracket delimiter decoys remain masked while following API references are linted");

    QString oversized = validReviewed(spec);
    oversized += QString(MaxCodeBytes + 1 - oversized.toUtf8().size(), QLatin1Char(' '));
    check(oversized.toUtf8().size() == MaxCodeBytes + 1
              && validateCode(spec, oversized, context).join('\n').contains("Code exceeds the size limit"),
          "source exceeding the byte limit is refused before lexical linting");
}

void testLuaKeywordCallRecognition()
{
    const Context context = bundledContext();
    const QString keywordForms = QString::fromUtf8(R"lua(
local direct = not(true)
local spaced = not (true)
local nested = not (not(not (false)))
local line_break = not
    (true)
local comment_gap = not -- a line comment between unary operator and operand
    (true)
local long_comment_gap = not --[=[ a long comment between unary operator and operand ]=] (true)
local conjunction = true and (not(false))
local disjunction = false or (true)
if (true) then
    local branch = true
elseif (false) then
    local other_branch = false
end
while (false) do
    break
end
repeat
    local body = true
until (true)
local function return_parenthesized()
    return (true)
end
local returned = return_parenthesized()
local string_decoy = "unknownFunction() not (unknownFunction()) if (unknownFunction())"
-- unknownFunction() and not (unknownFunction()) if (unknownFunction())
--[=[ unknownFunction() and not (unknownFunction()) until (unknownFunction()) ]=]
)lua");
    const auto keywordErrors = validateCode(defaultSpec(), assemble(defaultSpec(), skillBody(defaultSpec()) + keywordForms), context);
    check(keywordErrors.isEmpty(),
          "Lua reserved keywords/operators before parentheses are not linted as function calls");

    const QString prefixSimilarCall = skillBody(defaultSpec())
        + QStringLiteral("\nnotAFunction(true)\n");
    const auto prefixSimilarErrors = validateCode(
        defaultSpec(), assemble(defaultSpec(), prefixSimilarCall), context);
    check(prefixSimilarErrors.contains("Undeclared Lua function: notAFunction"),
          "keyword matching does not exempt prefix-similar ordinary function names");

    const QJsonObject sampleSpec{
        {"package_id", "diy_deepseek_qa"}, {"general_id", "diy_deepseek_qa_hero"},
        {"display_name", "Fictional QA Hero"}, {"kingdom", "wei"}, {"max_hp", 4},
        {"start_hp", 4}, {"armor", 0}, {"male", true}, {"lord", false},
        {"title", "Test fixture"}, {"designer", "Automated fictional QA"},
        {"skills", QJsonArray{QJsonObject{
            {"id", "diy_deepseek_qa_hero_finish"}, {"title", "Finish Draw"},
            {"description", "At the start of your Finish phase, you may draw one card."}
        }}}
    };
    // Deterministic copy of the fictional saved stage-one response's skills_lua.
    const QString rawDeepSeekSkills = QString::fromUtf8(R"lua(
local diy_deepseek_qa_hero_finish = sgs.CreateTriggerSkillV2 {
    name = "diy_deepseek_qa_hero_finish",
    events = EventPhaseStart,
    frequency = Skill_NotFrequent,
    can_trigger = function(skill, event, room, player, data)
        if not (player and player:isAlive() and player:hasSkill(skill:objectName())) then
            return false
        end
        if player:getPhase() ~= Player_Finish then
            return false
        end
        return skill:objectName()
    end,
    on_cost = function(skill, event, room, player, ctx)
        return room:askForSkillInvoke(player, skill:objectName(), ctx.original_data)
    end,
    on_effect = function(skill, event, room, player, ctx)
        player:drawCards(1, skill:objectName())
        return false
    end,
}
)lua");
    check(rawDeepSeekSkills.contains("if not (")
              && rawDeepSeekSkills.contains("events = EventPhaseStart")
              && rawDeepSeekSkills.contains("frequency = Skill_NotFrequent")
              && rawDeepSeekSkills.contains("~= Player_Finish"),
          "saved fictional sample retains its original condition and bare enum spellings");
    const QString rawSampleErrors = validateCode(sampleSpec, assemble(sampleSpec, rawDeepSeekSkills), context).join('\n');
    check(!rawSampleErrors.contains("Undeclared Lua function: not"),
          "saved DeepSeek `not (` guard is accepted by function-call lint");

    QString qualifiedSample = rawDeepSeekSkills;
    qualifiedSample.replace("events = EventPhaseStart", "events = { sgs.EventPhaseStart }");
    qualifiedSample.replace("frequency = Skill_NotFrequent", "frequency = sgs.Skill_NotFrequent");
    qualifiedSample.replace("~= Player_Finish", "~= sgs.Player_Finish");
    check(qualifiedSample.contains("if not (")
              && validateCode(sampleSpec, assemble(sampleSpec, qualifiedSample), context).isEmpty(),
          "enum-qualified saved sample validates without rewriting its legal `not (` expression");

    const QString invalidReservedForm = skillBody(defaultSpec())
        + QStringLiteral("\nlocal invalid = if (true) then true end\n");
    const auto invalidErrors = validateCode(defaultSpec(), assemble(defaultSpec(), invalidReservedForm), context);
    check(invalidErrors.join('\n').contains("Lua syntax"),
          "reserved-keyword call-like text that is invalid Lua still receives a syntax diagnostic");

    const QJsonObject spec = defaultSpec();
    const QString keywordStatement = QStringLiteral("do local condition = not (true) end\n");
    const QString unknownAndForbidden = QStringLiteral(
        "nonexistentFunction()\nos.execute(\"not executed\")\n");
    const int fixedBytes = assemble(spec, skillBody(spec) + unknownAndForbidden).toUtf8().size();
    const int keywordCount = qMax(0, (MaxCodeBytes - fixedBytes) / keywordStatement.toUtf8().size());
    const QString manyKeywords = keywordStatement.repeated(keywordCount);
    const QString nearLimitValid = codeAtSize(spec, skillBody(spec) + manyKeywords, MaxCodeBytes);
    const auto manyKeywordErrors = validateCode(spec, nearLimitValid, context);
    check(nearLimitValid.toUtf8().size() == MaxCodeBytes && manyKeywordErrors.isEmpty(),
          "near-limit source with many valid `not (` expressions has no false function diagnostics");

    const QString nearLimitMixed = codeAtSize(
        spec, skillBody(spec) + manyKeywords + unknownAndForbidden, MaxCodeBytes);
    const auto mixedErrors = validateCode(spec, nearLimitMixed, context);
    check(nearLimitMixed.toUtf8().size() == MaxCodeBytes
              && mixedErrors.contains("Undeclared Lua function: nonexistentFunction")
              && mixedErrors.contains("External loading, host access and dynamic engine lookup are outside this authoring contract.")
              && !mixedErrors.contains("Undeclared Lua function: not"),
          "near-limit mixed source still rejects unknown and unsafe calls without keyword false positives");
}

void testDocumentRequestsAndHistory()
{
    Document document;
    QString error;
    const auto spec = document.spec;
    document.setReviewed(validReviewed(spec, 1));
    document.checkpoint();

    const quint64 first = document.beginRequest();
    const quint64 second = document.beginRequest();
    check(document.busy(), "request is marked busy");
    check(!document.receive(first, apiResponse(skillBody(spec, 3)), &error), "superseded response is discarded");
    check(document.receive(second, apiResponse(skillBody(spec, 2), QStringLiteral("Use a simple draw skill.")), &error),
          "latest response can be accepted as an inert candidate");
    check(document.reviewed.contains("drawCards(1"), "generation leaves reviewed code unchanged before apply");
    check(document.candidate.contains("drawCards(2"), "candidate contains generated correction");
    check(document.apply(&error), "validated candidate applies explicitly");
    check(document.reviewed.contains("drawCards(2"), "apply changes reviewed code");
    check(error.contains("Use a simple draw skill."), "response notes remain available for review");
    check(document.spec == spec, "applying returned skill code preserves structured metadata");
    check(!document.history.isEmpty() && document.history.size() <= MaxVersions, "apply records bounded versions");
    check(document.undo(), "undo restores the previous reviewed version");
    check(document.reviewed.contains("drawCards(1"), "undo restores original reviewed code");

    Document badEnvelope;
    const quint64 badEnvelopeId = badEnvelope.beginRequest();
    const QByteArray wrongSchema = QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{
        {"finish_reason", "stop"}, {"message", QJsonObject{{"content", QString::fromUtf8(
            QJsonDocument(QJsonObject{{"schema_version", 7}}).toJson(QJsonDocument::Compact))}}}
    }}}}).toJson(QJsonDocument::Compact);
    check(!badEnvelope.receive(badEnvelopeId, wrongSchema, &error) && badEnvelope.candidate.isEmpty(),
          "provider response with an unsupported JSON schema is rejected");

    Document invalidCandidate;
    const QString initial = validReviewed(invalidCandidate.spec, 1);
    invalidCandidate.setReviewed(initial);
    QString malformedSkills = skillBody(invalidCandidate.spec, 2);
    malformedSkills.replace("  end,\n  on_cost", "  on_cost");
    check(acceptSkills(invalidCandidate, malformedSkills, &error), "well-shaped provider envelope can carry untrusted Lua for review");
    check(!invalidCandidate.apply(&error) && invalidCandidate.reviewed == initial,
          "syntax-invalid candidate cannot be applied or replace reviewed code");

    Document unknownApiCandidate;
    const QString unknownInitial = validReviewed(unknownApiCandidate.spec, 1);
    unknownApiCandidate.setReviewed(unknownInitial);
    QString unsupportedSkills = skillBody(unknownApiCandidate.spec, 2);
    unsupportedSkills.replace("sgs.TurnStart", "sgs.NonexistentEvent");
    check(acceptSkills(unknownApiCandidate, unsupportedSkills, &error), "provider envelope accepts an untrusted API candidate for validation");
    check(!unknownApiCandidate.apply(&error) && unknownApiCandidate.reviewed == unknownInitial,
          "undeclared API candidate cannot be applied");

    Document editedAfterCandidate;
    const QString beforeCandidate = validReviewed(editedAfterCandidate.spec, 1);
    editedAfterCandidate.setReviewed(beforeCandidate);
    check(acceptSkills(editedAfterCandidate, skillBody(editedAfterCandidate.spec, 2), &error),
          "valid response becomes a candidate before a later manual edit");
    const QString manualAfterCandidate = beforeCandidate + QStringLiteral("-- edited after candidate\n");
    editedAfterCandidate.setReviewed(manualAfterCandidate);
    check(!editedAfterCandidate.apply(&error) && editedAfterCandidate.reviewed == manualAfterCandidate,
          "manual edit after candidate acceptance makes candidate stale and preserves reviewed text");

    const quint64 cancelled = document.beginRequest();
    document.cancel();
    check(!document.busy() && !document.receive(cancelled, apiResponse(skillBody(spec)), &error),
          "cancelled response is ignored and request state clears");

    const QString manual = document.reviewed + QStringLiteral("-- manual edit\n");
    document.setReviewed(manual);
    const quint64 stale = document.beginRequest();
    auto changedSpec = document.spec;
    changedSpec["display_name"] = QStringLiteral("Revised name");
    document.setSpec(changedSpec);
    check(!document.receive(stale, apiResponse(skillBody(changedSpec, 4)), &error), "document edits make an in-flight response stale");
    check(document.reviewed == manual, "stale response preserves manual code edits");
    check(!document.apply(&error), "stale candidate cannot be applied");

    // Exercise the history cap with distinct inert text snapshots.
    Document bounded;
    for (int i = 0; i < MaxVersions + 4; ++i) {
        bounded.setReviewed(QString::number(i));
        bounded.checkpoint();
    }
    check(bounded.history.size() == MaxVersions, "history evicts its oldest versions at the configured limit");
}

void testCorrectionPayloadAndProjects()
{
    Document document;
    const QJsonObject original = document.spec;
    document.setReviewed(validReviewed(original, 1));
    QString error;
    check(acceptSkills(document, skillBody(original, 2), &error), "initial response becomes a candidate for correction-flow setup");
    check(document.apply(&error), "initial generated version applies");

    QString manual = document.reviewed;
    manual.replace("drawCards(2", "drawCards(3");
    document.setReviewed(manual);
    auto current = document.spec;
    current["display_name"] = QStringLiteral("Edited display metadata");
    document.setSpec(current);
    const QByteArray request = document.preview(QStringLiteral("local-model"),
                                               QStringLiteral("Keep my edit and update only the trigger."), &error);
    check(!request.isEmpty(), "correction request payload builds from the current document");
    const auto envelope = QJsonDocument::fromJson(request).object();
    const auto messages = envelope.value("messages").toArray();
    const auto userContent = QJsonDocument::fromJson(messages.at(1).toObject().value("content").toString().toUtf8()).object();
    check(userContent.value("original_spec").toObject().value("display_name") == original.value("display_name"),
          "correction request preserves original structured spec");
    check(userContent.value("current_spec").toObject().value("display_name") == current.value("display_name"),
          "correction request includes current metadata");
    check(userContent.value("reviewed_code").toString() == manual,
          "correction request includes the latest manual code edit");
    check(userContent.value("correction_request").toString().contains("Keep my edit"),
          "correction request includes the user's instruction");
    check(userContent.value("diagnostics").isArray() && userContent.value("engine_context").isString(),
          "correction request includes diagnostics and the bounded API contract");

    const QByteArray project = document.project(&error);
    check(!project.isEmpty(), "project serialization succeeds");
    Document imported;
    check(imported.importProject(project, &error), "project import round-trips the saved project");
    check(imported.spec == document.spec && imported.originalSpec == document.originalSpec
              && imported.reviewed == document.reviewed && imported.history.size() == document.history.size(),
          "project round-trip restores metadata, manual code and history");

    Document incomplete;
    auto incompleteSpec = incomplete.spec;
    incompleteSpec["package_id"] = QString();
    incompleteSpec["general_id"] = QString();
    incompleteSpec["display_name"] = QString();
    incompleteSpec["kingdom"] = QString();
    incompleteSpec["max_hp"] = 0;
    incompleteSpec["start_hp"] = 0;
    incompleteSpec["skills"] = QJsonArray{};
    incomplete.setSpec(incompleteSpec);
    incomplete.originalSpec = incompleteSpec;
    incomplete.setReviewed(QStringLiteral("unfinished Lua notes"));
    incomplete.history.append({incompleteSpec, QStringLiteral("earlier unfinished notes")});
    const QByteArray incompleteProject = incomplete.project(&error);
    check(!incompleteProject.isEmpty(), "structurally valid incomplete document can be saved as inert project data");
    Document incompleteRoundTrip;
    check(incompleteRoundTrip.importProject(incompleteProject, &error), "incomplete metadata and history round-trip as inert data");
    check(incompleteRoundTrip.spec == incompleteSpec && incompleteRoundTrip.history.size() == 1,
          "project import preserves unfinished structured fields and version snapshots");
    check(!incompleteRoundTrip.diagnostics().isEmpty(), "incomplete project remains diagnostically invalid for generation");
    QTemporaryDir incompleteRoot;
    const QString incompleteParent = incompleteRoot.filePath(QStringLiteral("staging"));
    QDir().mkpath(incompleteParent);
    QString incompleteExport;
    check(!incompleteRoundTrip.exportDisabled(incompleteParent, {}, &incompleteExport, &error),
          "incomplete project cannot be exported as a package");

    Document oversizedDraft;
    oversizedDraft.setReviewed(QString(MaxCodeBytes + 1, QLatin1Char('x')));
    oversizedDraft.checkpoint();
    check(oversizedDraft.history.isEmpty() && oversizedDraft.project(&error).isEmpty(),
          "oversized manual code cannot poison history or save an unimportable project");
    oversizedDraft.setReviewed(validReviewed(oversizedDraft.spec));
    check(!oversizedDraft.project(&error).isEmpty(), "shortening manual code restores project saving");

    QJsonObject malformed = QJsonDocument::fromJson(project).object();
    malformed["schema_version"] = 2;
    check(!imported.importProject(QJsonDocument(malformed).toJson(), &error), "unknown project schema version is rejected");

    Document secretDoc;
    const QString escapedSecret = QStringLiteral("mock-key-only");
    secretDoc.setReviewed(QStringLiteral("-- %1\n").arg(escapedSecret));
    secretDoc.checkpoint();
    check(secretDoc.history.size() == 1, "test fixture checkpoints text before credential recognition");
    secretDoc.rememberSecret(escapedSecret);
    check(secretDoc.history.isEmpty(), "remembering a credential purges credential-bearing history entries");
    secretDoc.setReviewed(validReviewed(secretDoc.spec) + QStringLiteral("-- %1\n").arg(escapedSecret));
    secretDoc.checkpoint();
    check(secretDoc.history.isEmpty(), "credential-bearing checkpoint is never added to history");
    const QByteArray secretPreview = secretDoc.preview(QStringLiteral("model"), QStringLiteral("draft"), &error);
    check(secretPreview.isEmpty() && !error.contains(escapedSecret), "secret is rejected and not exposed in preview error");
    check(secretDoc.project(&error).isEmpty(), "secret-bearing project cannot be serialized");
    QByteArray encodedResponse = apiResponse(skillBody(secretDoc.spec), QStringLiteral("credential: %1").arg(escapedSecret));
    encodedResponse.replace(escapedSecret.toUtf8(), QByteArrayLiteral("\\u006dock-key-only"));
    check(!encodedResponse.contains(escapedSecret.toUtf8()), "response fixture hides the credential behind nested JSON escaping");
    check(!secretDoc.receive(secretDoc.beginRequest(), encodedResponse, &error),
          "nested escaped credential in provider response is rejected");

    Document escapedProjectDoc;
    escapedProjectDoc.rememberSecret(escapedSecret);
    escapedProjectDoc.setReviewed(QStringLiteral("{\"token\":\"\\u006dock-key-only\"}"));
    const QByteArray encodedProject = escapedProjectDoc.project(&error);
    check(encodedProject.isEmpty(), "nested JSON credential escaped by a project is rejected");
    QJsonObject encodedProjectObject{{"schema_version", 1}, {"original_spec", escapedProjectDoc.spec},
        {"spec", escapedProjectDoc.spec}, {"reviewed_code", QStringLiteral("{\"token\":\"\\u006dock-key-only\"}")},
        {"history", QJsonArray{}}};
    const QByteArray encodedProjectBytes = QJsonDocument(encodedProjectObject).toJson(QJsonDocument::Compact);
    check(!encodedProjectBytes.contains(escapedSecret.toUtf8()), "project fixture hides credential behind nested JSON escaping");
    check(!escapedProjectDoc.importProject(encodedProjectBytes, &error), "nested escaped credential in imported project is rejected");
}

void testDisabledExportSafety()
{
    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary export root is available");
    Document document;
    document.setReviewed(validReviewed(document.spec));
    const QString parent = temporary.filePath(QStringLiteral("staging"));
    QDir().mkpath(parent);
    const QString sentinel = QDir(parent).filePath(QStringLiteral("existing.txt"));
    QFile existing(sentinel);
    existing.open(QIODevice::WriteOnly);
    existing.write("keep");
    existing.close();

    QString exported, error;
    const QByteArray png("test-png-art");
    check(document.exportDisabled(parent, png, &exported, &error), "export writes a separate disabled package");
    check(QFileInfo(exported).isDir() && QFileInfo::exists(exported + QStringLiteral("/DISABLED.txt")),
          "export includes a disabled marker outside an engine-scanned root");
    check(QFileInfo::exists(exported + QStringLiteral("/authoring.json")), "export includes the editable authoring project");
    QFile sentinelAfter(sentinel);
    check(sentinelAfter.open(QIODevice::ReadOnly) && sentinelAfter.readAll() == QByteArray("keep"),
          "export leaves pre-existing sibling files unchanged");

    QFile manifestFile(exported + QStringLiteral("/diy_demo/manifest.json"));
    check(manifestFile.open(QIODevice::ReadOnly), "export includes a child package manifest");
    const auto manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    check(manifest.value("schema_version").toInt() == 1 && manifest.value("engine_api").toInt() == 1,
          "export manifest uses the current package schema");
    const auto extension = manifest.value("extensions").toArray().first().toObject();
    check(extension.value("script").toString() == QStringLiteral("lua/diy_demo.lua"),
          "export manifest points at the generated Lua extension");
    const auto assets = manifest.value("assets").toObject();
    const QString artPath = assets.value(QStringLiteral("image/diy/diy_demo_hero.png")).toString();
    check(artPath.startsWith(QStringLiteral("image/")) && QFileInfo::exists(exported + QStringLiteral("/diy_demo/") + artPath),
          "export artwork mapping and file satisfy the engine package media-path contract");
    const auto files = manifest.value("files").toArray();
    bool artSealed = false;
    for (const auto &recordValue : files) {
        const auto record = recordValue.toObject();
        if (record.value("path").toString() == artPath && record.value("role").toString() == "data") artSealed = true;
    }
    check(artSealed, "export inventory seals card art as package media data");
    const QSanPackages::Package parsed = QSanPackages::parsePackage(
        exported + QStringLiteral("/diy_demo"), &error, true, true);
    check(error.isEmpty() && parsed.id == QStringLiteral("diy_demo"),
          "native package catalog accepts the exported package with full file and data inspection");

    Document collision;
    collision.occupied.insert(collision.spec.value("package_id").toString());
    check(!collision.exportDisabled(parent, {}, &exported, &error), "occupied package identifier blocks export");

    Document traversal;
    auto traversalSpec = traversal.spec;
    traversalSpec["package_id"] = QStringLiteral("../outside");
    traversal.setSpec(traversalSpec);
    check(!traversal.exportDisabled(parent, {}, &exported, &error), "traversal identifiers cannot select an export path");
    check(!QFileInfo::exists(temporary.filePath(QStringLiteral("outside"))), "traversal attempt does not create a sibling package");

    const QString packageRoot = QDir(parent).filePath(QStringLiteral("packages"));
    QDir().mkpath(packageRoot);
    const QString protectedFile = QDir(packageRoot).filePath(QStringLiteral("do-not-overwrite"));
    QFile protectedSentinel(protectedFile);
    protectedSentinel.open(QIODevice::WriteOnly);
    protectedSentinel.write("protected");
    protectedSentinel.close();
    check(!document.exportDisabled(packageRoot, {}, &exported, &error), "engine packages directory is refused as an export parent");
    QFile protectedAfter(protectedFile);
    check(protectedAfter.open(QIODevice::ReadOnly) && protectedAfter.readAll() == QByteArray("protected"),
          "refused engine path leaves existing package files intact");

    const QString real = QDir(temporary.path()).filePath(QStringLiteral("real"));
    const QString link = QDir(temporary.path()).filePath(QStringLiteral("linked"));
    QDir().mkpath(real);
    const bool linked = QFile::link(real, link);
    if (linked)
        check(!document.exportDisabled(link, {}, &exported, &error), "symlink export ancestor is refused");
    else
        check(false, "test environment supports symlink creation");
}

void testEndpointAndProviderTransport()
{
    const QUrl endpoint(QStringLiteral("https://api.example.invalid/v1/chat/completions"));
    check(validEndpoint(endpoint), "HTTPS endpoint is accepted");
    check(!validEndpoint(QUrl(QStringLiteral("https://api.example.invalid/v1/%0A"))),
          "encoded uppercase endpoint control characters are rejected");
    check(!validEndpoint(QUrl(QStringLiteral("http://api.example.invalid/v1/chat/completions"))), "plain HTTP endpoint is rejected");
    check(!validEndpoint(QUrl(QStringLiteral("https://user:pass@api.example.invalid/v1"))), "credential-bearing endpoint URL is rejected");
    check(!validEndpoint(QUrl(QStringLiteral("https://api.example.invalid/v1?redirect=elsewhere"))), "endpoint query parameters are rejected");

    const QByteArray payload("{\"model\":\"test\",\"messages\":[]}");
    const QString key = QStringLiteral("mock-key-only");
    ScriptedManager manager;
    manager.responses.enqueue({QByteArrayLiteral("{\"ok\":true}"), 200, 0});
    Provider provider(nullptr, &manager);
    bool done = false;
    QByteArray body;
    QString error;
    QEventLoop loop;
    provider.send(endpoint, key, payload, [&](QByteArray value, QString failure) {
        body = std::move(value); error = std::move(failure); done = true; loop.quit();
    });
    runUntilDone(loop);
    check(done && error.isEmpty() && body == QByteArrayLiteral("{\"ok\":true}"), "mock HTTPS request returns the bounded response body");
    check(manager.requests.size() == 1 && manager.requests.first().operation == QNetworkAccessManager::PostOperation,
          "provider makes one POST through the injected mock transport");
    if (!manager.requests.isEmpty()) {
        const auto request = manager.requests.first();
        check(request.request.url() == endpoint, "provider posts to the selected endpoint");
        check(request.request.rawHeader("Authorization") == QByteArray("Bearer ") + key.toUtf8(),
              "API credential is sent only as the authorization header");
        check(!request.body.contains(key.toUtf8()), "API credential is absent from the model request body");
        check(request.request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt()
                  == int(QNetworkRequest::ManualRedirectPolicy), "redirects are disabled for credential-bearing calls");
        check(request.request.attribute(QNetworkRequest::CookieLoadControlAttribute).toInt()
                  == int(QNetworkRequest::Manual), "ambient cookies are disabled");
    }

    ScriptedManager badManager;
    Provider badProvider(nullptr, &badManager);
    bool badDone = false;
    QString badError;
    badProvider.send(QUrl(QStringLiteral("http://api.example.invalid/")), key, payload,
                     [&](QByteArray, QString failure) { badDone = true; badError = failure; });
    check(badDone && !badError.isEmpty() && badManager.requests.isEmpty(), "invalid endpoint is rejected before transport");

    ScriptedManager redirectManager;
    redirectManager.responses.enqueue({QByteArrayLiteral("redirect body"), 302, 0});
    Provider redirectProvider(nullptr, &redirectManager);
    bool redirectDone = false;
    QString redirectError;
    QEventLoop redirectLoop;
    redirectProvider.send(endpoint, key, payload, [&](QByteArray response, QString failure) {
        redirectDone = true; check(response.isEmpty(), "redirect response body is discarded");
        redirectError = failure; redirectLoop.quit();
    });
    runUntilDone(redirectLoop);
    check(redirectDone && redirectError.contains("redirect") && !redirectError.contains(key),
          "redirect response is refused without exposing credentials");
    check(redirectManager.requests.size() == 1, "redirect is never followed to a second host");

    ScriptedManager leakManager;
    leakManager.responses.enqueue({QByteArray("provider echoed mock-key-only"), 200, 0});
    Provider leakProvider(nullptr, &leakManager);
    bool leakDone = false;
    QByteArray leakedBody;
    QString leakError;
    QEventLoop leakLoop;
    leakProvider.send(endpoint, key, payload, [&](QByteArray response, QString failure) {
        leakDone = true; leakedBody = std::move(response); leakError = std::move(failure); leakLoop.quit();
    });
    runUntilDone(leakLoop);
    check(leakDone && leakedBody.isEmpty() && !leakError.isEmpty() && !leakError.contains(key),
          "credential echoed by a provider is discarded and redacted");

    ScriptedManager errorManager;
    errorManager.responses.enqueue({QByteArray("server error mock-key-only"), 500, 0});
    Provider errorProvider(nullptr, &errorManager);
    bool errorDone = false;
    QByteArray errorBody;
    QString safeError;
    QEventLoop errorLoop;
    errorProvider.send(endpoint, key, payload, [&](QByteArray response, QString failure) {
        errorDone = true; errorBody = std::move(response); safeError = std::move(failure); errorLoop.quit();
    });
    runUntilDone(errorLoop);
    check(errorDone && errorBody.isEmpty() && !safeError.contains(key) && !safeError.contains("server error"),
          "provider error body is not returned or copied into diagnostics");

    ScriptedManager largeManager;
    largeManager.responses.enqueue({QByteArray(512 * 1024 + 1, 'x'), 200, 0});
    Provider largeProvider(nullptr, &largeManager);
    bool largeDone = false;
    int largeCallbackCount = 0;
    QString largeError;
    QEventLoop largeLoop;
    largeProvider.send(endpoint, key, payload, [&](QByteArray response, QString failure) {
        largeDone = true; ++largeCallbackCount;
        check(response.isEmpty(), "oversized response body is discarded");
        largeError = failure; largeLoop.quit();
    });
    runUntilDone(largeLoop);
    check(largeDone && largeCallbackCount == 1 && largeError.contains("size limit"),
          "provider enforces its response cap and completes exactly once");

    ScriptedManager cancelManager;
    cancelManager.responses.enqueue({QByteArrayLiteral("late"), 200, 300});
    Provider cancelProvider(nullptr, &cancelManager);
    bool cancelledCallback = false;
    cancelProvider.send(endpoint, key, payload, [&](QByteArray, QString) { cancelledCallback = true; });
    cancelProvider.cancel();
    QEventLoop cancelLoop;
    QTimer::singleShot(50, &cancelLoop, &QEventLoop::quit);
    cancelLoop.exec();
    check(!cancelledCallback, "cancelled network request does not invoke completion");

    ScriptedManager orderManager;
    orderManager.responses.enqueue({QByteArrayLiteral("old"), 200, 100});
    orderManager.responses.enqueue({QByteArrayLiteral("new"), 200, 0});
    Provider orderProvider(nullptr, &orderManager);
    bool oldCallback = false, newCallback = false;
    QByteArray newest;
    QEventLoop orderLoop;
    orderProvider.send(endpoint, key, payload, [&](QByteArray, QString) { oldCallback = true; });
    orderProvider.send(endpoint, key, payload, [&](QByteArray response, QString) {
        newCallback = true; newest = std::move(response); orderLoop.quit();
    });
    runUntilDone(orderLoop);
    check(newCallback && newest == QByteArrayLiteral("new") && !oldCallback,
          "superseded network response cannot overwrite the newest request");
}

void testDialogConstructionAndPrivacy()
{
    GeneralAuthoringDialog dialog(nullptr, defaultSpec(), {}, {"wei", "shu", "wu", "qun", "god"},
                                  QByteArrayLiteral("card-art-snapshot"));
    auto *endpoint = dialog.findChild<QLineEdit *>(QStringLiteral("authoringEndpoint"));
    auto *credential = dialog.findChild<QLineEdit *>(QStringLiteral("authoringCredential"));
    auto *reviewed = dialog.findChild<QPlainTextEdit *>(QStringLiteral("authoringReviewedCode"));
    auto *candidate = dialog.findChild<QPlainTextEdit *>(QStringLiteral("authoringCandidate"));
    auto *diagnostics = dialog.findChild<QPlainTextEdit *>(QStringLiteral("authoringDiagnostics"));
    auto *tabs = dialog.findChild<QTabWidget *>();
    check(endpoint && credential && reviewed && candidate && diagnostics && tabs, "dialog constructs its expected authoring controls");
    if (endpoint && credential && reviewed && candidate && diagnostics) {
        check(credential->echoMode() == QLineEdit::Password, "provider credential field is masked");
        check(credential->maxLength() == 4096, "credential field has the configured input bound");
        check(reviewed->toPlainText().isEmpty() && candidate->toPlainText().isEmpty(),
              "dialog opens with inert text fields and does not synthesize or execute Lua");
        check(endpoint->focusPolicy() != Qt::NoFocus, "endpoint field participates in keyboard focus");
        check(candidate->tabChangesFocus(), "code editor preserves Tab as keyboard navigation");
        reviewed->setPlainText(validReviewed(defaultSpec()));
        const QString credentialValue = QStringLiteral("test-prefix-redaction-key");
        for (int length = 1; length <= credentialValue.size(); ++length)
            credential->setText(credentialValue.left(length));
        check(QMetaObject::invokeMethod(credential, "editingFinished", Qt::DirectConnection),
              "credential is registered after editing is finished");
        QPushButton *validate = nullptr;
        for (auto *button : dialog.findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("Validate code")) validate = button;
        check(validate != nullptr, "dialog exposes the local validation action");
        if (validate) validate->click();
        check(diagnostics->toPlainText().startsWith(QStringLiteral("Static checks passed")),
              "typing credential prefixes does not redact a valid local validation result");
        dialog.reject();
        check(credential->text().isEmpty(), "closing the dialog clears the in-memory credential field");
    }
    Q_UNUSED(tabs);
}

void testDialogMockGenerationCorrectionAndCancel()
{
    const QJsonObject originalSpec = defaultSpec();
    const QString key = QStringLiteral("dialog-mock-key");
    ScriptedManager manager;
    GeneralAuthoringDialog dialog(nullptr, originalSpec, {}, {"wei", "shu", "wu", "qun", "god"},
                                  QByteArrayLiteral("card-art-snapshot"), &manager);
    dialog.show();
    QApplication::processEvents();

    auto *endpoint = dialog.findChild<QLineEdit *>(QStringLiteral("authoringEndpoint"));
    auto *credential = dialog.findChild<QLineEdit *>(QStringLiteral("authoringCredential"));
    auto *model = dialog.findChild<QLineEdit *>(QStringLiteral("authoringModel"));
    auto *reviewed = dialog.findChild<QPlainTextEdit *>(QStringLiteral("authoringReviewedCode"));
    auto *candidate = dialog.findChild<QPlainTextEdit *>(QStringLiteral("authoringCandidate"));
    auto *diagnostics = dialog.findChild<QPlainTextEdit *>(QStringLiteral("authoringDiagnostics"));
    QLineEdit *displayName = nullptr;
    for (auto *edit : dialog.findChildren<QLineEdit *>())
        if (edit->text() == originalSpec.value("display_name").toString()) displayName = edit;
    QPlainTextEdit *instruction = nullptr;
    for (auto *edit : dialog.findChildren<QPlainTextEdit *>())
        if (edit->placeholderText().startsWith(QStringLiteral("Describe what to generate"))) instruction = edit;
    check(endpoint && credential && model && reviewed && candidate && diagnostics && displayName && instruction,
          "dialog mock flow locates the provider, metadata, instruction and review controls");
    if (!endpoint || !credential || !model || !reviewed || !candidate || !diagnostics || !displayName || !instruction) {
        dialog.reject();
        return;
    }

    endpoint->setText(QStringLiteral("https://mock-provider.invalid/v1/chat/completions"));
    credential->setText(key);
    model->setText(QStringLiteral("mock-model"));
    instruction->setPlainText(QStringLiteral("Create the described skill."));

    auto button = [&dialog](const QString &text) -> QPushButton * {
        for (auto *item : dialog.findChildren<QPushButton *>())
            if (item->text() == text) return item;
        return nullptr;
    };
    auto approvePreview = [&dialog, &button](QByteArray *shownPayload) {
        auto *previewRequest = button(QStringLiteral("Preview request"));
        if (!previewRequest) { check(false, "dialog exposes Preview request"); return false; }
        auto completed = std::make_shared<bool>(false);
        QTimer::singleShot(0, &dialog, [&dialog, shownPayload, completed] {
            auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!modal || modal == &dialog) { check(false, "request preview opens a modal review dialog"); return; }
            if (auto *view = modal->findChild<QPlainTextEdit *>()) *shownPayload = view->toPlainText().toUtf8();
            QPushButton *send = nullptr;
            for (auto *item : modal->findChildren<QPushButton *>())
                if (item->text() == QStringLiteral("Send request")) send = item;
            check(send != nullptr, "request preview offers an explicit Send request action");
            if (send) { *completed = true; send->click(); }
        });
        QTimer::singleShot(2000, &dialog, [&dialog, completed] {
            if (*completed) return;
            if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                modal && modal != &dialog) modal->reject();
        });
        previewRequest->click();
        return *completed;
    };

    const QString firstSkills = skillBody(originalSpec, 1);
    const QString expectedFirstCode = assemble(originalSpec, firstSkills);
    manager.responses.enqueue({apiResponse(firstSkills, QStringLiteral("First draft note.")), 200, 0});
    QByteArray firstPreview;
    check(approvePreview(&firstPreview), "first generation request is explicitly approved in the mock preview");
    check(manager.requests.size() == 1, "first dialog generation uses only the injected network manager");
    const auto firstRequest = manager.requests.isEmpty() ? QJsonObject{}
        : QJsonDocument::fromJson(manager.requests.first().body).object();
    check(firstRequest.value("model").toString() == QStringLiteral("mock-model"),
          "dialog sends the selected model through the provider payload");
    if (!manager.requests.isEmpty()) {
        check(manager.requests.first().request.rawHeader("Authorization") == QByteArray("Bearer ") + key.toUtf8(),
              "dialog sends the entered key only as provider authorization");
        check(!manager.requests.first().body.contains(key.toUtf8()), "dialog excludes the key from model content");
    }
    check(QJsonDocument::fromJson(firstPreview).object().value("model").toString() == QStringLiteral("mock-model"),
          "modal displays the exact outbound request payload for review");
    check(waitForCondition([&] { return candidate->toPlainText() == expectedFirstCode; }),
          "mock provider response becomes a full assembled candidate in the dialog");
    check(reviewed->toPlainText().isEmpty(), "generated candidate does not replace reviewed code before Apply");
    check(diagnostics->toPlainText().contains(QStringLiteral("First draft note.")),
          "provider notes reach the review status area");
    if (auto *apply = button(QStringLiteral("Apply reviewed candidate"))) apply->click();
    else check(false, "dialog exposes Apply reviewed candidate");
    check(reviewed->toPlainText() == expectedFirstCode && candidate->toPlainText().isEmpty(),
          "explicit Apply promotes candidate to reviewed code");

    QString manualCode = reviewed->toPlainText();
    manualCode.replace(QStringLiteral("player:drawCards(1"), QStringLiteral("player:drawCards(2"));
    check(manualCode != expectedFirstCode, "manual review edit changes a skill effect");
    reviewed->setPlainText(manualCode);
    const QString editedName = QStringLiteral("Dialog metadata edit");
    displayName->setText(editedName);
    instruction->setPlainText(QStringLiteral("Keep the manual draw change and revise the trigger."));

    auto currentSpec = originalSpec;
    currentSpec["display_name"] = editedName;
    const QString correctionSkills = skillBody(currentSpec, 3);
    manager.responses.enqueue({apiResponse(correctionSkills, QStringLiteral("Correction note.")), 200, 300});
    QByteArray correctionPreview;
    check(approvePreview(&correctionPreview), "correction request is explicitly approved in the mock preview");
    check(manager.requests.size() == 2, "correction request uses the injected mock transport");
    const QByteArray actualCorrectionBody = manager.requests.size() > 1 ? manager.requests.at(1).body : QByteArray{};
    check(actualCorrectionBody == correctionPreview,
          "the exact reviewed correction preview is the body sent to the mock provider");
    const auto correctionRequest = QJsonDocument::fromJson(actualCorrectionBody).object();
    const auto messages = correctionRequest.value("messages").toArray();
    const auto correctionContent = messages.size() > 1
        ? QJsonDocument::fromJson(messages.at(1).toObject().value("content").toString().toUtf8()).object()
        : QJsonObject{};
    check(correctionContent.value("original_spec").toObject().value("display_name") == originalSpec.value("display_name"),
          "actual correction request retains the original metadata");
    check(correctionContent.value("current_spec").toObject().value("display_name") == editedName,
          "actual correction request includes the edited metadata");
    check(correctionContent.value("reviewed_code").toString() == manualCode,
          "actual correction request includes the exact manual reviewed code");
    check(correctionContent.value("correction_request").toString() == instruction->toPlainText(),
          "actual correction request includes the current instruction");
    check(reviewed->toPlainText() == manualCode, "reviewed manual edit remains visible while correction is pending");

    if (auto *cancel = button(QStringLiteral("Cancel request"))) cancel->click();
    else check(false, "dialog exposes Cancel request while a provider request is pending");
    QEventLoop settleCancel;
    QTimer::singleShot(350, &settleCancel, &QEventLoop::quit);
    settleCancel.exec();
    check(reviewed->toPlainText() == manualCode && candidate->toPlainText().isEmpty(),
          "cancelling correction leaves reviewed manual code intact and no candidate applied");
    check(diagnostics->toPlainText().contains(QStringLiteral("Request cancelled")),
          "dialog reports that the pending correction was cancelled");

    // A later response must also stay stale if a user edits reviewed text while it is in flight.
    manager.responses.enqueue({apiResponse(correctionSkills, QStringLiteral("Late note.")), 200, 120});
    QByteArray stalePreview;
    check(approvePreview(&stalePreview), "stale-response request is explicitly approved in the mock preview");
    const QString inFlightEdit = manualCode + QStringLiteral("\n-- typed while the request was pending\n");
    reviewed->setPlainText(inFlightEdit);
    check(waitForCondition([&] { return diagnostics->toPlainText().contains(QStringLiteral("document changed during the request")); }),
          "dialog reports that an out-of-date response was ignored");
    check(reviewed->toPlainText() == inFlightEdit && candidate->toPlainText().isEmpty(),
          "stale provider response cannot overwrite the in-flight manual edit");
    if (!manager.requests.isEmpty()) {
        bool bodyContainsKey = false;
        for (const auto &request : manager.requests) bodyContainsKey = bodyContainsKey || request.body.contains(key.toUtf8());
        check(!bodyContainsKey, "no dialog request body contains the provider credential");
    }
    dialog.reject();
    check(credential->text().isEmpty(), "closing the mock dialog clears its key after real UI interactions");
}

void benchmarkLexicalValidation()
{
    const QJsonObject spec = defaultSpec();
    const Context context = bundledContext();
    const QList<int> targets{8192, 16384, 32768, MaxCodeBytes - 1};
    const auto measure = [&](const QString &name, const QString &code, int target) {
        QElapsedTimer timer;
        timer.start();
        const QStringList errors = validateCode(spec, code, context);
        const qint64 elapsedMs = timer.elapsed();
        std::cout << name.toStdString() << " target=" << target
                  << " actual=" << code.toUtf8().size() << " elapsed_ms=" << elapsedMs
                  << " diagnostics=" << errors.size() << '\n';
        if (code.toUtf8().size() != target || !errors.isEmpty()) {
            ++failures;
            std::cerr << "Benchmark fixture invalid: " << name.toStdString() << '\n';
        }
    };

    std::cout << "validateCode lexical benchmark (single run per fixture; timing is informational)\n";
    for (const int target : targets) {
        const QString tokens = skillBody(spec) + QStringLiteral("\nlocal short_token = 1\n");
        measure(QStringLiteral("ordinary-padding"), codeAtSize(spec, tokens, target), target);

        QString manyCalls = skillBody(spec);
        const QString call = QStringLiteral("player : isAlive()\n");
        while (assemble(spec, manyCalls + call).toUtf8().size() <= target) manyCalls += call;
        measure(QStringLiteral("many-calls"), codeAtSize(spec, manyCalls, target), target);

        QString manyNotExpressions = skillBody(spec);
        const QString notExpression = QStringLiteral("do local condition = not (true) end\n");
        while (assemble(spec, manyNotExpressions + notExpression).toUtf8().size() <= target)
            manyNotExpressions += notExpression;
        measure(QStringLiteral("many-not-keywords"), codeAtSize(spec, manyNotExpressions, target), target);

        const int baseBytes = assemble(spec, skillBody(spec)).toUtf8().size();
        const QString longComment = longDelimiterComment(target - baseBytes);
        measure(QStringLiteral("long-delimiter-decoys"),
                assemble(spec, skillBody(spec) + longComment), target);
    }
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (argc > 1 && QByteArray(argv[1]) == QByteArrayLiteral("--benchmark-lexical")) {
        benchmarkLexicalValidation();
        return failures ? 1 : 0;
    }
    testSpecAndCodeContracts();
    testLuaLexicalMasking();
    testLuaKeywordCallRecognition();
    testDocumentRequestsAndHistory();
    testCorrectionPayloadAndProjects();
    testDisabledExportSafety();
    testEndpointAndProviderTransport();
    testDialogConstructionAndPrivacy();
    testDialogMockGenerationCorrectionAndCancel();
    if (failures) std::cerr << failures << " contract test(s) failed\n";
    else std::cout << "All general authoring contract tests passed\n";
    return failures ? 1 : 0;
}
