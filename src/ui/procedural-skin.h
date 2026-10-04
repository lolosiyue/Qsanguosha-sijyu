#ifndef _PROCEDURAL_SKIN_H
#define _PROCEDURAL_SKIN_H

#include "json.h"

#include <QPixmap>
#include <QString>

// Skin art drawn at runtime from text and flat colors, so a skin can run without image assets.
// An image config entry names it with a virtual file name:
//   gen:<kind>/<arg>?w=<width>&h=<height>&<param>=<value>...
// Kinds: fill, label, general, card, suit, number, kingdom, role, equip, judge, button, skill.
namespace ProceduralSkin
{
bool isUri(const QString &fileName);
// buttons maps a button name to [label, width, height]; QSanButton takes its size from the pixmap.
QPixmap render(const QString &uri, const JsonObject &buttons);
}

#endif
