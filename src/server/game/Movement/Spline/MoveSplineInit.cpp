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

#include "MoveSplineInit.h"
#include "MoveSpline.h"
#include "MovementPacketBuilder.h"
#include "Creature.h"
#include "Map.h"
#include "Opcodes.h"
#include "Transport.h"
#include "Unit.h"
#include "Vehicle.h"
#include "WorldPacket.h"
#include "Log.h"

namespace Movement
{
namespace
{
    // Tuning for the CatmullRom departure smoothing below. The client evaluates CatmullRom
    // with uniform knot parameterization, so tight knots (short segments combined with large
    // direction changes) produce orientation flips and speed wobble ("jitter"). The rules here
    // keep exactly ONE gentle curvature lobe per spline; anything sharper is split into two
    // sequential splines instead of being forced into a single tight arc.
    constexpr float SMOOTH_MIN_TURN    = float(M_PI) / 12.0f;       // 15 deg: smaller turns need no help
    constexpr float SMOOTH_REF_TURN     = float(M_PI) * 2.0f / 3.0f; // 120 deg reference for scaling the blend distance
    constexpr float SMOOTH_MIN_DIST    = 2.0f;                      // shorter moves have no room for any arc
    constexpr float SMOOTH_MIN_SEGMENT = 1.0f;                      // tighter knots jitter client-side
    constexpr uint32 SMOOTH_MAX_SAMPLE_SEGMENTS = 16;               // curve validation budget per MoveTo
    constexpr float SMOOTH_MAX_BLEND_T = 0.45f;                     // blend must stay clear of the target

    // CreatureMovementInfo.dbc `field1` decoded as float (SmoothFacingChaseRate, rad/s).
    // 0 = snap (no slew). Server copy of your DBC export: only non-zero rows listed.
    float SlewRateForMovementId(uint32 movementId)
    {
        switch (movementId)
        {
            case 1:   return 2.0f;
            case 321: return 4.0f;
            case 341: return 5.0f;
            case 361: return 0.0f;
            case 381: return 6.0f;
            case 401: return 5.0f;
            case 441: return 10.0f;
            case 541: return 10.0f;
            case 661: return 5.0f;
            case 681: return 5.0f;
            case 721: return 10.0f;
            case 741: return 10.0f;
            case 781: return 1.0f;
            case 801: return 0.25f;
            default:  return 0.0f;
        }
    }

    float GetFacingSlewRate(Unit const* unit)
    {
        if (Creature const* creature = unit->ToCreature())
            if (CreatureTemplate const* proto = creature->GetCreatureTemplate())
                return SlewRateForMovementId(proto->movementId);
        return 10.0f; // non-creature movers (pets/vehicles): assume fast slew
    }

    float NormalizeAngle(float angle)
    {
        while (angle >  M_PI) angle -= 2.0f * float(M_PI);
        while (angle < -M_PI) angle += 2.0f * float(M_PI);
        return angle;
    }

    float NormalizeOrientation(float orient)
    {
        while (orient < 0.0f) orient += 2.0f * float(M_PI);
        while (orient >= 2.0f * float(M_PI)) orient -= 2.0f * float(M_PI);
        return orient;
    }

    bool CanSmooth(Unit const* unit, MoveSplineInitArgs const& args, bool transport)
    {
        if (transport || unit->IsPlayer())
            return false;
        if (args.flags.parabolic || args.flags.falling || args.flags.animation || args.flags.cyclic || args.flags.flying)
            return false;
        if (args.flags.orientationInversed)
            return false;
        return args.path.size() >= 2;
    }

    // Ground movers get terrain/WMO/gameobject-floor aware Z. Flyers keep authoritative Z.
    bool IsGroundMode(Unit const* unit)
    {
        return !unit->CanFly();
    }

    float SnapControlPointZ(Unit* unit, float x, float y, float hintZ)
    {
        float z = hintZ;
        unit->UpdateAllowedPositionZ(x, y, z);
        return z;
    }

