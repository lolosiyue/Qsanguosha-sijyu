#include "json.h"

#include <QColor>

bool JsonUtils::tryParse(const QVariant &arg, QColor &color)
{
    JsonArray args = arg.value<JsonArray>();
    if (args.size() < 3) return false;

    color.setRed(args[0].toInt());
    color.setGreen(args[1].toInt());
    color.setBlue(args[2].toInt());
    color.setAlpha(args.size() > 3 ? args[3].toInt() : 255);

    return true;
}
