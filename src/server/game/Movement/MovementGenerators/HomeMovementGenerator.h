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

#ifndef ACORE_HOMEMOVEMENTGENERATOR_H
#define ACORE_HOMEMOVEMENTGENERATOR_H

#include "MovementGenerator.h"

class Creature;

template < class T >
class HomeMovementGenerator;

template <>
class HomeMovementGenerator<Creature> : public MovementGeneratorMedium< Creature, HomeMovementGenerator<Creature> >
{
public:
    HomeMovementGenerator(bool walk) : arrived(false), i_recalculateTravel(false), _walk(walk), _repaths(0), _maxLegs(30), _stuckLegs(0), _waitTimer(0), _x(0.0f), _y(0.0f), _z(0.0f), _o(0.0f), _sx(0.0f), _sy(0.0f), _sz(0.0f) {}
    ~HomeMovementGenerator() {}

    void DoInitialize(Creature*);
    void DoFinalize(Creature*);
    void DoReset(Creature*);
    bool DoUpdate(Creature*, const uint32);
    MovementGeneratorType GetMovementGeneratorType() { return HOME_MOTION_TYPE; }
    void unitSpeedChanged() { i_recalculateTravel = true; }

private:
    void _setTargetLocation(Creature*);
    bool arrived : 1;
    bool i_recalculateTravel : 1;
    bool _walk;
    uint8 _repaths;
    uint8 _maxLegs;
    uint8 _stuckLegs;
    uint32 _waitTimer;
    float _x, _y, _z, _o;
    float _sx, _sy, _sz;
};
#endif