    // True when the straight segment a->b cannot be traversed: blocked line of sight
    // (terrain, WMO, gameobjects) or an unclimbable slope step for a ground mover.
    bool IsSegmentBlocked(Unit const* unit, Vector3 const& a, Vector3 const& b, bool groundMode)
    {
        Map* map = unit->GetMap();
        if (!map)
            return true;

        float const heightOffset = std::max(unit->GetCollisionHeight(), 0.5f);
        uint32 const phase = unit->GetPhaseMask();
        if (!map->isInLineOfSight(a.x, a.y, a.z + heightOffset, b.x, b.y, b.z + heightOffset,
                phase, LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing))
            return true;

        if (groundMode)
        {
            float const collisionHeight = unit->GetCollisionHeight();
            bool const swimmable =
                unit->CanSwim() &&
                map->IsInWater(phase, a.x, a.y, a.z, collisionHeight) &&
                map->IsInWater(phase, b.x, b.y, b.z, collisionHeight);
            if (!swimmable && !PathGenerator::IsWalkableClimb(a.x, a.y, a.z, b.x, b.y, b.z, collisionHeight))
                return true;
        }
        return false;
    }

    // Uniform CatmullRom evaluation, matching SplineBase::EvaluateCatmullRom in Spline.cpp.
    void EvaluateCatmullRomSegment(Vector3 const& p0, Vector3 const& p1, Vector3 const& p2, Vector3 const& p3, float t, Vector3& out)
    {
        float const t2 = t * t;
        float const t3 = t2 * t;
        out.x = 0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 + (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3);
        out.y = 0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 + (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3);
        out.z = 0.5f * ((2.0f * p1.z) + (-p0.z + p2.z) * t + (2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z) * t2 + (-p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z) * t3);
    }

    // Samples the curve the client will actually render (midpoints of each segment) and
    // rejects it when it deviates from traversable space: through walls (LoS), under the
    // floor, or floating high above it. The ghost points mirror SplineBase::InitCatmullRom
    // (extrapolated start = controls[0] toward controls[1] mirrored, duplicated end) so the
    // samples match the client curve exactly; server and client must never disagree here.
    bool IsSmoothedCurveValid(Unit const* unit, PointsArray const& controls, bool groundMode)
    {
        if (controls.size() < 3)
            return true;

        uint32 const segments = std::min<uint32>(uint32(controls.size() - 1), SMOOTH_MAX_SAMPLE_SEGMENTS);
        Vector3 prev = controls[0];

        for (uint32 k = 0; k < segments; ++k)
        {
            Vector3 const& p1 = controls[k];
            Vector3 const& p2 = controls[k + 1];

            Vector3 startGhost = p1 + (p1 - p2);
            Vector3 const& p0 = (k == 0) ? startGhost : controls[k - 1];
            std::size_t const p3Idx = (std::size_t)k + 2;
            Vector3 const& p3 = (p3Idx < controls.size()) ? controls[p3Idx] : controls.back();

            Vector3 mid;
            EvaluateCatmullRomSegment(p0, p1, p2, p3, 0.5f, mid);

            if (IsSegmentBlocked(unit, prev, mid, groundMode) || IsSegmentBlocked(unit, mid, p2, groundMode))
                return false;

            if (groundMode)
            {
                Map* map = unit->GetMap();
                bool const inWater =
                    unit->CanSwim() &&
                    map->IsInWater(unit->GetPhaseMask(), mid.x, mid.y, mid.z, unit->GetCollisionHeight());
                if (!inWater)
                {
                    float const ground = unit->GetMapHeight(mid.x, mid.y, mid.z);
                    if (ground > INVALID_HEIGHT)
                    {
                        float const minZ = ground - 1.0f;
                        float const maxZ = ground + unit->GetHoverHeight() + 4.0f;
                        if (mid.z < minZ || mid.z > maxZ)
                            return false;
                    }
                }
            }
            prev = p2;
        }
        return true;
    }

