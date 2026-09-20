#ifndef ROOM_CHAT_CONTROLLER_H
#define ROOM_CHAT_CONTROLLER_H

#include <QString>

#include <functional>

class RoomChatController final
{
public:
    struct Callbacks {
        std::function<bool()> chatDisabled;
        std::function<QString()> draftText;
        std::function<void()> clearDraft;
        std::function<void(const QString &)> sendToServer;
        std::function<void(const QString &)> appendHtml;
        std::function<QString()> speakerTitle;
        std::function<void(bool, const QString &, bool)> backgroundMusicChanged;
    };

    explicit RoomChatController(Callbacks callbacks);
    void submit();

private:
    void appendLocal(const QString &text) const;
    Callbacks m_callbacks;
};

#endif
