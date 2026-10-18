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

#include "Log.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "Vehicle.h"
#include "WorldPacket.h"
#include "WorldSession.h"

namespace
{
    // Salvaged siege engines (vehicle kit 336, e.g. creature 33060) carry the
    // turret as a nested vehicle accessory. Boarding/exiting both levels in the
    // same tick wedges the client (forced root + locked orientation), so turret
    // leaves are split: exit now, re-board the driver seat a moment later.
    uint32 const SALVAGED_SIEGE_VEHICLE_ID = 336;

    // If the player rides a turret accessory of a salvaged siege engine,
    // returns the parent (siege) base. Otherwise returns nullptr.
    Unit* GetSalvagedSiegeParent(Player* player)
    {
        Vehicle* turret = player->GetVehicle();
        if (!turret)
            return nullptr;

        Unit* turretBase = turret->GetBase();
        if (!turretBase || turretBase == player)
            return nullptr;

        Vehicle* parentKit = turretBase->GetVehicle();
        if (!parentKit || !parentKit->GetVehicleInfo() || parentKit->GetVehicleInfo()->m_ID != SALVAGED_SIEGE_VEHICLE_ID)
            return nullptr;

        Unit* parentBase = parentKit->GetBase();
        if (!parentBase || !parentBase->IsAlive() || !parentBase->IsInWorld() || parentBase->IsDuringRemoveFromWorld())
            return nullptr;

        return parentBase;
    }

    void ScheduleSalvagedSiegeReboard(Player* player, Unit* parentBase)
    {
        ObjectGuid playerGuid = player->GetGUID();
        ObjectGuid parentGuid = parentBase->GetGUID();

        player->m_Events.AddEvent([playerGuid, parentGuid]()
        {
            Player* plr = ObjectAccessor::FindConnectedPlayer(playerGuid);
            if (!plr || !plr->IsAlive() || !plr->IsInWorld() || plr->IsDuringRemoveFromWorld())
                return;

            if (plr->GetVehicle()) // boarded something else meanwhile, don't yank
                return;

            Unit* parent = ObjectAccessor::GetUnit(*plr, parentGuid);
            if (!parent || !parent->IsAlive() || !parent->IsInWorld() || parent->IsDuringRemoveFromWorld())
                return;

            Vehicle* parentKit = parent->GetVehicleKit();
            if (!parentKit || !parentKit->HasEmptySeat(0))
                return;

            parent->HandleSpellClick(plr, 0);
        }, Milliseconds(1000));
    }

    // Exit the turret now. Driver seat free: re-board it a moment later
    // (point 1). Driver seat taken: teleport out beside the siege, the plain
    // nested exit spline does not land clientside (point 2).
    void ExitSalvagedTurret(Player* player, Unit* parent)
    {
        if (parent->GetVehicleKit()->HasEmptySeat(0))
        {
            player->ExitVehicle();
            ScheduleSalvagedSiegeReboard(player, parent);
        }
        else
        {
            Position exitPos = parent->GetPosition();
            parent->MovePositionToFirstCollision(exitPos, 5.0f, float(M_PI));
            player->ExitVehicle();
            player->NearTeleportTo(exitPos.GetPositionX(), exitPos.GetPositionY(), exitPos.GetPositionZ(), exitPos.GetOrientation(), false);
        }
    }

    // Point 1+2: leaving the turret. Returns true when handled.
    bool HandleSalvagedTurretLeave(Player* player)
    {
        Unit* parent = GetSalvagedSiegeParent(player);
        if (!parent)
            return false;

        ExitSalvagedTurret(player, parent);
        return true;
    }

    // Point 1+2 for explicit seat switches: turret -> parent driver seat.
    bool TrySalvagedTurretSwitchToDriver(Player* player, Unit* destBase, int8 seatId)
    {
        if (seatId != 0 || !destBase)
            return false;

        Unit* parent = GetSalvagedSiegeParent(player);
        if (!parent || destBase != parent)
            return false;

        ExitSalvagedTurret(player, parent);
        return true;
    }
} // namespace