    // Builds a single departure blend point on the angle bisector between the current facing
    // and the bearing to the reference target. One interior point bends the same way all along
    // (a single curvature lobe), splitting the total turn into two roughly equal halves, each
    // of which CatmullRom renders smoothly. Returns false when there is no room for an arc.
    bool BuildDeparturePoint(Unit* unit, Vector3 const& src, float orient, Vector3 const& refTarget, float distRef, float angleDelta, bool groundMode, float tMin, Vector3& out)
    {
        float const absTurn = std::fabs(angleDelta);
        float t = 0.25f + 0.15f * (absTurn / SMOOTH_REF_TURN); // 0.25 .. 0.40, wider turns blend further out
        if (t < tMin)
            t = tMin;
        if (t > SMOOTH_MAX_BLEND_T)
            t = SMOOTH_MAX_BLEND_T;

        for (uint8 attempt = 0; attempt < 3; ++attempt)
        {
            float const bisector = orient + angleDelta * 0.5f;
            float const reach = distRef * t;
            Vector3 candidate(
                src.x + reach * std::cos(bisector),
                src.y + reach * std::sin(bisector),
                src.z + (refTarget.z - src.z) * t);

            if (groundMode)
                candidate.z = SnapControlPointZ(unit, candidate.x, candidate.y, candidate.z);

            float const legA = std::hypot(candidate.x - src.x, candidate.y - src.y);
            float const legB = std::hypot(refTarget.x - candidate.x, refTarget.y - candidate.y);
            if (legA >= SMOOTH_MIN_SEGMENT && legB >= SMOOTH_MIN_SEGMENT &&
                !IsSegmentBlocked(unit, src, candidate, groundMode) &&
                !IsSegmentBlocked(unit, candidate, refTarget, groundMode))
            {
                out = candidate;
                return true;
            }
            t *= 0.5f;
        }

        // No off-axis placement is traversable: fall back to a collinear subdivision point at
        // the remaining fraction. It adds no curvature (zero deviation from the requested path)
        // but keeps the spline knot spacing even.
        Vector3 collinear(
            src.x + (refTarget.x - src.x) * t,
            src.y + (refTarget.y - src.y) * t,
            src.z + (refTarget.z - src.z) * t);
        if (groundMode)
            collinear.z = SnapControlPointZ(unit, collinear.x, collinear.y, collinear.z);

        float const legA = std::hypot(collinear.x - src.x, collinear.y - src.y);
        float const legB = std::hypot(refTarget.x - collinear.x, refTarget.y - collinear.y);
        if (legA >= SMOOTH_MIN_SEGMENT && legB >= SMOOTH_MIN_SEGMENT &&
            !IsSegmentBlocked(unit, src, collinear, groundMode) &&
            !IsSegmentBlocked(unit, collinear, refTarget, groundMode))
        {
            out = collinear;
            return true;
        }
        return false;
    }

