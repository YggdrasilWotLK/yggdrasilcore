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
#include "Map.h"
#include "MoveSplineInit.h"
#include "PathGenerator.h"

namespace
{
    constexpr float HOME_HOP_DIST = 20.0f;
    constexpr float HOME_HOP_STEP = 4.0f;
    constexpr uint8 HOME_MAX_LEGS = 30;
    constexpr uint8 HOME_MAX_STUCK = 5;
    constexpr uint8 HOME_AIR_SLIDE_AFTER = 2;
    constexpr uint32 HOME_RETRY_DELAY = 2000;

    // Same traversability test as MoveSplineInit::IsSegmentBlocked: LoS at
    // chest height + low dynamic-tree ray + walkable slope step.
    bool HomeSegmentBlocked(Creature* owner, G3D::Vector3 const& a, G3D::Vector3 const& b)
    {
        Map* map = owner->GetMap();
        if (!map)
            return true;

        float const heightOffset = std::max(owner->GetCollisionHeight(), 0.5f);
        uint32 const phase = owner->GetPhaseMask();
        if (!map->isInLineOfSight(a.x, a.y, a.z + heightOffset, b.x, b.y, b.z + heightOffset,
                phase, LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing))
            return true;

        float rx, ry, rz;
        if (map->GetObjectHitPos(phase, a.x, a.y, a.z + 0.6f, b.x, b.y, b.z + 0.6f, rx, ry, rz, -0.5f))
            return true;

        float const collisionHeight = owner->GetCollisionHeight();
        bool const swimmable =
            owner->CanSwim() &&
            map->IsInWater(phase, a.x, a.y, a.z, collisionHeight) &&
            map->IsInWater(phase, b.x, b.y, b.z, collisionHeight);
        if (!swimmable && !PathGenerator::IsWalkableClimb(a.x, a.y, a.z, b.x, b.y, b.z, collisionHeight))
            return true;

        return false;
    }

    // Lateral slide around a blocked micro-segment, mirroring
    // MoveSplineInit::TryDetourAroundBlockage radii.
    bool HomeSlideAround(Creature* owner, G3D::Vector3 const& from, G3D::Vector3 const& sample, G3D::Vector3 const& to, G3D::Vector3& out)
    {
        G3D::Vector3 dir(to.x - from.x, to.y - from.y, 0.0f);
        float const len = std::hypot(dir.x, dir.y);
        if (len < 0.01f)
            return false;
        dir.x /= len;
        dir.y /= len;
        G3D::Vector3 const perp(-dir.y, dir.x, 0.0f);

        float const base = std::max(owner->GetObjectSize(), 0.5f);
        float const offsets[4] = { base + 0.5f, base + 1.5f, base + 3.0f, base + 5.0f };

        for (uint8 i = 0; i < 4; ++i)
            for (uint8 side = 0; side < 2; ++side)
            {
                float const sign = (side == 0) ? 1.0f : -1.0f;
                G3D::Vector3 candidate(sample.x + perp.x * sign * offsets[i],
                    sample.y + perp.y * sign * offsets[i],
                    sample.z);
                owner->UpdateAllowedPositionZ(candidate.x, candidate.y, candidate.z);
                if (HomeSegmentBlocked(owner, from, candidate) || HomeSegmentBlocked(owner, candidate, to))
                    continue;
                out = candidate;
                return true;
            }
        return false;
    }
}

