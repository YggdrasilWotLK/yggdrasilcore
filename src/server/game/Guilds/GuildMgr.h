/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _GUILDMGR_H
#define _GUILDMGR_H

#include "Guild.h"

#include <utility>
#include <vector>

class GuildMgr
{
private:
    GuildMgr();
    ~GuildMgr();

public:
    static GuildMgr* instance();

    Guild* GetGuildByLeader(ObjectGuid guid) const;
    Guild* GetGuildById(uint32 guildId) const;
    Guild* GetGuildByName(std::string_view guildName) const;
    std::string GetGuildNameById(uint32 guildId) const;

    void LoadGuilds();
    void AddGuild(Guild* guild);
    void RemoveGuild(uint32 guildId);
    // Crash-proof: only the manager frees guilds. Destroy unregisters, marks
    // the guild disbanded (zombie: live memory, all ops no-op) and defers the
    // actual delete so stale holders cannot UAF. Never `delete` a Guild outside.
    void DestroyGuild(Guild* guild);

    uint32 GenerateGuildId();
    void SetNextGuildId(uint32 Id) { NextGuildId = Id; }

    void ResetTimes();
protected:
    typedef std::unordered_map<uint32, Guild*> GuildContainer;
    uint32 NextGuildId;
    GuildContainer GuildStore;
    // Zombie graveyard: (guild, timestamp). Swept after a grace period.
    std::vector<std::pair<Guild*, uint32>> _guildGraveyard;
    void SweepGuildGraveyard();
};

#define sGuildMgr GuildMgr::instance()

#endif