    // Inserts ground-snapped midpoints on long, steep spans so the spline tracks hillsides
    // instead of tunneling through them or floating above them. Inserted points lie on the
    // requested segments, so they add no curvature of their own.
    void DensifySteepSpans(Unit* unit, PointsArray& path)
    {
        if (path.size() < 2 || path.size() > 7)
            return;

        for (std::size_t i = 0; i + 1 < path.size(); ++i)
        {
            Vector3 const& a = path[i];
            Vector3 const& b = path[i + 1];
            float const flatDist = std::hypot(b.x - a.x, b.y - a.y);
            if (flatDist < 8.0f)
                continue;

            float const groundA = unit->GetMapHeight(a.x, a.y, a.z);
            float const groundB = unit->GetMapHeight(b.x, b.y, b.z);
            if (groundA <= INVALID_HEIGHT || groundB <= INVALID_HEIGHT || std::fabs(groundB - groundA) < 1.5f)
                continue;

            Vector3 mid((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f);
            mid.z = SnapControlPointZ(unit, mid.x, mid.y, mid.z);
            if (IsSegmentBlocked(unit, a, mid, true) || IsSegmentBlocked(unit, mid, b, true))
                continue;

            path.insert(path.begin() + i + 1, mid);
            ++i; // skip over the span just subdivided
            if (path.size() > 15)
                return;
        }
    }

    // Rewrites args.path for CatmullRom rendering:
    //  - snaps control points to the walkable surface (terrain/WMO/floors + hover offset),
    //  - inserts exactly one departure blend point (a single arc, never chained arcs),
    //  - validates the rendered curve against LoS/height and falls back to linear on failure.
    // Deliberately one spline per MoveTo: chaining a second leg on a timer desyncs from the
    // client's render position and snaps at the pivot.
    void SmoothPathForCatmullRom(Unit* unit, MoveSplineInitArgs& args, Location const& realPos)
    {
        if (!unit->GetMap()) // not in world: leave the requested path untouched
            return;
        Vector3 const src(realPos.x, realPos.y, realPos.z);
        bool const groundMode = IsGroundMode(unit);

        if (groundMode)
            for (Vector3& point : args.path)
                point.z = SnapControlPointZ(unit, point.x, point.y, point.z);

        float const orient = NormalizeOrientation(realPos.orientation);
        Vector3 const firstLeg = args.path[1] - src;
        float const distToNext = firstLeg.length();
        float const distToFinal = (args.path.back() - src).length();
        // A blend needs room on both the departure step and the overall move; without it the
        // departure bearing is noise and any inserted knot would only jitter.
        bool const canBlend = distToNext >= SMOOTH_MIN_DIST && distToFinal >= SMOOTH_MIN_DIST;

        float angleDelta = 0.0f;
        if (canBlend)
        {
            float const bearingToNext = std::atan2(firstLeg.y, firstLeg.x);
            angleDelta = NormalizeAngle(bearingToNext - orient);
        }
        if (!canBlend || std::fabs(angleDelta) <= SMOOTH_MIN_TURN)
        {
            if (groundMode)
                DensifySteepSpans(unit, args.path);
            // Even a blend-free mmap polygon can poke through geometry once CatmullRom rounds
            // its corners, so validate the rendered curve before committing to smoothing.
            if (args.path.size() > 2 && !IsSmoothedCurveValid(unit, args.path, groundMode))
                return; // keep snapped points, but move linearly
            args.flags.EnableCatmullRom();
            return;
        }

        // Per-creature facing slew cap: the client's tangent sweep must not outrun the
        // DBC SmoothFacingChaseRate, or the model snaps to default and back. Rate 0
        // means snap: no curve at all, move linearly and let facing snap at corners.
        float const slewRate = GetFacingSlewRate(unit);
        if (slewRate <= 0.01f)
        {
            if (groundMode)
                DensifySteepSpans(unit, args.path);
            return; // linear spline, no CatmullRom
        }

        // Widen the arc so the lobe sweep (absTurn/2 traversed in reach/velocity)
        // stays within the slew budget. If it cannot fit, go linear instead.
        float const velocity = std::max(unit->GetSpeed(MOVE_RUN), 0.5f);
        float const tNeeded = (std::fabs(angleDelta) * velocity) / (2.0f * slewRate * distToNext);
        if (tNeeded > SMOOTH_MAX_BLEND_T)
        {
            if (groundMode)
                DensifySteepSpans(unit, args.path);
            LOG_DEBUG("movement", "SmoothPathForCatmullRom: turn exceeds slew rate, falling back to linear");
            return;
        }

        // Single departure arc toward the next target. Sharp reversals stay in one spline:
        // the client's facing slew (CreatureMovementInfo rate) absorbs the visual
        // turn, while the position curve keeps a single gentle lobe instead of a tight knot.
        // Track the inserted point by value: DensifySteepSpans below may shift indices.
        bool hasBlend = false;
        Vector3 blendValue = Vector3::zero();
        Vector3 const refTarget = args.path[1];
        float const distRef = (refTarget - src).length();
        if (distRef >= SMOOTH_MIN_DIST)
        {
            float const refBearing = std::atan2((refTarget.y - src.y), (refTarget.x - src.x));
            float const refTurn = NormalizeAngle(refBearing - orient);
            if (std::fabs(refTurn) > SMOOTH_MIN_TURN)
            {
                Vector3 blend;
                if (BuildDeparturePoint(unit, src, orient, refTarget, distRef, refTurn, groundMode, tNeeded, blend))
                {
                    args.path.insert(args.path.begin() + 1, blend);
                    hasBlend = true;
                    blendValue = blend;
                }
            }
        }

        if (groundMode)
            DensifySteepSpans(unit, args.path);

        // The rendered curve must stay inside traversable space; otherwise drop the blend
        // point first, and if the path itself still misbehaves under CatmullRom, move linearly.
        if (!IsSmoothedCurveValid(unit, args.path, groundMode))
        {
            if (hasBlend)
            {
                for (auto itr = args.path.begin() + 1; itr != args.path.end(); ++itr)
                    if (*itr == blendValue)
                    {
                        args.path.erase(itr);
                        break;
                    }
                hasBlend = false;
                LOG_DEBUG("movement", "SmoothPathForCatmullRom: blend point rejected by LoS/terrain, retrying without it");
            }
            if (args.path.size() > 2 && !IsSmoothedCurveValid(unit, args.path, groundMode))
            {
                LOG_DEBUG("movement", "SmoothPathForCatmullRom: curve still invalid, falling back to linear spline");
                return; // keep snapped points, but do not enable CatmullRom
            }
        }

        args.flags.EnableCatmullRom();
    }
} // anonymous namespace (still inside namespace Movement)
    UnitMoveType SelectSpeedType(uint32 moveFlags)
    {
        if (moveFlags & MOVEMENTFLAG_FLYING)
        {
            if (moveFlags & MOVEMENTFLAG_BACKWARD /*&& speed_obj.flight >= speed_obj.flight_back*/)
                return MOVE_FLIGHT_BACK;
            else
                return MOVE_FLIGHT;
        }
        else if (moveFlags & MOVEMENTFLAG_SWIMMING)
        {
            if (moveFlags & MOVEMENTFLAG_BACKWARD /*&& speed_obj.swim >= speed_obj.swim_back*/)
                return MOVE_SWIM_BACK;
            else
                return MOVE_SWIM;
        }
        else if (moveFlags & MOVEMENTFLAG_WALKING)
        {
            //if (speed_obj.run > speed_obj.walk)
            return MOVE_WALK;
        }
        else if (moveFlags & MOVEMENTFLAG_BACKWARD /*&& speed_obj.run >= speed_obj.run_back*/)
            return MOVE_RUN_BACK;

        // Flying creatures use MOVEMENTFLAG_CAN_FLY or MOVEMENTFLAG_DISABLE_GRAVITY
        // Run speed is their default flight speed.
        return MOVE_RUN;
    }

