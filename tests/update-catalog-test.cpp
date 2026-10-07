#include "update-catalog.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

namespace {
int failures = 0;

void check(bool condition, const char *expression, int line)
{
    if (condition) return;
    qCritical().noquote() << QStringLiteral("line %1: %2").arg(line).arg(QString::fromLatin1(expression));
    ++failures;
}

#define CHECK(expression) check((expression), #expression, __LINE__)

const QString ValidDigest(64, QLatin1Char('a'));
const QString Repo = QStringLiteral("https://github.com/lolosiyue/Qsanguosha-sijyu");

QJsonObject material(const QString &id = QStringLiteral("sijyu-material"),
                     const QString &version = QStringLiteral("1.1.0"),
                     qint64 size = 1024,
                     const QString &gameVersion = QStringLiteral("20251231"))
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("version"), version},
            {QStringLiteral("name"), QStringLiteral("Sijyu materials")},
            {QStringLiteral("notes"), QStringLiteral("Catalog fixture")},
            {QStringLiteral("url"), QStringLiteral("https://cdn.example.test/sijyu.zip")},
            {QStringLiteral("sha256"), ValidDigest},
            {QStringLiteral("size"), double(size)},
            {QStringLiteral("game_version"), gameVersion}};
}

QByteArray materialCatalog(const QJsonArray &packages)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("schema_version"), 1},
                                     {QStringLiteral("packages"), packages}})
        .toJson(QJsonDocument::Compact);
}

QJsonObject releaseAsset(const QString &name,
                         const QString &digest = QStringLiteral("sha256:") + ValidDigest,
                         const QString &url = Repo + QStringLiteral("/releases/download/v1.1.0/asset"),
                         qint64 size = 4096)
{
    return {{QStringLiteral("name"), name},
            {QStringLiteral("digest"), digest},
            {QStringLiteral("size"), double(size)},
            {QStringLiteral("browser_download_url"), url}};
}

QJsonObject release(const QString &tag,
                    bool prerelease,
                    const QJsonArray &assets,
                    const QString &page = Repo + QStringLiteral("/releases/tag/v1.1.0"))
{
    return {{QStringLiteral("tag_name"), tag},
            {QStringLiteral("name"), tag},
            {QStringLiteral("body"), QStringLiteral("Release fixture")},
            {QStringLiteral("html_url"), page},
            {QStringLiteral("draft"), false},
            {QStringLiteral("prerelease"), prerelease},
            {QStringLiteral("assets"), assets}};
}

QByteArray releaseCatalog(const QJsonArray &releases)
{
    return QJsonDocument(releases).toJson(QJsonDocument::Compact);
}

void testMalformedAndOversizedCatalogs()
{
    using namespace QSanUpdates;
    CHECK(!materials(QByteArrayLiteral("{"), QStringLiteral("20251231"), {}).valid());
    CHECK(!materials(QByteArrayLiteral("[]"), QStringLiteral("20251231"), {}).valid());
    CHECK(!materials(QByteArrayLiteral("{}"), QStringLiteral("20251231"), {}).valid());
    CHECK(!materials(QByteArray(CatalogLimit + 1, ' '), QStringLiteral("20251231"), {}).valid());

    CHECK(!releases(QByteArrayLiteral("{"), QStringLiteral("1.0.0"),
                    QStringLiteral("linux-x86_64"), false).valid());
    CHECK(!releases(QByteArrayLiteral("{}"), QStringLiteral("1.0.0"),
                    QStringLiteral("linux-x86_64"), false).valid());
    CHECK(!releases(QByteArray(CatalogLimit + 1, ' '), QStringLiteral("1.0.0"),
                    QStringLiteral("linux-x86_64"), false).valid());

    QJsonArray tooManyPackages;
    for (int i = 0; i < 1001; ++i)
        tooManyPackages.append(material(QStringLiteral("package-%1").arg(i)));
    CHECK(!materials(materialCatalog(tooManyPackages), QStringLiteral("20251231"), {}).valid());

    QJsonArray tooManyReleases;
    for (int i = 0; i < 101; ++i)
        tooManyReleases.append(QJsonObject{});
    CHECK(!releases(releaseCatalog(tooManyReleases), QStringLiteral("1.0.0"),
                    QStringLiteral("linux-x86_64"), false).valid());
}

