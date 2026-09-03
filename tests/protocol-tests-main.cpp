// 協定層契約測試共用執行檔。五個 suite 全部只需要 Qt6::Core／Qt6::Network 同
// qsanguosha_protocol_v2_contract_support(reply-adapter 再加 client_core),
// 冇一個要 engine 或者 GUI,所以夾埋一個 target 唔會改變任何一個測試嘅
// link 面。每個 suite 依然行喺自己嘅 process。
#include "test-suite.h"

#include <QCoreApplication>

int runProtocolMessagesTests();
int runProtocolV2CodecTests();
int runProtocolFlowInventoryTests(int argc, char **argv);
int runAllInteractionPayloadTests();
int runInteractionReplyAdapterTests(int argc, char **argv);

int main(int argc, char **argv)
{
    // 子 suite 各自會起自己嘅 QCoreApplication,所以呢條路唔可以先起一個。
    const QString suite = parseSuite(argc, argv);
    if (suite == QLatin1String("protocol-messages"))
        return runProtocolMessagesTests();
    if (suite == QLatin1String("v2-codec"))
        return runProtocolV2CodecTests();
    if (suite == QLatin1String("flow-inventory"))
        return runProtocolFlowInventoryTests(argc, argv);
    if (suite == QLatin1String("all-interaction-payloads"))
        return runAllInteractionPayloadTests();
    if (suite == QLatin1String("reply-adapter"))
        return runInteractionReplyAdapterTests(argc, argv);
    if (!suite.isEmpty())
        return 64;

    QCoreApplication application(argc, argv);
    return runIsolatedTestCases("PROTOCOL_CONTRACT_RESULT", {
        {QStringLiteral("protocol-messages"),
         {QStringLiteral("--suite"), QStringLiteral("protocol-messages")}},
        {QStringLiteral("v2-codec"),
         {QStringLiteral("--suite"), QStringLiteral("v2-codec")}},
        {QStringLiteral("flow-inventory"),
         {QStringLiteral("--suite"), QStringLiteral("flow-inventory")}},
        {QStringLiteral("all-interaction-payloads"),
         {QStringLiteral("--suite"), QStringLiteral("all-interaction-payloads")}},
        {QStringLiteral("reply-adapter"),
         {QStringLiteral("--suite"), QStringLiteral("reply-adapter")}}
    });
}
