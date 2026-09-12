#ifndef QSAN_ASSET_MANIFEST_H
#define QSAN_ASSET_MANIFEST_H

#include <QString>
#include <QStringList>
#include <QVariantMap>

// Asset manifest (Linux GUI M3).
//
// Neither the repository nor clean CI carries the full art/audio assets (see AGENTS.md).
// Neither does the shipped core runtime package: it includes rules, Lua and UI scripts but
// not the gigabytes of portraits and voice. So "what is mandatory" versus "what can be
// missing with only some lost sound/art" must be data that can be read, verified and
// printed when something is missing — not assumptions scattered through the code.
//
// The manifest lives in the asset root: <assetRoot>/assets-manifest.json.
namespace QSanAssetManifest
{
struct Entry
{
    QString path;
    bool required = false;
    bool present = false;
};

struct Report
{
    bool manifestPresent = false;
    QString manifestPath;
    QString error;            // manifest unreadable/malformed (a missing manifest does not count)
    int schemaVersion = 0;
    QString gameVersion;
    QString assetPackVersion;
    QString assetRoot;
    QList<Entry> entries;

    QStringList missingRequired() const;
    QStringList missingOptional() const;
    // A missing required entry means the package is broken; missing optional entries just
    // mean less content.
    bool complete() const { return error.isEmpty() && missingRequired().isEmpty(); }
};

// Reads the manifest and checks each entry for existence. With assetRoot empty,
// QSanRuntimePaths::assetRoot() is used.
//
// With manifestPath empty, <assetRoot>/assets-manifest.json is used (the installed/
// packaged location). The dev tree and CI never install anything; the manifest only
// exists in the build directory, so an explicit path must be passable — otherwise "which
// assets are expected to be missing" goes unanswered exactly where it matters most
// (running the GUI from a clean checkout).
Report inspect(const QString &assetRoot = QString(), const QString &manifestPath = QString());

// Human-readable missing-asset diagnostics (one sentence per line, never crashes, never
// poses as an error).
QStringList diagnostics(const Report &report);

// JSON-able; used by --asset-report and the package smoke test.
QVariantMap describe(const Report &report);
}

#endif
