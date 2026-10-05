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

#include "HomeMovementGenerator.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "DisableMgr.h"
#include "MoveSplineInit.h"
#include "PathGenerator.h"

namespace
{
    constexpr float HOME_HOP_DIST = 20.0f;
    constexpr uint8 HOME_MAX_LEGS = 30;
}

void HomeMovementGenerator<Creature>::DoInitialize(Creature* owner)
{
    _repaths = 0;
    _setTargetLocation(owner);
}

void HomeMovementGenerator<Creature>::DoFinalize(Creature* owner)
{
    owner->ClearUnitState(UNIT_STATE_EVADE);
    if (arrived)
    {
        owner->LoadCreaturesAddon(true);
        owner->AI()->JustReachedHome();
    }

    if (!owner->HasSwimmingFlagOutOfCombat())
        owner->RemoveUnitFlag(UNIT_FLAG_SWIMMING);
}

void HomeMovementGenerator<Creature>::DoReset(Creature*)
{
    _repaths = 0;
}

void HomeMovementGenerator<Creature>::_setTargetLocation(Creature* owner)
{
    // Xinef: dont interrupt in any cast!
    //if (owner->HasUnitState(UNIT_STATE_ROOT | UNIT_STATE_STUNNED | UNIT_STATE_DISTRACTED))
    //    return;
    float x, y, z;
    float o = owner->GetHomePosition().GetOrientation();

    // Xinef: if there is motion generator on controlled slot, this one is not updated
    // Xinef: always get reset pos from idle slot
    MovementGenerator* gen = owner->GetMotionMaster()->GetMotionSlot(MOTION_SLOT_IDLE);
    if (owner->GetMotionMaster()->empty() || !gen || !gen->GetResetPosition(x, y, z))
    {
        owner->GetHomePosition(x, y, z, o);
    }

    _x = x;
    _y = y;
    _z = z;
    _o = o;
    owner->UpdateAllowedPositionZ(_x, _y, _z);

    Movement::MoveSplineInit init(owner);
    init.SetFacing(_o);

    // No mmaps: straight run, nothing to fix up.
    if (!sDisableMgr->IsPathfindingEnabled(owner->FindMap()))
    {
        init.MoveTo(_x, _y, _z, false, true);
    }
    else
    {
        // No force: keep valid prefix instead of shortcutting whole route.
        PathGenerator path(owner);
        bool ok = path.CalculatePath(_x, _y, _z, owner->CanFly());
        PathType type = path.GetPathType();
        bool usable = ok && (type & (PATHFIND_NORMAL | PATHFIND_INCOMPLETE))
            && !(type & (PATHFIND_NOPATH | PATHFIND_SHORTCUT | PATHFIND_SHORT | PATHFIND_NOT_USING_PATH))
            && path.GetPath().size() >= 2;

        // Straight swim is fine, straight run is not.
        bool canStraight = owner->CanFly() || (owner->CanSwim() && path.IsWaterPath(path.GetPath()));
        if ((usable || canStraight) && path.GetPath().size() >= 2)
        {
            init.MovebyPath(path.GetPath());
        }
        else if (!canStraight)
        {
            // Gap: short hop toward home, remesh next leg.
            float dx = _x - owner->GetPositionX();
            float dy = _y - owner->GetPositionY();
            float dist2d = std::sqrt(dx * dx + dy * dy);
            float hx, hy, hz = _z;
            if (dist2d < 0.01f)
            {
                hx = _x;
                hy = _y;
            }
            else
            {
                float step = std::min(HOME_HOP_DIST, dist2d);
                hx = owner->GetPositionX() + dx / dist2d * step;
                hy = owner->GetPositionY() + dy / dist2d * step;
            }
            owner->UpdateAllowedPositionZ(hx, hy, hz);
            init.MoveTo(hx, hy, hz, false, false);
        }
        else
        {
            init.MoveTo(_x, _y, _z, false, true);
        }
    }

    init.SetWalk(_walk);
    init.Launch();
    arrived = false;

    owner->ClearUnitState(uint32(UNIT_STATE_ALL_STATE & ~(UNIT_STATE_POSSESSED | UNIT_STATE_EVADE | UNIT_STATE_IGNORE_PATHFINDING | UNIT_STATE_NO_ENVIRONMENT_UPD)));
}

bool HomeMovementGenerator<Creature>::DoUpdate(Creature* owner, const uint32 /*time_diff*/)
{
    if (i_recalculateTravel)
    {
        _repaths = 0;
        _setTargetLocation(owner);
        i_recalculateTravel = false;
        return true;
    }

    if (!owner->movespline->Finalized())
        return true;

    // Leg done but not home: mesh next segment or hop the gap.
    if (owner->GetExactDist(_x, _y, _z) > 3.0f && _repaths < HOME_MAX_LEGS)
    {
        ++_repaths;
        _setTargetLocation(owner);
        return true;
    }

    // Reached, or out of legs: reset where we stand.
    arrived = true;
    return false;
}