void WorldSession::HandleDismissControlledVehicle(WorldPacket& recvData)
{
    LOG_DEBUG("network", "WORLD: Recvd CMSG_DISMISS_CONTROLLED_VEHICLE");

    ObjectGuid vehicleGUID = _player->GetCharmGUID();

    if (!vehicleGUID)                                       // something wrong here...
    {
        recvData.rfinish();                                // prevent warnings spam
        return;
    }

    // Salvaged siege turret leave (points 1+2).
    if (HandleSalvagedTurretLeave(_player))
    {
        recvData.rfinish();
        return;
    }

    ObjectGuid guid;
    recvData >> guid.ReadAsPacked();

    // pussywizard: typical check for incomming movement packets
    if (!_player->m_mover || !_player->m_mover->IsInWorld() || _player->m_mover->IsDuringRemoveFromWorld() || guid != _player->m_mover->GetGUID())
    {
        recvData.rfinish(); // prevent warnings spam
        _player->ExitVehicle();
        return;
    }

    MovementInfo mi;
    mi.guid = guid;
    ReadMovementInfo(recvData, &mi);

    _player->m_mover->m_movementInfo = mi;

    _player->ExitVehicle();
}

void WorldSession::HandleChangeSeatsOnControlledVehicle(WorldPacket& recvData)
{
    LOG_DEBUG("network", "WORLD: Recvd CMSG_CHANGE_SEATS_ON_CONTROLLED_VEHICLE");

    Unit* vehicle_base = GetPlayer()->GetVehicleBase();
    if (!vehicle_base)
    {
        recvData.rfinish();                                // prevent warnings spam
        return;
    }

    VehicleSeatEntry const* seat = GetPlayer()->GetVehicle()->GetSeatForPassenger(GetPlayer());
    if (!seat->CanSwitchFromSeat())
    {
        recvData.rfinish();                                // prevent warnings spam
        LOG_ERROR("network.opcode", "HandleChangeSeatsOnControlledVehicle, Opcode: {}, Player {} tried to switch seats but current seatflags {} don't permit that.",
                       recvData.GetOpcode(), GetPlayer()->GetGUID().ToString(), seat->m_flags);
        return;
    }

    switch (recvData.GetOpcode())
    {
        case CMSG_REQUEST_VEHICLE_PREV_SEAT:
        case CMSG_REQUEST_VEHICLE_NEXT_SEAT:
        {
            // Seat arrows from a salvaged turret move to the driver seat when it
            // is free, otherwise they keep today's behavior (no-op in the turret).
            bool next = recvData.GetOpcode() == CMSG_REQUEST_VEHICLE_NEXT_SEAT;
            Unit* parent = GetSalvagedSiegeParent(GetPlayer());
            if (parent && parent->GetVehicleKit()->HasEmptySeat(0))
            {
                GetPlayer()->ExitVehicle();
                ScheduleSalvagedSiegeReboard(GetPlayer(), parent);
            }
            else
                GetPlayer()->ChangeSeat(-1, next);
            break;
        }
        case CMSG_CHANGE_SEATS_ON_CONTROLLED_VEHICLE:
            {
                ObjectGuid guid;        // current vehicle guid
                recvData >> guid.ReadAsPacked();

                // pussywizard:
                if (vehicle_base->GetGUID() != guid)
                {
                    recvData.rfinish(); // prevent warnings spam
                    return;
                }

                MovementInfo movementInfo;
                movementInfo.guid = guid;
                ReadMovementInfo(recvData, &movementInfo);
                vehicle_base->m_movementInfo = movementInfo;

                ObjectGuid accessory;        //  accessory guid
                recvData >> accessory.ReadAsPacked();

                int8 seatId;
                recvData >> seatId;

                if (!accessory)
                    GetPlayer()->ChangeSeat(-1, seatId > 0); // prev/next
                else if (Unit* vehUnit = ObjectAccessor::GetUnit(*GetPlayer(), accessory))
                {
                    // Salvaged turret -> parent driver seat goes through the deferred
                    // transfer instead of a same-tick exit+enter.
                    if (TrySalvagedTurretSwitchToDriver(GetPlayer(), vehUnit, seatId))
                        break;

                    if (Vehicle* vehicle = vehUnit->GetVehicleKit())
                        if (vehicle->HasEmptySeat(seatId))
                            vehUnit->HandleSpellClick(GetPlayer(), seatId);
                }
                break;
            }
        case CMSG_REQUEST_VEHICLE_SWITCH_SEAT:
            {
                ObjectGuid guid;        // current vehicle guid
                recvData >> guid.ReadAsPacked();

                int8 seatId;
                recvData >> seatId;

                if (vehicle_base->GetGUID() == guid)
                    GetPlayer()->ChangeSeat(seatId);
                else if (Unit* vehUnit = ObjectAccessor::GetUnit(*GetPlayer(), guid))
                {
                    // Salvaged turret -> parent driver seat goes through the deferred
                    // transfer instead of a same-tick exit+enter.
                    if (TrySalvagedTurretSwitchToDriver(GetPlayer(), vehUnit, seatId))
                        break;

                    if (Vehicle* vehicle = vehUnit->GetVehicleKit())
                        if (vehicle->HasEmptySeat(seatId))
                            vehUnit->HandleSpellClick(GetPlayer(), seatId);
                }
                break;
            }
        default:
            break;
    }
}

