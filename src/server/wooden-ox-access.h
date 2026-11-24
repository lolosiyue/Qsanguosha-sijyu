#ifndef WOODEN_OX_ACCESS_H
#define WOODEN_OX_ACCESS_H

class Card;
class Room;
class ServerPlayer;

// Recheck storage access on submission because the occupier can change after a prompt.
bool canUseWoodenOxMaterials(const Room &room, const ServerPlayer *player, const Card *card);

#endif