    int32 MoveSplineInit::Launch()
    {
        MoveSpline& move_spline = *unit->movespline;

        bool transport = unit->HasUnitMovementFlag(MOVEMENTFLAG_ONTRANSPORT) && unit->GetTransGUID();
        Location real_position;
        // there is a big chance that current position is unknown if current state is not finalized, need compute it
        // this also allows CalculatePath spline position and update map position in much greater intervals
        // Don't compute for transport movement if the unit is in a motion between two transports
        if (!move_spline.Finalized() && move_spline.onTransport == transport)
            real_position = move_spline.ComputePosition();
        else
        {
            Position const* pos;
            if (!transport)
                pos = unit;
            else
                pos = &unit->m_movementInfo.transport.pos;

            real_position.x = pos->GetPositionX();
            real_position.y = pos->GetPositionY();
            real_position.z = pos->GetPositionZ();
            real_position.orientation = unit->GetOrientation();
        }

        // should i do the things that user should do? - no.
        if (args.path.empty())
            return 0;

        // corrent first vertex
        args.path[0] = real_position;
        args.initialOrientation = real_position.orientation;
        move_spline.onTransport = transport;

        // Every ground move renders as a client-side spline: blend the departure toward the
        // current facing with a single validated arc and snap Z to terrain/WMO/floors.
        if (CanSmooth(unit, args, transport))
            SmoothPathForCatmullRom(unit, args, real_position);

        uint32 moveFlags = unit->m_movementInfo.GetMovementFlags();
        moveFlags |= MOVEMENTFLAG_SPLINE_ENABLED;

        if (!args.flags.orientationInversed)
        {
            moveFlags = (moveFlags & ~(MOVEMENTFLAG_BACKWARD)) | MOVEMENTFLAG_FORWARD;
        }
        else
        {
            moveFlags = (moveFlags & ~(MOVEMENTFLAG_FORWARD)) | MOVEMENTFLAG_BACKWARD;
        }

        bool isOrientationOnly = args.path.size() == 2 && args.path[0] == args.path[1];

        if (moveFlags & MOVEMENTFLAG_ROOT) // This case should essentially never occur - hence the trace logging - hints to issues elsewhere
        {
            LOG_TRACE("movement", "Invalid movement during root. Entry: {} IsImmobilized {}, moveflags {}", unit->GetEntry(), unit->IsImmobilizedState() ? "true" : "false", moveFlags);
            moveFlags &= ~MOVEMENTFLAG_MASK_MOVING;
        }

        if (isOrientationOnly)
            moveFlags &= ~MOVEMENTFLAG_MASK_MOVING;

        if (!args.HasVelocity)
        {
            // If spline is initialized with SetWalk method it only means we need to select
            // walk move speed for it but not add walk flag to unit
            uint32 moveFlagsForSpeed = moveFlags;
            if (args.flags.walkmode)
                moveFlagsForSpeed |= MOVEMENTFLAG_WALKING;
            else
                moveFlagsForSpeed &= ~MOVEMENTFLAG_WALKING;

            args.velocity = unit->GetSpeed(SelectSpeedType(moveFlagsForSpeed));
        }

        // limit the speed in the same way the client does
        args.velocity = std::min(args.velocity, args.flags.catmullrom || args.flags.flying ? 50.0f : std::max(28.0f, unit->GetSpeed(MOVE_RUN) * 4.0f));

        if (!args.Validate(unit))
            return 0;

        unit->m_movementInfo.SetMovementFlags(moveFlags);
        move_spline.Initialize(args);

        WorldPacket data(SMSG_MONSTER_MOVE, 64);
        data << unit->GetPackGUID();
        if (transport)
        {
            data.SetOpcode(SMSG_MONSTER_MOVE_TRANSPORT);
            data << unit->GetTransGUID().WriteAsPacked();
            data << int8(unit->GetTransSeat());
        }

        PacketBuilder::WriteMonsterMove(move_spline, data);
        unit->SendMessageToSet(&data, true);

        return move_spline.Duration();
    }

