#ifndef QSAN_THEME_PACK_H
#define QSAN_THEME_PACK_H

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

// Theme packs replace room art slot by slot, like resource packs: each pack is a folder
// with a theme.json manifest, packs are stacked in a user-chosen order, and any slot no
// enabled pack fills keeps the skin's own art. skins/theme-slots.json lists the slots.
//
// With no pack enabled every lookup returns an empty string before touching a lock or
// the filesystem, so the default look is unchanged.
namespace ThemePacks
{
struct Slot
{
    QString id;
    QString label;
    QString group;
    QString note;
    bool directory = false;
    // Built-in art for the slot; empty when the room draws it in code instead.
    QString defaultPath;
    // Also redirect plain loads of defaultPath (other code reading the same file).
    bool redirect = false;
    QStringList skinKeys;
    int width = 0, height = 0;
};

struct Pack
{
    QString id;
    QString name;
    QString author;
    QString version;
    QString description;
    // Absolute folder holding theme.json.
    QString root;
    // Absolute preview image, empty when the pack has none.
    QString preview;
    // Slot id -> absolute file or folder (only entries that exist).
    QMap<QString, QString> slotFiles;
    // Legacy asset path (case-folded, "image/...") -> absolute file; a key ending in '/'
    // maps a whole folder.
    QMap<QString, QString> files;
    // Problems found while reading the manifest; the pack still loads what it can.
    QStringList warnings;
};

const QList<Slot> &slotTable();
const Slot *findSlot(const QString &id);

// Folders scanned for packs: <asset root>/themes, then <user data>/themes.
QStringList searchDirectories();
// Writable folder players drop packs into.
QString userThemeDirectory();

// Installed packs, scanned once and cached until reload().
QList<Pack> installed();
// Rescans the folders and rebuilds the override tables from the saved order.
void reload();

// Enabled pack ids, highest priority first.
QStringList enabledIds();
// Saves the order and rebuilds the override tables; returns true when anything changed.
bool setEnabledIds(const QStringList &ids);

// True when at least one enabled pack overrides something.
bool isActive();
// Bumped on every rebuild so pixmap caches keyed by it drop themed results.
quint64 revision();

// Absolute replacement for a slot, or empty when no enabled pack provides it.
QString overrideForSlot(const QString &slotId);
// Absolute replacement for a skin image key, or empty.
QString overrideForKey(const QString &skinKey);
// Absolute replacement for a legacy asset path such as "image/system/card-back.png", or empty.
QString overrideForFile(const QString &legacyPath);
// Folder to read an animation from: the first enabled pack whose copy of legacyDir holds
// probe, else legacyDir itself. legacyDir ends with '/'.
QString resolveDirectory(const QString &legacyDir, const QString &probe = QStringLiteral("0.png"));

// Parses one pack folder; exposed for the manager page and tests.
Pack parsePack(const QString &directory, QString *error);
}

#endif
