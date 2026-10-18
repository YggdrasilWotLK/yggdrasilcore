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

#ifndef SCRIPT_OBJECT_ALL_BATTLEFIELD_SCRIPT_H_
#define SCRIPT_OBJECT_ALL_BATTLEFIELD_SCRIPT_H_

#include "ScriptObject.h"
#include <vector>

enum AllBattlefieldHook
{
    ALLBATTLEFIELDHOOK_ON_BATTLEFIELD_START,
    ALLBATTLEFIELDHOOK_ON_BATTLEFIELD_END,
    ALLBATTLEFIELDHOOK_END
};

class Battlefield;

class AllBattlefieldScript : public ScriptObject
{
protected:
    AllBattlefieldScript(const char* name, std::vector<uint16> enabledHooks = std::vector<uint16>());

public:
    [[nodiscard]] bool IsDatabaseBound() const override { return false; }

    /**
     * @brief This hook runs when a Battlefield battle starts
     *
     * @param bf Contains information about the Battlefield
     */
    virtual void OnBattlefieldStart(Battlefield* /*bf*/) { }

    /**
     * @brief This hook runs when a Battlefield battle ends
     *
     * @param bf Contains information about the Battlefield
     * @param endByTimer True when the battle ended by timer expiry
     */
    virtual void OnBattlefieldEnd(Battlefield* /*bf*/, bool /*endByTimer*/) { }
};

// Compatibility for old scripts
using BattlefieldScript = AllBattlefieldScript;

#endif