void testHttpsUrlValidation()
{
    using namespace QSanUpdates;
    CHECK(safeHttps(QUrl(QStringLiteral("https://cdn.example.test/catalog.json"))));
    CHECK(safeHttps(QUrl(QStringLiteral("https://cdn.example.test:443/catalog.json"))));
    CHECK(!safeHttps(QUrl(QStringLiteral("http://cdn.example.test/catalog.json"))));
    CHECK(!safeHttps(QUrl(QStringLiteral("https://user:pass@cdn.example.test/catalog.json"))));
    CHECK(!safeHttps(QUrl(QStringLiteral("https://cdn.example.test/catalog.json#fragment"))));
    CHECK(!safeHttps(QUrl(QStringLiteral("https://cdn.example.test:444/catalog.json"))));
    CHECK(!safeHttps(QUrl(QStringLiteral("https:///catalog.json"))));
    CHECK(!safeHttps(QUrl(QStringLiteral("not a URL"))));

    auto invalidAuth = material();
    invalidAuth.insert(QStringLiteral("url"), QStringLiteral("https://user:pass@cdn.example.test/package.zip"));
    CHECK(!materials(materialCatalog({invalidAuth}), QStringLiteral("20251231"), {}).valid());
    auto invalidFragment = material();
    invalidFragment.insert(QStringLiteral("url"), QStringLiteral("https://cdn.example.test/package.zip#payload"));
    CHECK(!materials(materialCatalog({invalidFragment}), QStringLiteral("20251231"), {}).valid());
}

void testStrictVersionComparison()
{
    using namespace QSanUpdates;
    bool comparable = false;
    CHECK(compareVersions(QStringLiteral("1.10.0"), QStringLiteral("1.9.9"), &comparable) > 0 && comparable);
    CHECK(compareVersions(QStringLiteral("v1.2.3"), QStringLiteral("1.2.3"), &comparable) == 0 && comparable);
    CHECK(compareVersions(QStringLiteral("20261231"), QStringLiteral("20260101"), &comparable) > 0 && comparable);
    CHECK(compareVersions(QStringLiteral("1.2.0-rc.2"), QStringLiteral("1.2.0-rc.10"), &comparable) < 0 && comparable);
    CHECK(compareVersions(QStringLiteral("1.2.0-rc.1"), QStringLiteral("1.2.0"), &comparable) < 0 && comparable);
    CHECK(compareVersions(QStringLiteral("20261231"), QStringLiteral("1.0.0"), &comparable) == 0 && !comparable);
    CHECK(compareVersions(QStringLiteral("20260230"), QStringLiteral("20260228"), &comparable) == 0 && !comparable);

    for (const QString &bad : {QStringLiteral("01.2.3"), QStringLiteral("1.02.3"),
                               QStringLiteral("1.2"), QStringLiteral("1.2.3.4"),
                               QStringLiteral("1.2.3-01"), QStringLiteral("2026123")}) {
        compareVersions(bad, QStringLiteral("1.2.3"), &comparable);
        CHECK(!comparable);
    }
}

void testMaterialCatalogValidationAndGates()
{
    using namespace QSanUpdates;
    const QString gameVersion = QStringLiteral("20251231");
    const auto valid = materials(materialCatalog({material()}), gameVersion, {});
    CHECK(valid.valid());
    CHECK(valid.entries.size() == 1);
    if (valid.entries.size() == 1)
        CHECK(valid.entries.first().downloadable);

    auto duplicate = material(QStringLiteral("duplicate"));
    CHECK(!materials(materialCatalog({duplicate, duplicate}), gameVersion, {}).valid());
    CHECK(!materials(materialCatalog({material(QStringLiteral("core"))}), gameVersion, {}).valid());
    CHECK(!materials(materialCatalog({material(QStringLiteral("Uppercase"))}), gameVersion, {}).valid());
    CHECK(!materials(materialCatalog({material(QStringLiteral("bad-id!"))}), gameVersion, {}).valid());
    CHECK(!materials(materialCatalog({material(QStringLiteral("bad-version"), QStringLiteral("1.02.0"))}),
                      gameVersion, {}).valid());

    auto badDigest = material(QStringLiteral("bad-digest"));
    badDigest.insert(QStringLiteral("sha256"), QString(64, QLatin1Char('A')));
    CHECK(!materials(materialCatalog({badDigest}), gameVersion, {}).valid());
    auto missingDigest = material(QStringLiteral("missing-digest"));
    missingDigest.remove(QStringLiteral("sha256"));
    CHECK(!materials(materialCatalog({missingDigest}), gameVersion, {}).valid());
    CHECK(!materials(materialCatalog({material(QStringLiteral("zero-size"), QStringLiteral("1.1.0"), 0)}),
                      gameVersion, {}).valid());
    CHECK(!materials(materialCatalog({material(QStringLiteral("large-size"), QStringLiteral("1.1.0"), PackageLimit + 1)}),
                      gameVersion, {}).valid());

    const auto wrongGame = materials(materialCatalog({material(QStringLiteral("wrong-game"),
                                                              QStringLiteral("1.1.0"), 1024,
                                                              QStringLiteral("20260101"))}),
                                     gameVersion, {});
    CHECK(wrongGame.valid() && wrongGame.entries.size() == 1);
    if (wrongGame.entries.size() == 1)
        CHECK(!wrongGame.entries.first().downloadable);

    const auto sameVersion = materials(materialCatalog({material(QStringLiteral("same"),
                                                                 QStringLiteral("1.0.0"))}),
                                        gameVersion, {{QStringLiteral("same"), QStringLiteral("1.0.0")}});
    CHECK(sameVersion.valid() && sameVersion.entries.size() == 1);
    if (sameVersion.entries.size() == 1)
        CHECK(!sameVersion.entries.first().downloadable);
    const auto installedNewer = materials(materialCatalog({material(QStringLiteral("newer"),
                                                                     QStringLiteral("1.0.0"))}),
                                          gameVersion, {{QStringLiteral("newer"), QStringLiteral("2.0.0")}});
    CHECK(installedNewer.valid() && installedNewer.entries.size() == 1);
    if (installedNewer.entries.size() == 1)
        CHECK(!installedNewer.entries.first().downloadable);
    const auto incomparable = materials(materialCatalog({material(QStringLiteral("mixed-family"),
                                                                    QStringLiteral("1.1.0"))}),
                                        gameVersion, {{QStringLiteral("mixed-family"), gameVersion}});
    CHECK(incomparable.valid() && incomparable.entries.size() == 1);
    if (incomparable.entries.size() == 1)
        CHECK(!incomparable.entries.first().downloadable);
    const auto pending = materials(materialCatalog({material(QStringLiteral("pending"))}),
                                   gameVersion, {}, {{QStringLiteral("pending"), QStringLiteral("install")}});
    CHECK(pending.valid() && pending.entries.size() == 1);
    if (pending.entries.size() == 1)
        CHECK(!pending.entries.first().downloadable);
}

