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

#include "ScriptMgr.h"
#include "ScriptMgrMacros.h"

bool ScriptMgr::OnShadowCheckLFGQueue(lfg::Lfg5Guids const& guidsList)
{
    auto ret = IsValidBoolScript<ShadowScript>([&](ShadowScript* script)
    {
        return !script->OnShadowCheckLFGQueue(guidsList);
    });

    if (ret && *ret)
    {
        return false;
    }

    return true;
}

void ScriptMgr::OnShadowCheckKillTask(Player* player, Unit* victim)
{
    ExecuteScript<ShadowScript>([&](ShadowScript* script)
    {
        script->OnShadowCheckKillTask(player, victim);
    });
}

void ScriptMgr::OnShadowCheckPetitionAccount(Player* player, bool& found)
{
    ExecuteScript<ShadowScript>([&](ShadowScript* script)
    {
        script->OnShadowCheckPetitionAccount(player, found);
    });
}

bool ScriptMgr::OnShadowCheckUpdatesToSend(Player* player)
{
    auto ret = IsValidBoolScript<ShadowScript>([&](ShadowScript* script)
    {
        return !script->OnShadowCheckUpdatesToSend(player);
    });

    if (ret && *ret)
    {
        return false;
    }

    return true;
}

void ScriptMgr::OnShadowPacketSent(Player* player, WorldPacket const* packet)
{
    ExecuteScript<ShadowScript>([&](ShadowScript* script)
    {
        script->OnShadowPacketSent(player, packet);
    });
}

void ScriptMgr::OnShadowUpdate(uint32 diff)
{
    ExecuteScript<ShadowScript>([&](ShadowScript* script)
    {
        script->OnShadowUpdate(diff);
    });
}

void ScriptMgr::OnShadowUpdateSessions(Player* player)
{
    ExecuteScript<ShadowScript>([&](ShadowScript* script)
    {
        script->OnShadowUpdateSessions(player);
    });
}

void ScriptMgr::OnShadowLogout(Player* player)
{
    ExecuteScript<ShadowScript>([&](ShadowScript* script)
    {
        script->OnShadowLogout(player);
    });
}

void ScriptMgr::OnShadowLogoutBots()
{
    ExecuteScript<ShadowScript>([&](ShadowScript* script)
    {
        script->OnShadowLogoutBots();
    });
}
