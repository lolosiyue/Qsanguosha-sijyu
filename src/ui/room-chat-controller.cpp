#include "room-chat-controller.h"

#include "audio.h"
#include "settings.h"

#include <QCoreApplication>
#include <QObject>

#include <utility>

RoomChatController::RoomChatController(Callbacks callbacks) : m_callbacks(std::move(callbacks))
{
}

void RoomChatController::appendLocal(const QString &text) const
{
    if (m_callbacks.appendHtml)
        m_callbacks.appendHtml(text);
}

void RoomChatController::submit()
{
    if (!m_callbacks.draftText)
        return;
    if (m_callbacks.chatDisabled && m_callbacks.chatDisabled()) {
            appendLocal(QCoreApplication::translate("RoomScene", "This room does not allow chatting!"));
    } else {
        bool broadcast = true;
        const QString text = m_callbacks.draftText();
        if (text == QStringLiteral(".StartBgMusic")) {
            broadcast = false;
            const QString bgMusicPath = Config.value("BackgroundMusic",
                "audio/system/background.ogg").toString();
#ifdef AUDIO_SUPPORT
            Audio::stopBGM();
            if (Config.BGMVolume > 0) {
                Audio::playBGM(bgMusicPath);
                Audio::setBGMVolume(Config.BGMVolume);
            }
#endif
            if (m_callbacks.backgroundMusicChanged)
                m_callbacks.backgroundMusicChanged(true, bgMusicPath, true);
#ifdef AUDIO_SUPPORT
        } else if (text.startsWith(QStringLiteral(".StartBgMusic="))) {
            broadcast = false;
            QString path = text.mid(14);
            const bool updatePath = path.startsWith(QLatin1Char('|'));
            if (updatePath) {
                path.remove(0, 1);
                Config.setValue("BackgroundMusic", path);
            }
            Audio::stopBGM();
            if (Config.BGMVolume > 0) {
                Audio::playBGM(path);
                Audio::setBGMVolume(Config.BGMVolume);
            }
            if (m_callbacks.backgroundMusicChanged)
                m_callbacks.backgroundMusicChanged(true, path, updatePath);
        } else if (text == QStringLiteral(".StopBgMusic")) {
            broadcast = false;
            Audio::stopBGM();
            if (m_callbacks.backgroundMusicChanged)
                m_callbacks.backgroundMusicChanged(false, QString(), false);
#endif
        }
        if (broadcast) {
            if (m_callbacks.sendToServer)
                m_callbacks.sendToServer(text);
        } else {
            const QString title = m_callbacks.speakerTitle ? m_callbacks.speakerTitle() : QString();
            appendLocal(QCoreApplication::translate("RoomScene", "<font color='%1'>[%2] said: %3 </font>")
                .arg(UiConfig.TextEditColor.name(), title, text));
        }
    }
    if (m_callbacks.clearDraft)
        m_callbacks.clearDraft();
}
