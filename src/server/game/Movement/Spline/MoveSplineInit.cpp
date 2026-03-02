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
#include "Opcodes.h"
#include "Transport.h"
#include "Unit.h"
#include "Vehicle.h"
#include "WorldPacket.h"
#include "Log.h"

namespace Movement
{
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

        if (!unit->IsPlayer() && !args.flags.parabolic && !args.flags.falling &&
            !args.flags.animation && !args.flags.cyclic && !args.flags.flying &&
            args.path.size() >= 2)
        {
            Vector3 const src(real_position.x, real_position.y, real_position.z);
            Vector3 const toNext = args.path[1] - src;
            float const distToNext = toNext.length();
            float const distToFinal = (args.path.back() - src).length();
            float orient = real_position.orientation;
            while (orient < 0.0f) orient += 2.0f * M_PI;
            while (orient >= 2.0f * M_PI) orient -= 2.0f * M_PI;
            float const bearingToNext = std::atan2(toNext.y, toNext.x);
            float angleDelta = bearingToNext - orient;
            while (angleDelta >  M_PI) angleDelta -= 2.0f * M_PI;
            while (angleDelta < -M_PI) angleDelta += 2.0f * M_PI;
            if (std::fabs(angleDelta) > M_PI / 12.0f && distToNext >= 2.0f && distToFinal >= 2.0f)
            {
                float const reach = std::max(distToFinal * 0.25f, 1.5f);
                float const cross = std::cos(orient) * toNext.y - std::sin(orient) * toNext.x;
                float const side = (cross >= 0.0f) ? 1.0f : -1.0f;
                float const perpAngle = orient + side * M_PI_2;
                Vector3 const dst = args.path.back();
                if (std::fabs(angleDelta) > M_PI * 2.0f / 3.0f)
                {
                    // large turn: sweep multiple waypoints from orient toward target bearing at larger reach
                    int32 const numPoints = static_cast<int32>(std::ceil(std::fabs(angleDelta) / (M_PI / 3.0f)));
                    float const largeReach = distToFinal * 0.5f;
                    for (int32 i = numPoints - 1; i >= 0; --i)
                    {
                        float const angle = orient + angleDelta * static_cast<float>(i) / static_cast<float>(numPoints);
                        args.path.insert(args.path.begin() + 1, Vector3(
                            src.x + largeReach * std::cos(angle),
                            src.y + largeReach * std::sin(angle),
                            src.z));
                    }
                    if (unit->GetMapId() == 13)
                        LOG_ERROR("movement", "ARC LARGE src=({:.2f},{:.2f}) orient={:.2f} bearingToNext={:.2f} angleDelta={:.2f} largeReach={:.2f} numPoints={} dst=({:.2f},{:.2f}) pathSize={}",
                            src.x, src.y,
                            orient * 180.0f / M_PI, bearingToNext * 180.0f / M_PI, angleDelta * 180.0f / M_PI,
                            largeReach, numPoints, dst.x, dst.y, args.path.size());
                }
                else
                {
                    // normal turn: circle arc
                    float const centerX = src.x + reach * std::cos(perpAngle);
                    float const centerY = src.y + reach * std::sin(perpAngle);
                    float const startAngle = std::atan2(src.y - centerY, src.x - centerX);
                    float const endAngle = startAngle + side * std::fabs(angleDelta);
                    float const p1x = src.x + reach * std::cos(orient);
                    float const p1y = src.y + reach * std::sin(orient);
                    float const circleEndX = centerX + reach * std::cos(endAngle);
                    float const circleEndY = centerY + reach * std::sin(endAngle);
                    float const circleEndToDst = std::sqrt((dst.x - circleEndX) * (dst.x - circleEndX) + (dst.y - circleEndY) * (dst.y - circleEndY));
                    float p2x, p2y;
                    if (circleEndToDst > distToFinal)
                    {
                        p2x = p1x + 0.33f * (dst.x - p1x);
                        p2y = p1y + 0.33f * (dst.y - p1y);
                    }
                    else if (circleEndToDst < distToFinal * 0.2f)
                    {
                        p2x = circleEndX * 0.3f + dst.x * 0.7f;
                        p2y = circleEndY * 0.3f + dst.y * 0.7f;
                    }
                    else
                    {
                        p2x = circleEndX;
                        p2y = circleEndY;
                    }
                    float const p1ToP2 = std::sqrt((p2x - p1x) * (p2x - p1x) + (p2y - p1y) * (p2y - p1y));
                    if (p1ToP2 < 1.0f)
                    {
                        float const midX = (src.x + dst.x) * 0.5f;
                        float const midY = (src.y + dst.y) * 0.5f;
                        float const candAx = midX + reach * std::cos(orient + M_PI_2);
                        float const candAy = midY + reach * std::sin(orient + M_PI_2);
                        float const candBx = midX + reach * std::cos(orient - M_PI_2);
                        float const candBy = midY + reach * std::sin(orient - M_PI_2);
                        float const distA = std::sqrt((candAx - dst.x) * (candAx - dst.x) + (candAy - dst.y) * (candAy - dst.y));
                        float const distB = std::sqrt((candBx - dst.x) * (candBx - dst.x) + (candBy - dst.y) * (candBy - dst.y));
                        p2x = (distA < distB) ? candAx : candBx;
                        p2y = (distA < distB) ? candAy : candBy;
                    }
                    float const p2ToDstFinal = std::sqrt((p2x - dst.x) * (p2x - dst.x) + (p2y - dst.y) * (p2y - dst.y));
                    args.path.insert(args.path.begin() + 1, Vector3(p1x, p1y, src.z));
                    args.path.insert(args.path.begin() + 2, Vector3(p2x, p2y, src.z));
                    if (unit->GetMapId() == 13)
                        LOG_ERROR("movement", "ARC src=({:.2f},{:.2f}) orient={:.2f} bearingToNext={:.2f} angleDelta={:.2f} reach={:.2f} p1=({:.2f},{:.2f}) p2=({:.2f},{:.2f}) dst=({:.2f},{:.2f}) srcToDst={:.2f} p2ToDst={:.2f} pathSize={}",
                            src.x, src.y,
                            orient * 180.0f / M_PI, bearingToNext * 180.0f / M_PI, angleDelta * 180.0f / M_PI,
                            reach, p1x, p1y, p2x, p2y, dst.x, dst.y, distToFinal, p2ToDstFinal, args.path.size());
                }
            }
            args.flags.EnableCatmullRom();
        }
        
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
