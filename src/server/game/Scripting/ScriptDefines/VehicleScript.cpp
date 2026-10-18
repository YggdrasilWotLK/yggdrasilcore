/*
 * This file is part of the YggdrasilCore Project. See AUTHORS file for Copyright information
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

#include "VehicleScript.h"
#include "ScriptMgr.h"
#include "ScriptMgrMacros.h"
#include "Vehicle.h"

void ScriptMgr::OnInstall(Vehicle* veh)
{
    ASSERT(veh);
    ASSERT(veh->GetBase()->IsCreature());

    ExecuteScript<VehicleScript>([&](VehicleScript* script)
    {
        script->OnInstall(veh);
    });
}

void ScriptMgr::OnUninstall(Vehicle* veh)
{
    ASSERT(veh);
    ASSERT(veh->GetBase()->IsCreature());

    ExecuteScript<VehicleScript>([&](VehicleScript* script)
    {
        script->OnUninstall(veh);
    });
}

void ScriptMgr::OnReset(Vehicle* veh)
{
    ASSERT(veh);
    ASSERT(veh->GetBase()->IsCreature());

    ExecuteScript<VehicleScript>([&](VehicleScript* script)
    {
        script->OnReset(veh);
    });
}

void ScriptMgr::OnInstallAccessory(Vehicle* veh, Creature* accessory)
{
    ASSERT(veh);
    ASSERT(veh->GetBase()->IsCreature());
    ASSERT(accessory);

    ExecuteScript<VehicleScript>([&](VehicleScript* script)
    {
        script->OnInstallAccessory(veh, accessory);
    });
}

void ScriptMgr::OnAddPassenger(Vehicle* veh, Unit* passenger, int8 seatId)
{
    ASSERT(veh);
    ASSERT(veh->GetBase()->IsCreature());
    ASSERT(passenger);

    ExecuteScript<VehicleScript>([&](VehicleScript* script)
    {
        script->OnAddPassenger(veh, passenger, seatId);
    });
}

void ScriptMgr::OnRemovePassenger(Vehicle* veh, Unit* passenger)
{
    ASSERT(veh);
    ASSERT(veh->GetBase()->IsCreature());
    ASSERT(passenger);

    ExecuteScript<VehicleScript>([&](VehicleScript* script)
    {
        script->OnRemovePassenger(veh, passenger);
    });
}

VehicleScript::VehicleScript(const char* name)
    : ScriptObject(name)
{
    ScriptRegistry<VehicleScript>::AddScript(this);
}

template class YC_GAME_API ScriptRegistry<VehicleScript>;