    void MoveSplineInit::Stop()
    {
        MoveSpline& move_spline = *unit->movespline;

        // No need to stop if we are not moving
        if (move_spline.Finalized())
            return;

        bool transport = unit->HasUnitMovementFlag(MOVEMENTFLAG_ONTRANSPORT) && unit->GetTransGUID();
        Location loc;
        if (move_spline.onTransport == transport)
            loc = move_spline.ComputePosition();
        else
        {
            Position const* pos;
            if (!transport)
                pos = unit;
            else
                pos = &unit->m_movementInfo.transport.pos;

            loc.x = pos->GetPositionX();
            loc.y = pos->GetPositionY();
            loc.z = pos->GetPositionZ();
            loc.orientation = unit->GetOrientation();
        }

        args.flags = MoveSplineFlag::Done;
        unit->m_movementInfo.RemoveMovementFlag(MOVEMENTFLAG_FORWARD | MOVEMENTFLAG_BACKWARD | MOVEMENTFLAG_SPLINE_ENABLED);
        move_spline.onTransport = transport;
        move_spline.Initialize(args);

        WorldPacket data(SMSG_MONSTER_MOVE, 64);
        data << unit->GetPackGUID();
        if (transport)
        {
            data.SetOpcode(SMSG_MONSTER_MOVE_TRANSPORT);
            data << unit->GetTransGUID().WriteAsPacked();
            data << int8(unit->GetTransSeat());
        }

        PacketBuilder::WriteStopMovement(loc, args.splineId, data);
        unit->SendMessageToSet(&data, true);
    }

    MoveSplineInit::MoveSplineInit(Unit* m) : unit(m)
    {
        args.splineId = splineIdGen.NewId();
        args.TransformForTransport = unit->HasUnitMovementFlag(MOVEMENTFLAG_ONTRANSPORT) && unit->GetTransGUID();
        // mix existing state into new
        args.flags.walkmode = unit->m_movementInfo.HasMovementFlag(MOVEMENTFLAG_WALKING);
        args.flags.flying = unit->m_movementInfo.HasMovementFlag((MovementFlags)(MOVEMENTFLAG_CAN_FLY | MOVEMENTFLAG_DISABLE_GRAVITY));
    }

    void MoveSplineInit::SetFacing(Unit const* target)
    {
        args.flags.EnableFacingTarget();
        args.facing.target = target->GetGUID().GetRawValue();
    }

    void MoveSplineInit::SetFacing(float angle)
    {
        if (args.TransformForTransport)
        {
            if (Unit* vehicle = unit->GetVehicleBase())
                angle -= vehicle->GetOrientation();
            else if (Transport* transport = unit->GetTransport())
                angle -= transport->GetOrientation();
        }

        args.facing.angle = G3D::wrap(angle, 0.f, (float)G3D::twoPi());
        args.flags.EnableFacingAngle();
    }

    void MoveSplineInit::MoveTo(const Vector3& dest, bool generatePath, bool forceDestination)
    {
        if (generatePath)
        {
            PathGenerator path(unit);
            bool result = path.CalculatePath(dest.x, dest.y, dest.z, forceDestination);
            if (result && !(path.GetPathType() & PATHFIND_NOPATH))
            {
                MovebyPath(path.GetPath());
                return;
            }
        }

        args.path_Idx_offset = 0;
        args.path.resize(2);
        TransportPathTransform transform(unit, args.TransformForTransport);
        args.path[1] = transform(dest);
    }

    Vector3 TransportPathTransform::operator()(Vector3 input)
    {
        if (_transformForTransport)
            if (TransportBase* transport = _owner->GetDirectTransport())
                transport->CalculatePassengerOffset(input.x, input.y, input.z);

        return input;
    }
}
