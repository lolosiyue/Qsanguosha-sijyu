#include "package-runtime.h"
#include "package-catalog.h"
#include "runtime-paths.h"
#if !defined(Q_OS_ANDROID) && !defined(__EMSCRIPTEN__) && !defined(QSAN_XP_LEGACY)
#include "package-store.h"
#endif
#include <QDir>
#include <QJsonArray>
#include <QSet>

namespace QSanPackages {
bool prepareRuntime(QString *error)
{
    clearCatalog();
#if !defined(Q_OS_ANDROID) && !defined(__EMSCRIPTEN__) && !defined(QSAN_XP_LEGACY)
    PackageStore store(QSanRuntimePaths::assetRoot(), QSanRuntimePaths::userDataRoot());
    if (QSanRuntimePaths::isResolved()) {
        if (store.hasUnsuccessfulBootAttempt() && !store.recoverPrevious(error)) return false;
        if (!store.prepareStartup(error)) return false;
    }
#endif
#ifndef __EMSCRIPTEN__
#ifdef Q_OS_ANDROID
    const Catalog catalog = loadCatalog(QSanRuntimePaths::assetRoot(), false, false);
#else
    const Catalog catalog = loadCatalog(QSanRuntimePaths::assetRoot());
#endif
    if (!catalog.isValid()) {
        if (error) *error = catalog.error;
        return false;
    }
    installCatalog(catalog);
#endif
#if !defined(Q_OS_ANDROID) && !defined(__EMSCRIPTEN__) && !defined(QSAN_XP_LEGACY)
    return !QSanRuntimePaths::isResolved() || store.beginBootAttempt(error);
#else
    return true;
#endif
}

QSanRules::ContentManifest effectiveContent(const QSanRules::ContentManifest &content,
                                          const QSanRules::ContentManifest &baseline)
{
#ifdef __EMSCRIPTEN__
    Q_UNUSED(baseline);
    // The worker has verified the delivered Lua closure. Media lives outside
    // MEMFS; its package metadata travels in the verified runtime descriptor.
    Catalog catalog;
    for (const auto &value : content.packages) {
        const QJsonObject metadata = value.toObject();
        Package package;
        package.id = metadata.value(QStringLiteral("id")).toString();
        package.version = metadata.value(QStringLiteral("version")).toString();
        package.root = QDir(QSanRuntimePaths::assetRoot()).filePath(QStringLiteral("packages/") + package.id);
        package.manifest = metadata;
        for (const auto &dependency : metadata.value(QStringLiteral("dependencies")).toArray())
            package.dependencies.append(dependency.toString());
        catalog.packages.append(package);
    }
    installCatalog(catalog);
    return content;
#else
    const Catalog catalog = activeCatalog();
    if (catalog.packages.isEmpty() && content.packages.isEmpty()) return content;
#ifdef Q_OS_ANDROID
    Q_UNUSED(baseline);
    if (!content.packages.isEmpty() && content.packages != packageDescriptors(catalog)) {
        QSanRules::ContentManifest invalid = content;
        invalid.error = QStringLiteral("runtime-content.json package catalog does not match installed packages");
        return invalid;
    }
    return QSanRules::mergePackageContent(content, extensionDescriptors(catalog), packageDescriptors(catalog));
#else
    // Desktop installs replace the catalog. A previously exported descriptor
    // must not pin its old package versions after a valid staged update.
    QSanRules::ContentManifest effective = content;
    effective.packages = {};
    const QJsonArray extensions = extensionDescriptors(catalog);
    QSet<QString> names;
    for (const auto &value : extensions) names.insert(value.toObject().value(QStringLiteral("name")).toString());
    for (int index = effective.entries.size() - 1; index >= 0; --index) {
        auto &entry = effective.entries[index];
        if (!entry.script.startsWith(QLatin1String("packages/")) || names.contains(entry.name)) continue;
        bool restored = false;
        for (const auto &original : baseline.entries) {
            if (original.name == entry.name) {
                entry = original;
                restored = true;
                break;
            }
        }
        if (!restored) effective.entries.removeAt(index);
    }
    return QSanRules::mergePackageContent(effective, extensions, packageDescriptors(catalog));
#endif
#endif
}

bool completeBoot(QString *error)
{
#if !defined(Q_OS_ANDROID) && !defined(__EMSCRIPTEN__) && !defined(QSAN_XP_LEGACY)
    PackageStore store(QSanRuntimePaths::assetRoot(), QSanRuntimePaths::userDataRoot());
    return !QSanRuntimePaths::isResolved() || store.markBootSuccessful(error);
#else
    Q_UNUSED(error);
    return true;
#endif
}
}