void HomeMovementGenerator<Creature>::DoInitialize(Creature* owner)
{
    _repaths = 0;
    _stuckLegs = 0;
    _waitTimer = 0;
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
    _stuckLegs = 0;
    _waitTimer = 0;
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
            // Gap: probe toward home in short grounded micro-steps so the
            // spline tracks the hillside instead of tunneling through it.
            // Each sub-segment is LoS + slope validated; on blockage we keep
            // the valid prefix (slide laterally if stuck at once) and remesh
            // the rest on the next leg.
            float sx = owner->GetPositionX();
            float sy = owner->GetPositionY();
            float sz = owner->GetPositionZ();
            owner->UpdateAllowedPositionZ(sx, sy, sz);
            G3D::Vector3 start(sx, sy, sz);

            float dx = _x - sx;
            float dy = _y - sy;
            float dist2d = std::sqrt(dx * dx + dy * dy);

            Movement::PointsArray hopPath;
            hopPath.push_back(start);

            if (dist2d < 0.01f)
            {
                G3D::Vector3 dest(_x, _y, _z);
                owner->UpdateAllowedPositionZ(dest.x, dest.y, dest.z);
                if (!HomeSegmentBlocked(owner, start, dest))
                    hopPath.push_back(dest);
            }
            else
            {
                float total = std::min(HOME_HOP_DIST, dist2d);
                dx /= dist2d;
                dy /= dist2d;

                G3D::Vector3 prev = start;
                bool blocked = false;
                G3D::Vector3 blockedSample = start;
                G3D::Vector3 blockedTarget = start;

                for (float t = HOME_HOP_STEP; t <= total + 0.001f; t += HOME_HOP_STEP)
                {
                    float step = std::min(t, total);
                    // Lerp Z hint toward home, then snap to terrain.
                    float hintZ = sz + (_z - sz) * (step / total);
                    G3D::Vector3 next(sx + dx * step, sy + dy * step, hintZ);
                    owner->UpdateAllowedPositionZ(next.x, next.y, next.z);

                    if (HomeSegmentBlocked(owner, prev, next))
                    {
                        blocked = true;
                        blockedSample = next;
                        blockedTarget = G3D::Vector3(sx + dx * total, sy + dy * total, hintZ);
                        owner->UpdateAllowedPositionZ(blockedTarget.x, blockedTarget.y, blockedTarget.z);
                        break;
                    }

                    hopPath.push_back(next);
                    prev = next;
                    if (step >= total)
                        break;
                }

                // No forward progress: slide sideways around the wall so the
                // next leg remeshes from a sane foothold instead of zapping.
                if (hopPath.size() < 2 && blocked)
                {
                    G3D::Vector3 slide = start;
                    if (HomeSlideAround(owner, prev, blockedSample, blockedTarget, slide))
                        hopPath.push_back(slide);
                }
            }

            if (hopPath.size() >= 2)
                init.MovebyPath(hopPath);
            else if (_stuckLegs >= HOME_AIR_SLIDE_AFTER)
            {
                // Failed validated hops twice: air-slide toward home in one
                // short unvalidated 4y step, accept tunneling. Remesh next leg.
                // NB: dx/dy above may be normalized already, recompute direction.
                float hx = sx, hy = sy, hz = _z;
                if (dist2d >= 0.01f)
                {
                    float total = std::min(HOME_HOP_STEP, dist2d);
                    hx = sx + (_x - sx) / dist2d * total;
                    hy = sy + (_y - sy) / dist2d * total;
                }
                else
                {
                    hx = _x;
                    hy = _y;
                }
                owner->UpdateAllowedPositionZ(hx, hy, hz);
                init.MoveTo(hx, hy, hz, false, false);
            }
            else
                init.MoveTo(sx, sy, sz, false, false); // stuck: hold, remesh next leg
        }
        else
        {
            init.MoveTo(_x, _y, _z, false, true);
        }
    }

    init.SetWalk(_walk);
    init.Launch();
    arrived = false;

    // Leg-start anchor for progress tracking in DoUpdate.
    _sx = owner->GetPositionX();
    _sy = owner->GetPositionY();
    _sz = owner->GetPositionZ();

    owner->ClearUnitState(uint32(UNIT_STATE_ALL_STATE & ~(UNIT_STATE_POSSESSED | UNIT_STATE_EVADE | UNIT_STATE_IGNORE_PATHFINDING | UNIT_STATE_NO_ENVIRONMENT_UPD)));
}

bool HomeMovementGenerator<Creature>::DoUpdate(Creature* owner, const uint32 time_diff)
{
    if (i_recalculateTravel)
    {
        _repaths = 0;
        _stuckLegs = 0;
        _waitTimer = 0;
        _setTargetLocation(owner);
        i_recalculateTravel = false;
        return true;
    }

    if (!owner->movespline->Finalized())
        return true;

    // Home: done, reactivate there.
    if (owner->GetExactDist(_x, _y, _z) <= 3.0f)
    {
        arrived = true;
        return false;
    }

    // Backing off after failed attempts: wait before the next attempt so a
    // walled-in creature doesn't hot-loop navmesh queries every tick.
    if (_waitTimer > time_diff)
    {
        _waitTimer -= time_diff;
        return true;
    }
    _waitTimer = 0;

    // Zero-progress legs (walled in, hold-in-place) must not burn the leg
    // budget: they finalize instantly and would otherwise spin.
    if (owner->GetExactDist(_sx, _sy, _sz) < 1.0f)
        ++_stuckLegs;
    else
    {
        _stuckLegs = 0;
        ++_repaths;
    }

    if (_repaths < HOME_MAX_LEGS && _stuckLegs < HOME_MAX_STUCK)
    {
        _setTargetLocation(owner);
        return true;
    }

    // Could not walk home yet: back off and keep trying. Never teleport,
    // never reactivate mid-path — evade only ends at home.
    _waitTimer = HOME_RETRY_DELAY;
    _repaths = 0;
    _stuckLegs = 0;
    return true;
}