void testReleasePrereleaseAndDigestBehavior()
{
    using namespace QSanUpdates;
    const auto stable = release(QStringLiteral("v1.1.0"), false,
                                {releaseAsset(QStringLiteral("QSanguosha-linux-x86_64.tar.zst"))});
    const auto preview = release(QStringLiteral("v1.2.0-rc.1"), true,
                                 {releaseAsset(QStringLiteral("QSanguosha-linux-x86_64.tar.zst"))});
    const QByteArray catalog = releaseCatalog({stable, preview});
    const auto stableOnly = releases(catalog, QStringLiteral("1.0.0"),
                                     QStringLiteral("linux-x86_64"), false);
    CHECK(stableOnly.valid() && stableOnly.entries.size() == 1);
    if (stableOnly.entries.size() == 1) {
        CHECK(stableOnly.entries.first().version == QStringLiteral("v1.1.0"));
        CHECK(stableOnly.entries.first().downloadable);
    }

    const auto withPreview = releases(catalog, QStringLiteral("1.0.0"),
                                      QStringLiteral("linux-x86_64"), true);
    CHECK(withPreview.valid() && withPreview.entries.size() == 2);
    if (withPreview.entries.size() == 2)
        CHECK(withPreview.entries.first().downloadable && withPreview.entries.at(1).downloadable);
    const auto sameRelease = releases(releaseCatalog({stable}), QStringLiteral("1.1.0"),
                                      QStringLiteral("linux-x86_64"), true);
    CHECK(sameRelease.entries.size() == 1);
    if (sameRelease.entries.size() == 1)
        CHECK(sameRelease.entries.first().downloadable);

    const auto missingDigest = release(QStringLiteral("v1.1.0"), false,
        {releaseAsset(QStringLiteral("QSanguosha-linux-x86_64.tar.zst"), QString())});
    const auto noVerifiedAsset = releases(releaseCatalog({missingDigest}), QStringLiteral("1.0.0"),
                                          QStringLiteral("linux-x86_64"), false);
    CHECK(noVerifiedAsset.valid() && noVerifiedAsset.entries.size() == 1);
    if (noVerifiedAsset.entries.size() == 1)
        CHECK(!noVerifiedAsset.entries.first().downloadable);

    const auto badDigest = release(QStringLiteral("v1.1.0"), false,
        {releaseAsset(QStringLiteral("QSanguosha-linux-x86_64.tar.zst"), QStringLiteral("sha256:bad"))});
    const auto badDigestResult = releases(releaseCatalog({badDigest}), QStringLiteral("1.0.0"),
                                          QStringLiteral("linux-x86_64"), false);
    CHECK(badDigestResult.entries.size() == 1);
    if (badDigestResult.entries.size() == 1)
        CHECK(!badDigestResult.entries.first().downloadable);

    const auto badDownloadUrl = release(QStringLiteral("v1.1.0"), false,
        {releaseAsset(QStringLiteral("QSanguosha-linux-x86_64.tar.zst"),
                      QStringLiteral("sha256:") + ValidDigest,
                      QStringLiteral("https://attacker.example/download"))});
    const auto badDownloadResult = releases(releaseCatalog({badDownloadUrl}), QStringLiteral("1.0.0"),
                                            QStringLiteral("linux-x86_64"), false);
    CHECK(badDownloadResult.entries.size() == 1);
    if (badDownloadResult.entries.size() == 1)
        CHECK(!badDownloadResult.entries.first().downloadable);
}

