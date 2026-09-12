// Shared executable for the protocol-layer contract tests. All five suites
// need only Qt6::Core/Qt6::Network and qsanguosha_protocol_v2_contract_support
// (reply-adapter additionally links client_core); none needs the engine or the
// GUI, so merging them into one target changes no suite's link surface. Each
// suite still runs in its own process.
#include "test-suite.h"

#include <QCoreApplication>

int runProtocolMessagesTests();
int runProtocolV2CodecTests();
int runProtocolFlowInventoryTests(int argc, char **argv);
int runAllInteractionPayloadTests();
int runInteractionReplyAdapterTests(int argc, char **argv);

int main(int argc, char **argv)
{
    // Each sub-suite creates its own QCoreApplication, so this path must not create one first.
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
