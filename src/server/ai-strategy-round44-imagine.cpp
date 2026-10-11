#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-round44-imagine.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QRegularExpression>
namespace {
bool matches(const QString &site,const QString &text)
{
    // Match the complete fixed-source format at each reached source location.
    // The Lua producer performs string.format first, preserving its errors.
    static const QHash<QString,QString> literals{
        {"56","vine+1"},{"60","gale+1"},{"69","guding+1"},
        {"76","luoyi+1"},{"82","drank+1"},{"87","jie+1"},
        {"624","in chained."},{"670","--------------------"},
        {"671","*******START********"},{"674","target:"},
        {"678","another:"},{"682","********END*********"},
        {"683","--------------------"}};
    const auto literal=literals.constFind(site);
    if(literal!=literals.cend())return text==literal.value();
    const QString integer=QStringLiteral("-?[0-9]+");
    QString pattern;
    if(site=="582")pattern="Now:immediateDamage="+integer;
    else if(site=="592")pattern="Now:damageValue="+integer+",fd="+integer+",fc="+integer+",ft="+integer;
    else if(site=="607")pattern="Now:damageValue="+integer+",ed="+integer+",ec="+integer+",et="+integer;
    else if(site=="672")pattern="damage from [\\s\\S]* to [\\s\\S]*:";
    else if(site=="673")pattern=" damageValue: "+integer;
    else if(site=="675"||site=="679")pattern=" damageCount: "+integer;
    else if(site=="676"||site=="680")pattern=" drawCards: "+integer;
    else if(site=="677"||site=="681")pattern=" turnDelt: "+integer;
    else return false;
    return QRegularExpression("\\A(?:"+pattern+")\\z").match(text).hasMatch();
}
}
bool emitAIRound44ImagineDiagnostic(Room &room,const AIRequest &request,ServerPlayer *viewer,
                                    const QString &site,const QString &message)
{
    if(!viewer||viewer->getRoom()!=&room||request.viewerObjectName!=viewer->objectName()
        ||!aiRound44RangeRequestCurrent(room,request,viewer)
        ||!room.getAllPlayers(true).contains(viewer)||message.size()>65536
        ||message.contains(QChar(0))||!matches(site,message))return false;
    // Identical SWIG Room.writeToConsole sink and source order. No pcall here:
    // a sink failure must not permit a later source diagnostic or return.
    room.output(message);
    qWarning("%s",qPrintable(message));
    return true;
}