void WorldSession::HandleEnterPlayerVehicle(WorldPacket& data)
{
    // Read guid
    ObjectGuid guid;
    data >> guid;

    if (Player* player = ObjectAccessor::GetPlayer(*_player, guid))
    {
        if (!player->GetVehicleKit())
            return;
        if (!player->IsInRaidWith(_player))
            return;
        if (!player->IsWithinDistInMap(_player, INTERACTION_DISTANCE))
            return;
        // Xinef:
        if (!_player->FindMap() || _player->FindMap()->IsBattleArena())
            return;

        _player->EnterVehicle(player);
    }
}

void WorldSession::HandleEjectPassenger(WorldPacket& data)
{
    Vehicle* vehicle = _player->GetVehicleKit();
    if (!vehicle)
    {
        data.rfinish();                                     // prevent warnings spam
        LOG_ERROR("network.opcode", "HandleEjectPassenger: Player {} is not in a vehicle!", GetPlayer()->GetGUID().ToString());
        return;
    }

    ObjectGuid guid;
    data >> guid;

    if (guid.IsPlayer())
    {
        Player* player = ObjectAccessor::GetPlayer(*_player, guid);
        if (!player)
        {
            LOG_ERROR("network.opcode", "Player {} tried to eject player {} from vehicle, but the latter was not found in world!", GetPlayer()->GetGUID().ToString(), guid.ToString());
            return;
        }

        if (!player->IsOnVehicle(vehicle->GetBase()))
        {
            LOG_ERROR("network.opcode", "Player {} tried to eject player {}, but they are not in the same vehicle", GetPlayer()->GetGUID().ToString(), guid.ToString());
            return;
        }

        VehicleSeatEntry const* seat = vehicle->GetSeatForPassenger(player);
        ASSERT(seat);
        if (seat->IsEjectable())
            player->ExitVehicle();
        else
            LOG_ERROR("network.opcode", "Player {} attempted to eject player {} from non-ejectable seat.", GetPlayer()->GetGUID().ToString(), guid.ToString());
    }
    else if (guid.IsCreature())
    {
        Unit* unit = ObjectAccessor::GetUnit(*_player, guid);
        if (!unit) // creatures can be ejected too from player mounts
        {
            LOG_ERROR("network.opcode", "Player {} tried to eject creature guid {} from vehicle, but the latter was not found in world!", GetPlayer()->GetGUID().ToString(), guid.ToString());
            return;
        }

        if (!unit->IsOnVehicle(vehicle->GetBase()))
        {
            LOG_ERROR("network.opcode", "Player {} tried to eject unit {}, but they are not in the same vehicle", GetPlayer()->GetGUID().ToString(), guid.ToString());
            return;
        }

        VehicleSeatEntry const* seat = vehicle->GetSeatForPassenger(unit);
        ASSERT(seat);
        if (seat->IsEjectable())
        {
            ASSERT(GetPlayer() == vehicle->GetBase());
            unit->ExitVehicle();
        }
        else
            LOG_ERROR("network.opcode", "Player {} attempted to eject creature {} from non-ejectable seat.", GetPlayer()->GetGUID().ToString(), guid.ToString());
    }
    else
        LOG_ERROR("network.opcode", "HandleEjectPassenger: Player {} tried to eject invalid {}", GetPlayer()->GetGUID().ToString(), guid.ToString());
}

void WorldSession::HandleRequestVehicleExit(WorldPacket& /*recvData*/)
{
    LOG_DEBUG("network", "WORLD: Recvd CMSG_REQUEST_VEHICLE_EXIT");

    // Salvaged siege turret leave (points 1+2).
    if (HandleSalvagedTurretLeave(GetPlayer()))
        return;

    if (Vehicle* vehicle = GetPlayer()->GetVehicle())
    {
        if (VehicleSeatEntry const* seat = vehicle->GetSeatForPassenger(GetPlayer()))
        {
            if (seat->CanEnterOrExit())
                GetPlayer()->ExitVehicle();
            else
                LOG_ERROR("network.opcode", "Player {} tried to exit vehicle, but seatflags {} (ID: {}) don't permit that.",
                               GetPlayer()->GetGUID().ToString(), seat->m_ID, seat->m_flags);
        }
    }
}
