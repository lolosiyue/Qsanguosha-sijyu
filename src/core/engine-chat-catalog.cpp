#include "engine-chat-catalog.h"

#include "engine.h"
#include "general.h"
#include "lua-wrapper.h"
#include "lua.hpp"
#include "settings.h"
#include <QFileInfo>

QStringList EngineChatCatalog::easyTexts(const Engine &engine)
{
    LuaLocker locker;
    static QStringList easyTexts = GetConfigFromLuaState(engine.getLuaState(), "easy_text").toStringList();
    return easyTexts;
}
QList<EasyTextItem> EngineChatCatalog::easyTextItems(const Engine &engine, const QString &general_name)
{
    QList<EasyTextItem> items;

    lua_State *chat_lua = CreateLuaState();
    if (DoLuaScript(chat_lua, "lua/chat_config.lua")) {
        QVariant base_texts = GetValueFromLuaState(chat_lua, "chat_config", "easy_text");
        if (base_texts.canConvert<QStringList>()) {
            QStringList easy_texts = base_texts.toStringList();
            foreach (const QString &text, easy_texts) {
                if (!text.isEmpty()) {
                    items << EasyTextItem(text, QString(), 0);
                }
            }
        }
    }
    lua_close(chat_lua);

    if (!general_name.isEmpty()) {
        QStringList general_names = general_name.split(",");

        foreach (const QString &gname, general_names) {
            QString trimmed_name = gname.trimmed();
            if (trimmed_name.isEmpty()) continue;

            const General *general = engine.getGeneral(trimmed_name);
            if (!general) continue;

            QString actualGn = engine.getResourceAlias("heroskin", trimmed_name);

            QList<const Skill *> skills = general->getVisibleSkillList();

            foreach (const Skill *skill, skills) {
                foreach (QString sk, skill->getWakedSkills().split(",")) {
                    const Skill *ski = engine.getSkill(sk);
                    if (ski && ski->isVisible() && !skills.contains(ski))
                        skills << ski;
                }
            }

            foreach (QString skill_name, general->getRelatedSkillNames()) {
                const Skill *skill = engine.getSkill(skill_name);
                if (skill && skill->isVisible() && !skills.contains(skill))
                    skills << skill;
            }

            foreach (const Skill *skill, skills) {
                QString skill_obj_name = skill->objectName();

                int skin_index = Config.value("HeroSkin/" + trimmed_name, 0).toInt();
                if (skin_index > 0) general->tryLoadingSkinTranslation(skin_index);

                QStringList audio_sources = skill->getSources(actualGn, skin_index);
                QStringList actual_files;

                if (!audio_sources.isEmpty()) {
                    actual_files = audio_sources;
                }

                if (actual_files.isEmpty()) {
                    QString aliasSkill = engine.getResourceAlias("audios", skill_obj_name);
                    if (aliasSkill != skill_obj_name) {
                        const Skill *aliasSk = engine.getSkill(aliasSkill);
                        if (aliasSk) {
                            actual_files = aliasSk->getSources(actualGn, skin_index);
                        }
                    }
                }

                if (!actual_files.isEmpty()) {
                    for (int i = 0; i < actual_files.size(); i++) {
                        QString audio_file = actual_files[i];
                        QString line_key;

                        if (skin_index > 0) {
                            QString basename = QFileInfo(audio_file).baseName();
                            line_key = QString("$%1-%2_%3").arg(basename).arg(actualGn).arg(skin_index);
                        } else {
                            line_key = QString("$%1%2").arg(skill_obj_name).arg(i + 1);
                        }

                        QString skill_line = engine.translate(line_key);
                        if (skill_line.startsWith("$")) {
                            line_key = QString("$%1%2").arg(skill_obj_name).arg(i + 1);
                            skill_line = engine.translate(line_key);
                        }

                        if (!skill_line.startsWith("$")) {
                            items << EasyTextItem(skill_line, audio_file, 1);
                        }
                    }
                } else {
                    bool has_lines = false;
                    for (int i = 1; i <= 10; i++) {
                        QString line_key = QString("$%1%2").arg(skill_obj_name).arg(i);
                        QString skill_line = engine.translate(line_key);

                        if (!skill_line.startsWith("$")) {
                            items << EasyTextItem(skill_line, QString(), 1);
                            has_lines = true;
                        } else {
                            break;
                        }
                    }

                    if (!has_lines) {
                        QString line_key = QString("$%1").arg(skill_obj_name);
                        QString skill_line = engine.translate(line_key);

                        if (!skill_line.startsWith("$")) {
                            items << EasyTextItem(skill_line, QString(), 1);
                        }
                    }
                }
            }

            int skin_index = Config.value("HeroSkin/" + trimmed_name, 0).toInt();
            QString death_audio;
            QString death_line;

            if (skin_index > 0) {
                QString hero_skin = engine.translate(QString("~%1-%2_%3").arg(actualGn).arg(actualGn).arg(skin_index));
                if (!hero_skin.startsWith("~")) {
                    death_audio = QString("hero-skin/%1/%2/death.ogg")
                        .arg(actualGn).arg(skin_index);
                    death_line = hero_skin;
                }
            }

            if (death_line.isEmpty()) {
                death_line = engine.translate("~" + actualGn);
                if (!death_line.startsWith("~") && death_line != " ") {
                    death_audio = QString("audio/death/%1.wav").arg(actualGn);
                }
            }

            if (death_line.startsWith("~") && actualGn.contains("_")) {
                QString new_name = actualGn.split("_").last();
                death_line = engine.translate("~" + new_name);

                if (!death_line.startsWith("~") && death_line != " ") {
                    int new_skin_index = Config.value("HeroSkin/" + new_name, 0).toInt();
                    if (new_skin_index > 0) {
                        QString hero_skin = engine.translate(QString("~%1-%2_%3").arg(new_name).arg(new_name).arg(new_skin_index));
                        if (!hero_skin.startsWith("~")) {
                            death_audio = QString("hero-skin/%1/%2/death.ogg")
                                .arg(new_name).arg(new_skin_index);
                            death_line = hero_skin;
                        }
                    } else {
                        death_audio = QString("audio/death/%1.wav").arg(new_name);
                    }
                }
            }

            if (!death_line.isEmpty() && !death_line.startsWith("~") && death_line != " ") {
                items << EasyTextItem(death_line, death_audio, 2);
            }

            QString win_audio = QString("audio/win/%1.wav").arg(actualGn);
            if (QFile::exists(win_audio)) {
                QString win_line = engine.translate("$" + actualGn);
                if (!win_line.startsWith("$")) {
                    items << EasyTextItem(win_line, win_audio, 3);
                }
            }
        }
    }

    return items;
}
