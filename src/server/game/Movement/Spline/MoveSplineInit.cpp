#include "MoveSplineInit.h"
#include "MoveSpline.h"
#include "MovementPacketBuilder.h"
#include "Opcodes.h"
#include "Transport.h"
#include "Unit.h"
#include "Vehicle.h"
#include "WorldPacket.h"
#include "Log.h"
#include "EventProcessor.h"

namespace Movement
{
    struct DelayedMoveEvent : public BasicEvent
    {
        DelayedMoveEvent(Unit* unit, Vector3 const& dest, float velocity, bool hasVelocity, bool walkmode)
            : _unit(unit), _dest(dest), _velocity(velocity), _hasVelocity(hasVelocity), _walkmode(walkmode) {}

        bool Execute(uint64 /*e_time*/, uint32 /*p_time*/) override
        {
            MoveSplineInit init(_unit);
            init.args.disableSplit = true;
            init.MoveTo(_dest.x, _dest.y, _dest.z, false);
            if (_hasVelocity)
                init.SetVelocity(_velocity);
            init.SetWalk(_walkmode);
            init.Launch();
            return true;
        }

        Unit* _unit;
        Vector3 _dest;
        float _velocity;
        bool _hasVelocity;
        bool _walkmode;
    };

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

        if (args.path.empty())
            return 0;

        args.path[0] = real_position;
        args.initialOrientation = real_position.orientation;
        move_spline.onTransport = transport;

        bool hasSecondLeg = false;
        Vector3 secondLegDest;

