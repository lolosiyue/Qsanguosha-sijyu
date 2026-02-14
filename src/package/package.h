#ifndef _PACKAGE_H
#define _PACKAGE_H

class Skill;
class Card;
class Player;

class CardPattern
{
public:
    virtual bool match(const Player *player, const Card *card) const = 0;
    virtual bool willThrow() const
    {
        return true;
    }
};

class Package : public QObject
{
    Q_OBJECT
    Q_ENUMS(Type)

public:
    enum Type
    {
        GeneralPack, CardPack, SpecialPack
    };

    Package(const QString &name, Type pack_type = GeneralPack)
    {
		forbid = name.startsWith("~");
		if(forbid) setObjectName(name.mid(1));
		else setObjectName(name);
        type = pack_type;
    }

    QList<const QMetaObject *> getMetaObjects() const
    {
        return metaobjects;
    }

    QList<const Skill *> getSkills() const
    {
        return skills;
    }

    inline void addSkills(const Skill *skill)
    {
        skills << skill;
    }

    QMap<QString, const CardPattern *> getPatterns() const
    {
        return patterns;
    }

    QMultiMap<QString, QString> getRelatedSkills() const
    {
        return related_skills;
    }

    QMultiMap<QString, QString> getConvertPairs() const
    {
        return convert_pairs;
    }

    Type getType() const
    {
        return type;
    }

    bool isForbid() const
    {
        return forbid;
    }

    template<typename T>
    void addMetaObject()
    {
        metaobjects << &T::staticMetaObject;
    }

    inline void insertRelatedSkills(const QString &main_skill, const QString &related_skill)
    {
        related_skills.insertMulti(main_skill, related_skill);
    }

    inline void insertConvertPairs(const QString &from, const QString &to)
    {
        convert_pairs.insertMulti(from, to);
    }

protected:
    Type type;
	bool forbid;
    QList<const Skill *> skills;
    QList<const QMetaObject *> metaobjects;
    QMap<QString, const CardPattern *> patterns;
    QMultiMap<QString, QString> related_skills, convert_pairs;
};

typedef QHash<QString, Package *> PackageHash;

class PackageAdder
{
public:
    PackageAdder(const QString &name, Package *pack)
    {
		packages()[name] = pack;
    }

    static PackageHash &packages(void);
};

#define ADD_PACKAGE(name) static PackageAdder name##PackageAdder(#name, new name##Package);

#endif