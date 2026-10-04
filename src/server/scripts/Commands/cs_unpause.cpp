/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Chat.h"
#include "CommandScript.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "PlayerScript.h"

using namespace Acore::ChatCommands;

// Released by the UnpauseOnInteract client addon when an interaction
// window closes: ".unpause <creature guid>". Only removes the stop aura
// if it was placed by the requesting player.
class unpause_commandscript : public CommandScript
{
public:
    unpause_commandscript() : CommandScript("unpause_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commandTable =
        {
            { "unpause", HandleUnpauseCommand, SEC_PLAYER, Console::No },
        };
        return commandTable;
    }

    static bool HandleUnpauseCommand(ChatHandler* handler, Tail guidStr)
    {
        Player* player = handler->GetSession()->GetPlayer();
        if (!player || guidStr.empty())
            return false;

        uint64 raw = 0;
        try
        {
            raw = std::stoull(std::string(guidStr), nullptr, 0);
        }
        catch (...)
        {
            return false;
        }

        ObjectGuid guid(raw);
        if (!guid.IsCreature() || !player->GetMap())
            return true;

        if (Creature* creature = player->GetMap()->GetCreature(guid))
            creature->ClearInteractionStop(player);

        return true;
    }
};

// Failover: auras placed by a player are released when they log out,
// otherwise the NPC would stay held until the aura expires.
class unpause_playerscript : public PlayerScript
{
public:
    unpause_playerscript() : PlayerScript("unpause_playerscript",
        {
            PLAYERHOOK_ON_LOGOUT,
            PLAYERHOOK_ON_BEFORE_TELEPORT
        })
    {
    }

    void OnPlayerLogout(Player* player) override
    {
        Creature::ClearInteractionStopsBy(player);
    }

    bool OnPlayerBeforeTeleport(Player* player, uint32 /*mapid*/, float /*x*/, float /*y*/, float /*z*/, float /*orientation*/, uint32 /*options*/, Unit* /*target*/) override
    {
        Creature::ClearInteractionStopsBy(player);
        return true;
    }
};

void AddSC_unpause_commandscript()
{
    new unpause_commandscript();
    new unpause_playerscript();
}