        if (!unit->IsPlayer() && !args.flags.parabolic && !args.flags.falling &&
            !args.flags.animation && !args.flags.cyclic && !args.flags.flying &&
            !args.disableSplit &&
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

            if (std::fabs(angleDelta) > M_PI * 2.0f / 3.0f && distToNext >= 2.0f && distToFinal >= 2.0f)
            {
                secondLegDest = args.path.back();
                float const midAngle = orient + angleDelta * 0.5f;
                float const midDist  = distToFinal * 0.382f;
                Vector3 const mid(
                    src.x + midDist * std::cos(midAngle),
                    src.y + midDist * std::sin(midAngle),
                    src.z);

                args.path.resize(2);
                args.path[1] = mid;
                hasSecondLeg = true;

                if (unit->GetMapId() == 13)
                    LOG_ERROR("movement", "ARC SPLIT src=({:.2f},{:.2f}) orient={:.2f} angleDelta={:.2f} mid=({:.2f},{:.2f}) finalDst=({:.2f},{:.2f})",
                        src.x, src.y, orient * 180.0f / M_PI, angleDelta * 180.0f / M_PI,
                        mid.x, mid.y, secondLegDest.x, secondLegDest.y);
            }
        }

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
                if (std::fabs(angleDelta) > M_PI / 4.0f)
                {
                    bool usedSpiralArc = false;

                    if (std::fabs(angleDelta) > M_PI / 2.0f && std::fabs(angleDelta) <= M_PI * 2.0f / 3.0f)
                    {
                        Vector3 const dst = args.path.back();

                        float const side = (angleDelta >= 0.0f) ? 1.0f : -1.0f;
                        float const perpX = -std::sin(orient) * side;
                        float const perpY =  std::cos(orient) * side;

                        float const dstFromSrcX = dst.x - src.x;
                        float const dstFromSrcY = dst.y - src.y;
                        float const orientDirX = std::cos(orient);
                        float const orientDirY = std::sin(orient);
                        float const cross = orientDirX * dstFromSrcY - orientDirY * dstFromSrcX;
                        float const chordSq = dstFromSrcX * dstFromSrcX + dstFromSrcY * dstFromSrcY;

                        if (std::fabs(cross) > 0.01f)
                        {
                            float const radius = chordSq / (2.0f * std::fabs(cross));

                            float const cx = src.x + perpX * radius;
                            float const cy = src.y + perpY * radius;

                            float const angleToSrc = std::atan2(src.y - cy, src.x - cx);
                            float       angleToEnd = std::atan2(dst.y - cy, dst.x - cx);

                            float arcSweep = angleToEnd - angleToSrc;
                            if (side > 0.0f)
                            {
                                while (arcSweep < 0.0f) arcSweep += 2.0f * M_PI;
                            }
                            else
                            {
                                while (arcSweep > 0.0f) arcSweep -= 2.0f * M_PI;
                            }

                            float const p1x = cx + radius * std::cos(angleToSrc + arcSweep * 0.25f);
                            float const p1y = cy + radius * std::sin(angleToSrc + arcSweep * 0.25f);
                            float const p2x = cx + radius * std::cos(angleToSrc + arcSweep * 0.50f);
                            float const p2y = cy + radius * std::sin(angleToSrc + arcSweep * 0.50f);
                            float const p3x = cx + radius * std::cos(angleToSrc + arcSweep * 0.75f);
                            float const p3y = cy + radius * std::sin(angleToSrc + arcSweep * 0.75f);

                            args.path.insert(args.path.begin() + 1, Vector3(p1x, p1y, src.z));
                            args.path.insert(args.path.begin() + 2, Vector3(p2x, p2y, src.z));
                            args.path.insert(args.path.begin() + 3, Vector3(p3x, p3y, src.z));

                            if (unit->GetMapId() == 13)
                                LOG_ERROR("movement", "ARC SPIRAL src=({:.2f},{:.2f}) orient={:.2f} angleDelta={:.2f} radius={:.2f} center=({:.2f},{:.2f}) arcSweep={:.2f} p1=({:.2f},{:.2f}) p2=({:.2f},{:.2f}) p3=({:.2f},{:.2f}) dst=({:.2f},{:.2f})",
                                    src.x, src.y, orient * 180.0f / M_PI, angleDelta * 180.0f / M_PI,
                                    radius, cx, cy, arcSweep * 180.0f / M_PI,
                                    p1x, p1y, p2x, p2y, p3x, p3y, dst.x, dst.y);

                            usedSpiralArc = true;
                        }
                    }

                    if (!usedSpiralArc)
                    {
                        if (std::fabs(angleDelta) > M_PI / 2.0f)
                        {
                            float const reach = distToFinal * 0.35f;
                            float const cross = std::cos(orient) * toNext.y - std::sin(orient) * toNext.x;
                            float const side = (cross >= 0.0f) ? 1.0f : -1.0f;
                            float const perpAngle = orient + side * M_PI_2;
                            Vector3 const dst = args.path.back();
                            float const centerX = src.x + reach * std::cos(perpAngle);
                            float const centerY = src.y + reach * std::sin(perpAngle);
                            float const startAngle = std::atan2(src.y - centerY, src.x - centerX);
                            float const endAngle = startAngle + side * std::fabs(angleDelta);
                            float const srcToDstAngle = std::atan2(dst.y - src.y, dst.x - src.x);
                            float p1Angle = orient;
                            float p1AngleDelta = p1Angle - srcToDstAngle;
                            while (p1AngleDelta >  M_PI) p1AngleDelta -= 2.0f * M_PI;
                            while (p1AngleDelta < -M_PI) p1AngleDelta += 2.0f * M_PI;
                            if (std::fabs(p1AngleDelta) > M_PI / 2.0f)
                                p1Angle = srcToDstAngle + (p1AngleDelta > 0.0f ? M_PI / 2.0f : -M_PI / 2.0f);
                            float p1x = src.x + reach * std::cos(p1Angle);
                            float p1y = src.y + reach * std::sin(p1Angle);
                            float const circleEndX = centerX + reach * std::cos(endAngle);
                            float const circleEndY = centerY + reach * std::sin(endAngle);
                            float const circleEndToDst = std::sqrt((dst.x - circleEndX) * (dst.x - circleEndX) + (dst.y - circleEndY) * (dst.y - circleEndY));
                            float p2x, p2y;
                            if (circleEndToDst > distToFinal)
                            {
                                float const pullback = std::min(2.0f, distToFinal * 0.33f);
                                float const dstToSrcX = src.x - dst.x;
                                float const dstToSrcY = src.y - dst.y;
                                float const dstToSrcLen = std::sqrt(dstToSrcX * dstToSrcX + dstToSrcY * dstToSrcY);
                                p2x = dst.x + (dstToSrcX / dstToSrcLen) * pullback;
                                p2y = dst.y + (dstToSrcY / dstToSrcLen) * pullback;
                            }
                            else if (circleEndToDst < distToFinal * 0.2f)
                            {
                                p2x = circleEndX * 0.3f + dst.x * 0.7f;
                                p2y = circleEndY * 0.3f + dst.y * 0.7f;
                            }
                            else
                            {
                                float const pullback = std::min(2.0f, distToFinal * 0.33f);
                                float const approachX = dst.x - p1x;
                                float const approachY = dst.y - p1y;
                                float const approachLen = std::sqrt(approachX * approachX + approachY * approachY);
                                p2x = dst.x - (approachX / approachLen) * pullback;
                                p2y = dst.y - (approachY / approachLen) * pullback;
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

                            float const minDist = std::min(2.0f, distToFinal * 0.2f);

                            float const p1Dist = std::sqrt((p1x - src.x) * (p1x - src.x) + (p1y - src.y) * (p1y - src.y));
                            if (p1Dist < minDist && p1Dist > 0.0f)
                            {
                                float const scale = minDist / p1Dist;
                                p1x = src.x + (p1x - src.x) * scale;
                                p1y = src.y + (p1y - src.y) * scale;
                            }

                            float const p2DstDist = std::sqrt((p2x - dst.x) * (p2x - dst.x) + (p2y - dst.y) * (p2y - dst.y));
                            if (p2DstDist < minDist && p2DstDist > 0.0f)
                            {
                                float const scale = minDist / p2DstDist;
                                p2x = dst.x + (p2x - dst.x) * scale;
                                p2y = dst.y + (p2y - dst.y) * scale;
                            }

                            float const p2ToDstFinal = std::sqrt((p2x - dst.x) * (p2x - dst.x) + (p2y - dst.y) * (p2y - dst.y));
                            args.path.insert(args.path.begin() + 1, Vector3(p1x, p1y, src.z));
                            args.path.insert(args.path.begin() + 2, Vector3(p2x, p2y, src.z));

                            if (unit->GetMapId() == 13)
                                LOG_ERROR("movement", "ARC src=({:.2f},{:.2f}) orient={:.2f} bearingToNext={:.2f} angleDelta={:.2f} reach={:.2f} p1=({:.2f},{:.2f}) p2=({:.2f},{:.2f}) dst=({:.2f},{:.2f}) srcToDst={:.2f} p2ToDst={:.2f} circleEndToDst={:.2f} threshold20pct={:.2f} pathSize={}",
                                    src.x, src.y,
                                    orient * 180.0f / M_PI, bearingToNext * 180.0f / M_PI, angleDelta * 180.0f / M_PI,
                                    reach, p1x, p1y, p2x, p2y, dst.x, dst.y, distToFinal, p2ToDstFinal, circleEndToDst, distToFinal * 0.2f, args.path.size());
                        }
                        else
                        {
                            // 45-90°: single point at 30% along orient, offset perpendicular, front-loads the turn
                            float const side = (angleDelta >= 0.0f) ? 1.0f : -1.0f;
                            Vector3 const dst = args.path.back();
                            float const offset = distToFinal * 0.22f * (std::fabs(angleDelta) / (M_PI / 2.0f));
                            float const mpx = src.x + distToFinal * 0.30f * std::cos(orient) + offset * (-std::sin(orient) * side);
                            float const mpy = src.y + distToFinal * 0.30f * std::sin(orient) + offset * ( std::cos(orient) * side);
                            args.path.insert(args.path.begin() + 1, Vector3(mpx, mpy, src.z));

                            if (unit->GetMapId() == 13)
                                LOG_ERROR("movement", "ARC SHALLOW src=({:.2f},{:.2f}) orient={:.2f} angleDelta={:.2f} mp=({:.2f},{:.2f}) dst=({:.2f},{:.2f})",
                                    src.x, src.y, orient * 180.0f / M_PI, angleDelta * 180.0f / M_PI,
                                    mpx, mpy, dst.x, dst.y);
                        }
                    }
                }
                else
                {
                    // 15-45°: single control point placed at 25% along path, offset perpendicular to initial orient
                    // this front-loads the turn so rotation happens early and arrival is straight
                    float const side = (angleDelta >= 0.0f) ? 1.0f : -1.0f;
                    Vector3 const dst = args.path.back();
                    float const offset = distToFinal * 0.18f * (std::fabs(angleDelta) / (M_PI / 4.0f));
                    float const mpx = src.x + distToFinal * 0.25f * std::cos(orient) + offset * (-std::sin(orient) * side);
                    float const mpy = src.y + distToFinal * 0.25f * std::sin(orient) + offset * ( std::cos(orient) * side);
                    args.path.insert(args.path.begin() + 1, Vector3(mpx, mpy, src.z));
                }

                args.flags.EnableCatmullRom();
            }
        }

        uint32 moveFlags = unit->m_movementInfo.GetMovementFlags();
        moveFlags |= MOVEMENTFLAG_SPLINE_ENABLED;

        if (!args.flags.orientationInversed)
            moveFlags = (moveFlags & ~(MOVEMENTFLAG_BACKWARD)) | MOVEMENTFLAG_FORWARD;
        else
            moveFlags = (moveFlags & ~(MOVEMENTFLAG_FORWARD)) | MOVEMENTFLAG_BACKWARD;

        bool isOrientationOnly = args.path.size() == 2 && args.path[0] == args.path[1];

        if (moveFlags & MOVEMENTFLAG_ROOT)
        {
            LOG_TRACE("movement", "Invalid movement during root. Entry: {} IsImmobilized {}, moveflags {}", unit->GetEntry(), unit->IsImmobilizedState() ? "true" : "false", moveFlags);
            moveFlags &= ~MOVEMENTFLAG_MASK_MOVING;
        }

        if (isOrientationOnly)
            moveFlags &= ~MOVEMENTFLAG_MASK_MOVING;

        if (!args.HasVelocity)
        {
            uint32 moveFlagsForSpeed = moveFlags;
            if (args.flags.walkmode)
                moveFlagsForSpeed |= MOVEMENTFLAG_WALKING;
            else
                moveFlagsForSpeed &= ~MOVEMENTFLAG_WALKING;

            args.velocity = unit->GetSpeed(SelectSpeedType(moveFlagsForSpeed));
        }

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

        int32 const duration = move_spline.Duration();

        if (hasSecondLeg)
            unit->m_Events.AddEvent(
                new DelayedMoveEvent(unit, secondLegDest, args.velocity, args.HasVelocity, args.flags.walkmode),
                unit->m_Events.CalculateTime(duration));

        return duration;
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