void testPlatformCompatibilityAndManualFallback()
{
    using namespace QSanUpdates;
    QJsonArray assets{
        releaseAsset(QStringLiteral("QSanguosha-linux-x86_64.tar.zst")),
        releaseAsset(QStringLiteral("QSanguosha-linux-x86_64.AppImage")),
        releaseAsset(QStringLiteral("QSanguosha-windows-x86_64.AppImage")),
        releaseAsset(QStringLiteral("QSanguosha-windows-x64.zip")),
        releaseAsset(QStringLiteral("QSanguosha-XP-win64.zip")),
        releaseAsset(QStringLiteral("QSanguosha-windows-x64-server.zip")),
        releaseAsset(QStringLiteral("QSanguosha-windows-x64-tui.zip"))};
    const QByteArray catalog = releaseCatalog({release(QStringLiteral("v1.1.0"), false, assets)});

    const auto linuxCatalog = releases(catalog, QStringLiteral("1.0.0"), QStringLiteral("linux-x86_64"), false);
    CHECK(linuxCatalog.valid());
    CHECK(linuxCatalog.entries.size() == 2);
    CHECK(linuxCatalog.entries.at(0).downloadable && linuxCatalog.entries.at(1).downloadable);

    const auto windows = releases(catalog, QStringLiteral("1.0.0"), QStringLiteral("windows-x86_64"), false);
    CHECK(windows.valid() && windows.entries.size() == 1);
    if (windows.entries.size() == 1) {
        CHECK(windows.entries.first().downloadable);
        CHECK(windows.entries.first().name == QStringLiteral("QSanguosha-windows-x64.zip"));
    }

    const auto android = releases(catalog, QStringLiteral("1.0.0"), QStringLiteral("android-arm64"), false);
    CHECK(android.valid() && android.entries.size() == 1);
    if (android.entries.size() == 1)
        CHECK(!android.entries.first().downloadable);
    CHECK(!platform().isEmpty());

    const auto badReleasePage = release(QStringLiteral("v1.1.0"), false, assets,
                                        QStringLiteral("https://github.com.evil.test/lolosiyue/Qsanguosha-sijyu/releases/tag/v1.1.0"));
    CHECK(releases(releaseCatalog({badReleasePage}), QStringLiteral("1.0.0"),
                   QStringLiteral("linux-x86_64"), false).entries.isEmpty());
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc == 2) {
        QFile file(QString::fromLocal8Bit(argv[1]));
        CHECK(file.open(QIODevice::ReadOnly));
        const auto catalog = QSanUpdates::materials(file.read(QSanUpdates::CatalogLimit + 1),
                                                    QStringLiteral("20251231"), {}, {});
        CHECK(catalog.valid());
        CHECK(catalog.entries.size() == 1);
        if (catalog.entries.size() == 1) {
            CHECK(catalog.entries.first().id == QStringLiteral("material-test"));
            CHECK(catalog.entries.first().version == QStringLiteral("1.0.0"));
            CHECK(catalog.entries.first().downloadable);
        }
        qInfo() << "Generated catalog C++ parser round-trip checks:" << (failures ? "FAILED" : "passed");
        return failures ? 1 : 0;
    }
    testMalformedAndOversizedCatalogs();
    testHttpsUrlValidation();
    testStrictVersionComparison();
    testMaterialCatalogValidationAndGates();
    testReleasePrereleaseAndDigestBehavior();
    testPlatformCompatibilityAndManualFallback();
    auto badChannel = release(QStringLiteral("v1.1.0"), false, {});
    badChannel.insert(QStringLiteral("prerelease"), QStringLiteral("true"));
    CHECK(!QSanUpdates::releases(releaseCatalog({badChannel}), QStringLiteral("1.0.0"),
                                QStringLiteral("linux-x86_64"), false).valid());
    auto mixedArchitecture = release(QStringLiteral("v1.1.0"), false,
        {releaseAsset(QStringLiteral("QSanguosha-windows-arm64-x64.zip"))});
    const auto mixed = QSanUpdates::releases(releaseCatalog({mixedArchitecture}), QStringLiteral("1.0.0"),
                                            QStringLiteral("windows-x86_64"), false);
    CHECK(mixed.valid() && mixed.entries.size() == 1 && !mixed.entries.first().downloadable);
    if (failures) {
        qCritical() << failures << "update catalog checks failed";
        return 1;
    }
    qInfo() << "All update catalog checks passed";
    return 0;
}
