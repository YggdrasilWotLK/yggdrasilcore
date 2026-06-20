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

#include "AchievementCriteriaScript.h"
#include "CreatureScript.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellScriptLoader.h"
#include "azjol_nerub.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"

// Configs
constexpr bool PLAYER_DMG_ON_HADRONOX_STOPS_ADD_SUMMONS = false; // Blizzlike is false, true is more akin to other pservers
constexpr bool HADRONOX_STOP_ASCENT_ON_PLAYER_DAMAGE    = true;  // Blizzlike is true
constexpr bool WEB_GRAB_OVERRIDE                        = true;  // Emulate web grab by using range-locked single target spell looping through and grabbing valid targets in range. Sniffed spell has too long range and grabs through floor. Blizzlike is false but bugged

enum Spells
{
    SPELL_SUMMON_ANUBAR_CHAMPION            = 53064,
    SPELL_SUMMON_ANUBAR_CRYPT_FIEND         = 53065,
    SPELL_SUMMON_ANUBAR_NECROMANCER         = 53066,
    SPELL_WEB_FRONT_DOORS                   = 53177, // Unscripted
    SPELL_WEB_SIDE_DOORS                    = 53185, // Apply flat aura on dummy instead of using spell cast
    SPELL_ACID_CLOUD                        = 53400,
    SPELL_LEECH_POISON                      = 53030,
    SPELL_LEECH_POISON_HEAL                 = 53800,
    SPELL_WEB_GRAB                          = 57731,
    SPELL_WEB_GRAB_OVERRIDE                 = 56640,
    SPELL_PIERCE_ARMOR                      = 53418,

    SPELL_SMASH                             = 53318,
    SPELL_FRENZY                            = 53801,

    SPELL_CHAMPION_REND                     = 53317,
    SPELL_CHAMPION_REND_H                   = 59343,
    SPELL_CHAMPION_PUMMEL                   = 53394,
    SPELL_CHAMPION_PUMMEL_H                 = 59344,

    SPELL_NECROMANCER_INFECTED_WOUND        = 53330,
    SPELL_NECROMANCER_INFECTED_WOUND_H      = 59348,
    SPELL_NECROMANCER_CRUSHING_WEBS         = 53322,
    SPELL_NECROMANCER_CRUSHING_WEBS_H       = 59347,

    SPELL_CRYPT_FIEND_SHADOW_BOLT           = 53333
};

enum Events
{
    EVENT_HADRONOX_ACID             = 5,
    EVENT_HADRONOX_LEECH            = 6,
    EVENT_HADRONOX_PIERCE           = 7,
    EVENT_HADRONOX_GRAB             = 8,
    EVENT_HADRONOX_CHECK            = 10,
    EVENT_HADRONOX_SUMMON_ADD       = 11,
    EVENT_HADRONOX_STEP             = 14,
    EVENT_HADRONOX_NEXT_WAYPOINT    = 15,

    EVENT_CRUSHER_SMASH             = 20,
    EVENT_CHECK_HEALTH              = 21,
    EVENT_CRUSHER_SPAWN_ADD1        = 22,
    EVENT_CRUSHER_SPAWN_ADD2        = 23,
    EVENT_CRUSHER_SPAWN_ADD3        = 24,
    EVENT_CRUSHER_SPAWN_ADD4        = 25,

    EVENT_CHAMPION_REND             = 1,
    EVENT_CHAMPION_PUMMEL           = 2,

    EVENT_NECRO_INFECTED_WOUND      = 1,
    EVENT_NECRO_CRUSHING_WEBS       = 2,

    EVENT_CRYPT_SHADOW_BOLT         = 1
};

enum Misc
{
    NPC_ANUB_AR_CRUSHER         = 28922,
    NPC_WEB_DUMMY_TARGET        = 24648,

    SAY_CRUSHER_AGGRO           = 1,
    SAY_CRUSHER_EMOTE           = 2,
    SAY_HADRONOX_EMOTE          = 0,

    ACTION_START_EVENT          = 2,
    ACTION_START_WALK           = 3,
    ACTION_STOP_SPAWNS          = 4,
    ACTION_CRUSHER_EVADE        = 5,
    ACTION_CRUSHER_AGGRO_SAY    = 6
};

constexpr float GAUNTLET_END_Z        = 640.0f;
constexpr float GAUNTLET_START_Z      = 730.0f;
constexpr float GAUNTLET_MAX_Y        = 625.0f;
constexpr float STEP_REACH            = 3.0f;
constexpr float STEP_SPEED            = 4.5f;
constexpr float ADDS_RANGE            = 8.0f;
constexpr float SPELL_MAX_DIST        = 40.0f;
constexpr float SPELL_MAX_Z_DIFF      = 20.0f;
constexpr float WAYPOINT_REACH        = 1.0f;
constexpr uint32 ADD_CHECK_MS         = 100;
constexpr float HADRONOX_LEASH_RANGE  = 10.0f;
constexpr uint32 HADRONOX_LEASH_CHECK = 2000;
const Position HADRONOX_SPAWN_POS = {522.531f, 544.911f, 674.679f, 0.0f};

const Position hadronoxWaypoints[8] =
{
    {533.5879f,  533.34607f, 681.70135f, 0.0f},
    {530.96313f, 520.79193f, 688.7247f,  0.0f},
    {533.6001f,  514.1237f,  694.8442f,  0.0f},
    {567.3953f,  513.29114f, 698.8085f,  0.0f},
    {607.9f,     512.8f,     695.3f,     0.0f},
    {611.67f,    564.11f,    720.0f,     0.0f},
    {576.1f,     580.0f,     727.5f,     0.0f},
    {534.87f,    554.0f,     733.0f,     0.0f}
};

const Position addSpawnPos[3] =
{
    {577.2f, 613.45f, 771.51f, 0.0f},
    {483.68f, 612.75f, 771.42f, 0.0f},
    {584.7559f, 603.21466f, 739.14014f, 0.0f}
};

constexpr uint32 ADD_WAYPOINT_COUNT = 15;
const Position addWaypoints[ADD_WAYPOINT_COUNT] =
{
    {530.65857f, 576.39716f, 733.4407f,  0.0f},
    {544.5423f,  568.7522f,  731.1956f,  0.0f},
    {553.83936f, 568.7116f,  729.4889f,  0.0f},
    {561.3301f,  572.9708f,  728.1504f,  0.0f},
    {571.4326f,  573.8239f,  727.1494f,  0.0f},
    {585.2161f,  576.30334f, 726.0641f,  0.0f},
    {597.7994f,  572.25964f, 723.12836f, 0.0f},
    {608.68f,    554.1495f,  714.67725f, 0.0f},
    {620.7482f,  539.47156f, 706.5057f,  0.0f},
    {620.7156f,  528.55646f, 698.867f,   0.0f},
    {603.81274f, 510.69556f, 694.7088f,  0.0f},
    {586.75836f, 512.0464f,  695.60925f, 0.0f},
    {566.58093f, 514.4511f,  698.7215f,  0.0f},
    {552.9107f,  523.64606f, 688.75604f, 0.0f},
    {540.6518f,  532.722f,   684.9354f,  0.0f}
};

const Position ADD_SPAWN3_JOIN_WP = {609.41376f, 574.623f, 722.0532f, 0.0f};

const Position webDummyPositions[3] =
{
    {583.8375f,  606.46747f, 739.3754f,  1.6401535f},
    {577.10364f, 612.30005f, 771.4738f,  0.60115397f},
    {482.53622f, 617.4355f,  771.92834f, 2.1164744f}
};

namespace HadronoxAddSlots
{
    static const Position Positions[4] =
    {
        {544.9088f,  567.09296f, 731.0783f,  0.0f},
        {534.2329f,  561.84143f, 732.4554f,  0.0f},
        {526.7353f,  560.04004f, 732.99493f, 0.0f},
        {516.26666f, 566.3751f,  734.37054f, 0.0f}
    };
    static bool Occupied[4] = {false, false, false, false};

    static void Reset()
    {
        for (int i = 0; i < 4; ++i)
            Occupied[i] = false;
    }
}

static bool IsValidSpellTarget(Unit* caster, WorldObject* target)
{
    if (!caster || !target)
        return false;
    if (caster->GetExactDist(target) > SPELL_MAX_DIST)
        return false;
    if (std::abs(caster->GetPositionZ() - target->GetPositionZ()) > SPELL_MAX_Z_DIFF)
        return false;
    return true;
}

struct npc_hadronox_addAI : public ScriptedAI
{
    npc_hadronox_addAI(Creature* creature) : ScriptedAI(creature)
    {
        _currentWaypoint = 0;
        _reachedHadronox = false;
        _attackedByPlayer = false;
        _spawnedAbove735 = creature->GetPositionZ() >= 735.0f;
        if (_spawnedAbove735)
            if (TempSummon* ts = creature->ToTempSummon())
                if (Unit* summoner = ts->GetSummonerUnit())
                    _spawnedAbove735 = (summoner->GetEntry() == NPC_HADRONOX);
        _chasingHadronox = false;
        _checkTimer = 0;
        _spawnIndex = -1;
        _joinedMainPath = false;
        _reservedSlot = -1;
        _crusherPathStep = 0;
        _crusherSlotPos = {0.0f, 0.0f, 0.0f, 0.0f};
    }

    uint32 _currentWaypoint;
    bool _reachedHadronox;
    bool _attackedByPlayer;
    bool _spawnedAbove735;
    bool _chasingHadronox;
    uint32 _checkTimer;
    int32 _spawnIndex;
    bool _joinedMainPath;
    int32 _reservedSlot;
    uint32 _crusherPathStep;
    Position _crusherSlotPos;
    EventMap events;

    bool IsHeroic() const { return me->GetMap()->IsHeroic(); }

    void Reset() override
    {
        ReleaseSlot();
        _currentWaypoint = 0;
        _reachedHadronox = false;
        _attackedByPlayer = false;
        _chasingHadronox = false;
        _checkTimer = 0;
        _joinedMainPath = false;
        events.Reset();
        if (_spawnedAbove735)
            IssueMove();
    }

    void SetData(uint32 id, uint32 value) override
    {
        if (id == 0)
        {
            _spawnIndex = (int32)value;
            _spawnedAbove735 = true;
            IssueMove();
        }
        else if (id == 1)
            _reservedSlot = (int32)value;
    }

    void StartCrusherPath(const Position& slotPos)
    {
        _crusherSlotPos = slotPos;
        _crusherPathStep = 1;
        me->GetMotionMaster()->MovePoint(0,
            addWaypoints[0].GetPositionX(),
            addWaypoints[0].GetPositionY(),
            addWaypoints[0].GetPositionZ(),
            FORCED_MOVEMENT_RUN);
    }

    void ReleaseSlot()
    {
        if (_reservedSlot >= 0 && _reservedSlot < 4)
        {
            HadronoxAddSlots::Occupied[_reservedSlot] = false;
            _reservedSlot = -1;
        }
    }

    Creature* FindHadronox() const
    {
        return me->FindNearestCreature(NPC_HADRONOX, 500.0f, true);
    }

    void MoveTowardHadronox(Creature* hadronox)
    {
        me->GetMotionMaster()->MovePoint(0, hadronox->GetPositionX(), hadronox->GetPositionY(), hadronox->GetPositionZ(), FORCED_MOVEMENT_RUN);
    }

    void EngageHadronox(Creature* hadronox)
    {
        _reachedHadronox = true;
        _chasingHadronox = false;
        me->SetReactState(REACT_AGGRESSIVE);
        AttackStart(hadronox);
        ScheduleCombatEvents();
    }

    void PullIntoCombat(Unit* puller)
    {
        if (_attackedByPlayer)
            return;
        _attackedByPlayer = true;
        _reachedHadronox = false;
        _chasingHadronox = false;
        _crusherPathStep = 0;
        me->GetMotionMaster()->Clear();
        me->SetReactState(REACT_AGGRESSIVE);
        if (puller)
        {
            if (Unit* victim = puller->GetVictim())
            {
                AttackStart(victim);
                me->AddThreat(victim, 1.0f);
            }
        }
        ScheduleCombatEvents();
    }

    // Issues MovePoint to current waypoint, or MovePoint to Hadronox if closer.
    void IssueMove()
    {
        if (_spawnIndex == 2 && !_joinedMainPath)
        {
            Creature* hadronox = FindHadronox();
            if (hadronox && me->GetExactDist(hadronox) <= ADDS_RANGE)
            {
                EngageHadronox(hadronox);
                return;
            }
            me->GetMotionMaster()->MovePoint(9999,
                ADD_SPAWN3_JOIN_WP.GetPositionX(),
                ADD_SPAWN3_JOIN_WP.GetPositionY(),
                ADD_SPAWN3_JOIN_WP.GetPositionZ(),
                FORCED_MOVEMENT_RUN);
            return;
        }

        Creature* hadronox = FindHadronox();

        if (hadronox)
        {
            float distToHadronox = me->GetExactDist(hadronox);
            if (distToHadronox <= ADDS_RANGE)
            {
                EngageHadronox(hadronox);
                return;
            }

            const Position& nextWp = addWaypoints[_currentWaypoint % ADD_WAYPOINT_COUNT];
            float distToNext = me->GetExactDist(nextWp);

            if (distToHadronox < distToNext)
            {
                _chasingHadronox = true;
                MoveTowardHadronox(hadronox);
                return;
            }
        }

        _chasingHadronox = false;
        me->GetMotionMaster()->MovePoint(
            _currentWaypoint,
            addWaypoints[_currentWaypoint % ADD_WAYPOINT_COUNT].GetPositionX(),
            addWaypoints[_currentWaypoint % ADD_WAYPOINT_COUNT].GetPositionY(),
            addWaypoints[_currentWaypoint % ADD_WAYPOINT_COUNT].GetPositionZ(),
            FORCED_MOVEMENT_RUN);
    }

    void DamageTaken(Unit* who, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellSchoolMask /*damageSchoolMask*/) override
    {
        if (!_attackedByPlayer && who && who->IsControlledByPlayer())
        {
            _attackedByPlayer = true;
            _reachedHadronox = false;
            _chasingHadronox = false;
            _crusherPathStep = 0;
            me->GetMotionMaster()->Clear();
            if (who->IsPlayer())
                AttackStart(who);
        }
    }
    
    void JustEngagedWith(Unit* who) override
    {
        me->SetReactState(REACT_AGGRESSIVE);
        if (who->IsPlayer())
            AttackStart(who);
        ScheduleCombatEvents();

        std::list<Creature*> crushers;
        me->GetCreaturesWithEntryInRange(crushers, 10.0f, NPC_ANUB_AR_CRUSHER);
        for (Creature* crusher : crushers)
            if (crusher->IsAlive() && !crusher->IsInCombat())
                crusher->AI()->AttackStart(who);
    }
    
    virtual void ScheduleCombatEvents() = 0;

    bool NearHadronox() const
    {
        return me->FindNearestCreature(NPC_HADRONOX, ADDS_RANGE, true) != nullptr;
    }

    bool ShouldUseCombatAbilities() const
    {
        return _attackedByPlayer || NearHadronox();
    }

    void UpdateAI(uint32 diff) override
    {
        if (_crusherPathStep > 0 && !_attackedByPlayer)
        {
            if (_crusherPathStep == 1)
            {
                if (me->GetExactDist(addWaypoints[0]) <= WAYPOINT_REACH)
                {
                    _crusherPathStep = 2;
                    me->GetMotionMaster()->MovePoint(1,
                        _crusherSlotPos.GetPositionX(),
                        _crusherSlotPos.GetPositionY(),
                        _crusherSlotPos.GetPositionZ(),
                        FORCED_MOVEMENT_RUN);
                }
                else if (!me->isMoving())
                {
                    me->GetMotionMaster()->MovePoint(0,
                        addWaypoints[0].GetPositionX(),
                        addWaypoints[0].GetPositionY(),
                        addWaypoints[0].GetPositionZ(),
                        FORCED_MOVEMENT_RUN);
                }
            }
            else if (_crusherPathStep == 2)
            {
                if (me->GetExactDist(_crusherSlotPos) <= WAYPOINT_REACH)
                    _crusherPathStep = 0;
                else if (!me->isMoving())
                {
                    me->GetMotionMaster()->MovePoint(1,
                        _crusherSlotPos.GetPositionX(),
                        _crusherSlotPos.GetPositionY(),
                        _crusherSlotPos.GetPositionZ(),
                        FORCED_MOVEMENT_RUN);
                }
            }
            return;
        }

        if (_attackedByPlayer)
        {
            bool hasPlayerThreat = false;
            ThreatMgr& mgr = me->GetThreatMgr();
            for (auto* ref : mgr.GetThreatList())
            {
                if (ref->GetVictim() && ref->GetVictim()->IsControlledByPlayer())
                {
                    hasPlayerThreat = true;
                    break;
                }
            }
            if (!hasPlayerThreat)
            {
                _attackedByPlayer = false;
                me->SetReactState(REACT_PASSIVE);
                me->AttackStop();
                me->GetThreatMgr().ClearAllThreat();
                IssueMove();
            }
        }

        if (_reachedHadronox && !_attackedByPlayer)
        {
            Creature* hadronox = FindHadronox();
            if (hadronox)
            {
                float dist = me->GetExactDist(hadronox);
                if (me->isMoving() && dist <= 3.0f)
                    me->GetMotionMaster()->Clear();
                else if (!me->isMoving() && dist > 8.0f)
                    MoveTowardHadronox(hadronox);
            }
            return;
        }

        if (!_spawnedAbove735 || _attackedByPlayer)
            return;

        Creature* hadronox = FindHadronox();

        if (hadronox && me->GetExactDist(hadronox) <= ADDS_RANGE)
        {
            EngageHadronox(hadronox);
            return;
        }

        if (_spawnIndex == 2 && !_joinedMainPath)
        {
            if (me->GetExactDist(ADD_SPAWN3_JOIN_WP) <= WAYPOINT_REACH)
            {
                _joinedMainPath = true;
                _currentWaypoint = 8;
                IssueMove();
            }
            else if (!me->isMoving())
                IssueMove();
            return;
        }

        if (_chasingHadronox && hadronox)
        {
            float dist = me->GetExactDist(hadronox);
            if (dist <= 4.0f)
                me->GetMotionMaster()->Clear();
            else if (dist >= 7.0f)
                MoveTowardHadronox(hadronox);
            return;
        }

        // Advance waypoint if reached
        const Position& wp = addWaypoints[_currentWaypoint % ADD_WAYPOINT_COUNT];
        if (me->GetExactDist(wp) <= WAYPOINT_REACH)
            _currentWaypoint++;

        if (!me->isMoving())
        {
            me->GetMotionMaster()->MovePoint(
                _currentWaypoint,
                addWaypoints[_currentWaypoint % ADD_WAYPOINT_COUNT].GetPositionX(),
                addWaypoints[_currentWaypoint % ADD_WAYPOINT_COUNT].GetPositionY(),
                addWaypoints[_currentWaypoint % ADD_WAYPOINT_COUNT].GetPositionZ(),
                FORCED_MOVEMENT_RUN);
        }

        _checkTimer += diff;
        if (_checkTimer < ADD_CHECK_MS)
            return;
        _checkTimer = 0;

        if (!hadronox)
            return;

        // Special case: reached waypoint 0 and Hadronox is already high
        if (_currentWaypoint == 0
            && me->GetExactDist(addWaypoints[0]) <= WAYPOINT_REACH
            && hadronox->GetPositionZ() > 729.0f)
        {
            _currentWaypoint = 1;
            _chasingHadronox = true;
            MoveTowardHadronox(hadronox);
            return;
        }

        float distToHadronox = me->GetExactDist(hadronox);
        const Position& nextWp = addWaypoints[_currentWaypoint % ADD_WAYPOINT_COUNT];
        float distToNext = me->GetExactDist(nextWp);
        if (distToHadronox < distToNext)
        {
            _chasingHadronox = true;
            MoveTowardHadronox(hadronox);
        }
    }
};

class npc_anub_ar_champion : public CreatureScript
{
public:
    npc_anub_ar_champion() : CreatureScript("npc_anub_ar_champion") { }

    struct npc_anub_ar_championAI : public npc_hadronox_addAI
    {
        npc_anub_ar_championAI(Creature* creature) : npc_hadronox_addAI(creature) { }

        void ScheduleCombatEvents() override
        {
            events.ScheduleEvent(EVENT_CHAMPION_REND, Milliseconds(urand(4000, 7000)));
            events.ScheduleEvent(EVENT_CHAMPION_PUMMEL, Milliseconds(urand(9000, 13000)));
        }

        void JustDied(Unit* /*killer*/) override
        {
            ReleaseSlot();
            me->DespawnOrUnsummon(10000ms);
        }

        void UpdateAI(uint32 diff) override
        {
            npc_hadronox_addAI::UpdateAI(diff);

            if (_spawnedAbove735 && !ShouldUseCombatAbilities())
                me->SetReactState(REACT_PASSIVE);
            else if (me->GetReactState() == REACT_PASSIVE)
            {
                me->SetReactState(REACT_AGGRESSIVE);
                if (me->GetVictim())
                    AttackStart(me->GetVictim());
            }

            if (!UpdateVictim())
                return;

            if (!ShouldUseCombatAbilities())
                return;

            events.Update(diff);

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;

            switch (events.ExecuteEvent())
            {
                case EVENT_CHAMPION_REND:
                    if (me->GetVictim() && IsValidSpellTarget(me, me->GetVictim()))
                        me->CastSpell(me->GetVictim(), IsHeroic() ? SPELL_CHAMPION_REND_H : SPELL_CHAMPION_REND, false);
                    events.ScheduleEvent(EVENT_CHAMPION_REND, Milliseconds(urand(12000, 18000)));
                    break;
                case EVENT_CHAMPION_PUMMEL:
                    if (me->GetVictim() && me->GetVictim()->HasUnitState(UNIT_STATE_CASTING) && IsValidSpellTarget(me, me->GetVictim()))
                        me->CastSpell(me->GetVictim(), IsHeroic() ? SPELL_CHAMPION_PUMMEL_H : SPELL_CHAMPION_PUMMEL, false);
                    events.ScheduleEvent(EVENT_CHAMPION_PUMMEL, Milliseconds(urand(9000, 13000)));
                    break;
            }

            DoMeleeAttackIfReady();
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetAzjolNerubAI<npc_anub_ar_championAI>(creature);
    }
};

class npc_anub_ar_necromancer : public CreatureScript
{
public:
    npc_anub_ar_necromancer() : CreatureScript("npc_anub_ar_necromancer") { }

    struct npc_anub_ar_necromancerAI : public npc_hadronox_addAI
    {
        npc_anub_ar_necromancerAI(Creature* creature) : npc_hadronox_addAI(creature) { }

        void ScheduleCombatEvents() override
        {
            events.ScheduleEvent(EVENT_NECRO_INFECTED_WOUND, Milliseconds(urand(4000, 7000)));
            events.ScheduleEvent(EVENT_NECRO_CRUSHING_WEBS, Milliseconds(urand(9000, 12000)));
        }

        void JustDied(Unit* /*killer*/) override
        {
            ReleaseSlot();
            me->DespawnOrUnsummon(10000ms);
        }

        void UpdateAI(uint32 diff) override
        {
            npc_hadronox_addAI::UpdateAI(diff);

            if (_spawnedAbove735 && !ShouldUseCombatAbilities())
                me->SetReactState(REACT_PASSIVE);
            else if (me->GetReactState() == REACT_PASSIVE)
            {
                me->SetReactState(REACT_AGGRESSIVE);
                if (me->GetVictim())
                    AttackStart(me->GetVictim());
            }

            if (!UpdateVictim())
                return;

            if (!ShouldUseCombatAbilities())
                return;

            events.Update(diff);

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;

            switch (events.ExecuteEvent())
            {
                case EVENT_NECRO_INFECTED_WOUND:
                    if (me->GetVictim() && IsValidSpellTarget(me, me->GetVictim()))
                        me->CastSpell(me->GetVictim(), IsHeroic() ? SPELL_NECROMANCER_INFECTED_WOUND_H : SPELL_NECROMANCER_INFECTED_WOUND, false);
                    events.ScheduleEvent(EVENT_NECRO_INFECTED_WOUND, Milliseconds(urand(9000, 12000)));
                    break;
                case EVENT_NECRO_CRUSHING_WEBS:
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 30.0f, true))
                        if (IsValidSpellTarget(me, target))
                            me->CastSpell(target, IsHeroic() ? SPELL_NECROMANCER_CRUSHING_WEBS_H : SPELL_NECROMANCER_CRUSHING_WEBS, false);
                    events.ScheduleEvent(EVENT_NECRO_CRUSHING_WEBS, Milliseconds(urand(10000, 13000)));
                    break;
            }

            DoMeleeAttackIfReady();
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetAzjolNerubAI<npc_anub_ar_necromancerAI>(creature);
    }
};

class npc_anub_ar_cryptfiend : public CreatureScript
{
public:
    npc_anub_ar_cryptfiend() : CreatureScript("npc_anub_ar_cryptfiend") { }

    struct npc_anub_ar_cryptfiendAI : public npc_hadronox_addAI
    {
        npc_anub_ar_cryptfiendAI(Creature* creature) : npc_hadronox_addAI(creature) { }

        void ScheduleCombatEvents() override
        {
            events.ScheduleEvent(EVENT_CRYPT_SHADOW_BOLT, Milliseconds(urand(0, 1000)));
        }

        void JustDied(Unit* /*killer*/) override
        {
            ReleaseSlot();
            me->DespawnOrUnsummon(10000ms);
        }

        void UpdateAI(uint32 diff) override
        {
            npc_hadronox_addAI::UpdateAI(diff);

            if (_spawnedAbove735 && !ShouldUseCombatAbilities())
                me->SetReactState(REACT_PASSIVE);
            else if (me->GetReactState() == REACT_PASSIVE)
            {
                me->SetReactState(REACT_AGGRESSIVE);
                if (me->GetVictim())
                    AttackStart(me->GetVictim());
            }

            if (!UpdateVictim())
                return;

            if (!ShouldUseCombatAbilities())
                return;

            events.Update(diff);

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;

            switch (events.ExecuteEvent())
            {
                case EVENT_CRYPT_SHADOW_BOLT:
                    if (me->GetVictim() && IsValidSpellTarget(me, me->GetVictim()))
                        me->CastSpell(me->GetVictim(), SPELL_CRYPT_FIEND_SHADOW_BOLT, true);
                    events.ScheduleEvent(EVENT_CRYPT_SHADOW_BOLT, Milliseconds(urand(2000, 3000)));
                    break;
            }

            DoMeleeAttackIfReady();
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetAzjolNerubAI<npc_anub_ar_cryptfiendAI>(creature);
    }
};

class boss_hadronox : public CreatureScript
{
public:
    boss_hadronox() : CreatureScript("boss_hadronox") { }

    struct boss_hadronoxAI : public BossAI
    {
        boss_hadronoxAI(Creature* creature) : BossAI(creature, DATA_HADRONOX)
        {
            _walkStarted = false;
            _spawnsActive = true;
            _combatStarted = false;
            _playerAttacked = false;
            _crusherAggroSaid = false;
            _startedSummons = false;
            _currentStep = -1;
            _spawnCount = 0;
            _movementCheckTimer = 0;
            _lastPos = creature->GetPosition();
            _waitingForNextStep = false;
            _reachedFinalWaypoint = false;
            _leashPos = {0.0f, 0.0f, 0.0f, 0.0f};
            _leashCheckTimer = 0;
        }

        bool _walkStarted;
        bool _spawnsActive;
        bool _combatStarted;
        bool _playerAttacked;
        bool _crusherAggroSaid;
        bool _startedSummons;
        bool _waitingForNextStep;
        bool _reachedFinalWaypoint;
        int32 _currentStep;
        uint32 _spawnCount;
        uint32 _movementCheckTimer;
        uint32 _leashCheckTimer;
        Position _lastPos;
        Position _leashPos;

        void Reset() override
        {
            BossAI::Reset();
            HadronoxAddSlots::Reset();
            _walkStarted = false;
            _spawnsActive = true;
            _combatStarted = false;
            _playerAttacked = false;
            _crusherAggroSaid = false;
            _startedSummons = false;
            _currentStep = -1;
            _spawnCount = 0;
            _movementCheckTimer = 0;
            _waitingForNextStep = false;
            _reachedFinalWaypoint = false;
            _leashPos = {0.0f, 0.0f, 0.0f, 0.0f};
            _leashCheckTimer = 0;
            _lastPos = me->GetPosition();
            me->SummonCreature(NPC_ANUB_AR_CRUSHER, 531.5281f, 553.8509f, 732.56f, 5.053f);
            me->SummonCreature(NPC_ANUB_AR_CRYPTFIEND, 524.07886f, 550.6797f, 731.873f, 5.053f);
            me->SummonCreature(NPC_ANUB_AR_CHAMPION, 541.0999f, 555.21924f, 732.152f, 5.053f);
            events.ScheduleEvent(EVENT_HADRONOX_CHECK, 1s);
        }

        void SpawnWebDummy(uint32 index)
        {
            if (index >= 3)
                return;
            if (Creature* dummy = me->SummonCreature(NPC_WEB_DUMMY_TARGET,
                webDummyPositions[index].GetPositionX(),
                webDummyPositions[index].GetPositionY(),
                webDummyPositions[index].GetPositionZ(),
                webDummyPositions[index].GetOrientation(),
                TEMPSUMMON_MANUAL_DESPAWN))
            {
                dummy->SetObjectScale(0.5f);
                dummy->AddAura(SPELL_WEB_SIDE_DOORS, dummy);
            }
        }

        void MoveToWaypoint(int32 index)
        {
            if (index < 0 || index >= 8)
                return;
            if (index == 6)
                SpawnWebDummy(0);
            if (index == 7)
            {
                SpawnWebDummy(1);
                SpawnWebDummy(2);
                _spawnsActive = false;
                events.CancelEvent(EVENT_HADRONOX_SUMMON_ADD);
            }
            me->GetMotionMaster()->MoveCharge(
                hadronoxWaypoints[index].GetPositionX(),
                hadronoxWaypoints[index].GetPositionY(),
                hadronoxWaypoints[index].GetPositionZ(),
                STEP_SPEED, 0, nullptr, true);
            _lastPos = me->GetPosition();
        }

        void StartWalkEvent()
        {
            if (_walkStarted)
                return;
            _walkStarted = true;
            instance->SetBossState(DATA_HADRONOX, IN_PROGRESS);
            me->setActive(true);
            events.ScheduleEvent(EVENT_HADRONOX_STEP, 10s);
        }

        void StartSummonEvents()
        {
            events.ScheduleEvent(EVENT_HADRONOX_SUMMON_ADD, 2s);
        }

        void SummonAdd(uint32 entry)
        {
            if (!_spawnsActive)
                return;
            uint32 spawnIndex = _spawnCount % 3;
            Position pos = addSpawnPos[spawnIndex];
            Creature* add = me->SummonCreature(entry, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), 0.0f, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5000);
            if (add)
                add->AI()->SetData(0, spawnIndex);
            _spawnCount++;
        }

        void DoAction(int32 param) override
        {
            if (param == ACTION_START_EVENT || param == ACTION_START_WALK)
                StartWalkEvent();
            else if (param == ACTION_STOP_SPAWNS)
                _spawnsActive = false;
            else if (param == ACTION_CRUSHER_EVADE)
                me->AI()->EnterEvadeMode();
            else if (param == ACTION_CRUSHER_AGGRO_SAY)
            {
                if (!_crusherAggroSaid)
                {
                    _crusherAggroSaid = true;
                    if (Creature* crusher = me->FindNearestCreature(NPC_ANUB_AR_CRUSHER, 300.0f, true))
                        crusher->AI()->Talk(SAY_CRUSHER_AGGRO);
                    StartSummonEvents();
                }
            }
        }

        void EnterEvadeMode(EvadeReason /*why*/) override
        {
            if (AnyPlayerInHadronoxGauntlet())
                return;
            me->RemoveAllDynObjects();
            BossAI::EnterEvadeMode();
        }

        uint32 GetData(uint32 data) const override
        {
            if (data == me->GetEntry())
                return (_currentStep < 7) ? 1 : 0;
            return 0;
        }

        void JustSummoned(Creature* summon) override
        {
            summons.Summon(summon);
        }

        void KilledUnit(Unit* victim) override
        {
            if (!me->IsAlive() || !victim->HasAura(SPELL_LEECH_POISON))
                return;

            me->ModifyHealth(int32(me->CountPctFromMaxHealth(10)));
        }

        void JustDied(Unit* killer) override
        {
            BossAI::JustDied(killer);
        }

        void JustEngagedWith(Unit*) override
        {
            _combatStarted = true;
            events.RescheduleEvent(EVENT_HADRONOX_ACID, 10s);
            events.RescheduleEvent(EVENT_HADRONOX_LEECH, 4s);
            events.RescheduleEvent(EVENT_HADRONOX_PIERCE, 1s);
            events.RescheduleEvent(EVENT_HADRONOX_GRAB, 15s);
        }

        void DamageTaken(Unit* who, uint32& damage, DamageEffectType /*damageType*/, SpellSchoolMask /*damageSchoolMask*/) override
        {
            if (who && who->IsControlledByPlayer() && !_playerAttacked)
            {
                _playerAttacked = true;

                if (HADRONOX_STOP_ASCENT_ON_PLAYER_DAMAGE)
                {
                    me->GetMotionMaster()->Clear();
                    _waitingForNextStep = false;
                    me->RemoveAurasDueToSpell(35340);
                }

                if (PLAYER_DMG_ON_HADRONOX_STOPS_ADD_SUMMONS)
                {
                    _spawnsActive = false;
                    events.CancelEvent(EVENT_HADRONOX_SUMMON_ADD);
                    me->GetMotionMaster()->Clear();

                    std::list<Creature*> cl;
                    me->GetCreaturesWithEntryInRange(cl, 30.0f, 28922);
                    for (std::list<Creature*>::iterator itr = cl.begin(); itr != cl.end(); ++itr)
                        (*itr)->RemoveAurasDueToSpell(SPELL_LEECH_POISON);

                    cl.clear();
                    me->GetCreaturesWithEntryInRange(cl, 30.0f, NPC_ANUB_AR_CHAMPION);
                    for (std::list<Creature*>::iterator itr = cl.begin(); itr != cl.end(); ++itr)
                        (*itr)->RemoveAurasDueToSpell(SPELL_LEECH_POISON);

                    cl.clear();
                    me->GetCreaturesWithEntryInRange(cl, 30.0f, NPC_ANUB_AR_NECROMANCER);
                    for (std::list<Creature*>::iterator itr = cl.begin(); itr != cl.end(); ++itr)
                        (*itr)->RemoveAurasDueToSpell(SPELL_LEECH_POISON);

                    cl.clear();
                    me->GetCreaturesWithEntryInRange(cl, 30.0f, NPC_ANUB_AR_CRYPTFIEND);
                    for (std::list<Creature*>::iterator itr = cl.begin(); itr != cl.end(); ++itr)
                        (*itr)->RemoveAurasDueToSpell(SPELL_LEECH_POISON);

                    me->RemoveAurasDueToSpell(SPELL_LEECH_POISON);
                }
            }

            if ((!who || !who->IsControlledByPlayer()) && me->HealthBelowPct(70))
            {
                if (me->HealthBelowPctDamaged(5, damage))
                    damage = 0;
                else
                    damage *= (me->GetHealthPct() - 5.0f) / 65.0f;
            }
        }

        bool AnyPlayerBelowWalkTrigger() const
        {
            Map::PlayerList const& playerList = me->GetMap()->GetPlayers();
            for (Map::PlayerList::const_iterator itr = playerList.begin(); itr != playerList.end(); ++itr)
            {
                Player* player = itr->GetSource();
                if (!player || player->IsGameMaster())
                    continue;
                if (player->GetPositionY() < GAUNTLET_MAX_Y && player->GetPositionZ() > GAUNTLET_END_Z && player->GetPositionZ() < GAUNTLET_START_Z)
                    return true;
            }
            return false;
        }

        bool AnyPlayerInHadronoxUpperGauntlet() const
        {
            Map::PlayerList const& playerList = me->GetMap()->GetPlayers();
            for (Map::PlayerList::const_iterator itr = playerList.begin(); itr != playerList.end(); ++itr)
            {
                Player* player = itr->GetSource();
                if (!player || player->IsGameMaster())
                    continue;
                float z = player->GetPositionZ();
                if (player->IsAlive() && z > GAUNTLET_END_Z && z < 785.0f)
                    return true;
            }
            return false;
        }

        bool AnyPlayerInHadronoxGauntlet() const
        {
            Map::PlayerList const& playerList = me->GetMap()->GetPlayers();
            for (Map::PlayerList::const_iterator itr = playerList.begin(); itr != playerList.end(); ++itr)
            {
                Player* player = itr->GetSource();
                if (!player || player->IsGameMaster())
                    continue;
                float z = player->GetPositionZ();
                float y = player->GetPositionY();
                if (player->IsAlive() && y < GAUNTLET_MAX_Y && z > GAUNTLET_END_Z)
                    return true;
            }
            return false;
        }

        bool AnyPlayerValid() const
        {
            Map::PlayerList const& playerList = me->GetMap()->GetPlayers();
            for (Map::PlayerList::const_iterator itr = playerList.begin(); itr != playerList.end(); ++itr)
                if (me->GetDistance(itr->GetSource()) < 130.0f && itr->GetSource()->IsAlive() && !itr->GetSource()->IsGameMaster() && me->CanCreatureAttack(itr->GetSource()))
                    return true;

            return false;
        }

        void CastWebGrabOnValidTargets()
        {
            if (WEB_GRAB_OVERRIDE)
            {
                std::list<Unit*> targets;

                Map::PlayerList const& playerList = me->GetMap()->GetPlayers();
                for (Map::PlayerList::const_iterator itr = playerList.begin(); itr != playerList.end(); ++itr)
                {
                    Player* player = itr->GetSource();
                    if (!player || !player->IsAlive() || player->IsGameMaster())
                        continue;
                    if (!IsValidSpellTarget(me, player))
                        continue;
                    targets.push_back(player);
                }

                for (uint32 entry : {(uint32)NPC_ANUB_AR_CRUSHER, (uint32)NPC_ANUB_AR_CHAMPION, (uint32)NPC_ANUB_AR_CRYPTFIEND, (uint32)NPC_ANUB_AR_NECROMANCER})
                {
                    std::list<Creature*> cl;
                    me->GetCreaturesWithEntryInRange(cl, SPELL_MAX_DIST, entry);
                    for (Creature* c : cl)
                    {
                        if (!c->IsAlive())
                            continue;
                        if (!IsValidSpellTarget(me, c))
                            continue;
                        targets.push_back(c);
                    }
                }

                for (Unit* target : targets)
                {
                    me->CastSpell(target, SPELL_WEB_GRAB_OVERRIDE, false);
                    me->AddThreat(target, 1.0f);
                }
            }
            else
            {
                me->CastSpell(me, SPELL_WEB_GRAB, false);
            }
        }

        float GetDistXY(const Position& a, const Position& b) const
        {
            float dx = a.GetPositionX() - b.GetPositionX();
            float dy = a.GetPositionY() - b.GetPositionY();
            return std::sqrt(dx * dx + dy * dy);
        }

        void UpdateAI(uint32 diff) override
        {
            events.Update(diff);

            _movementCheckTimer += diff;
            if (_movementCheckTimer >= 200)
            {
                _movementCheckTimer = 0;

                if (_walkStarted && _currentStep >= 0 && _currentStep < 8)
                {
                    const Position& dest = hadronoxWaypoints[_currentStep];
                    float distToDest = me->GetExactDist(dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ());

                    if (!_waitingForNextStep && distToDest <= STEP_REACH)
                    {
                        _currentStep++;
                        if (_currentStep < 8)
                        {
                            if (_currentStep >= 4)
                            {
                                _waitingForNextStep = true;
                                events.ScheduleEvent(EVENT_HADRONOX_NEXT_WAYPOINT, 15s);
                                if (Aura* aura = me->AddAura(35340, me))
                                    aura->SetDuration(15000);
                            }
                            else
                                MoveToWaypoint(_currentStep);
                        }
                        else
                        {
                            _reachedFinalWaypoint = true;
                            _leashPos = me->GetPosition();
                            Map::PlayerList const& playerList = me->GetMap()->GetPlayers();
                            for (Map::PlayerList::const_iterator itr = playerList.begin(); itr != playerList.end(); ++itr)
                            {
                                Player* player = itr->GetSource();
                                if (!player || !player->IsAlive() || player->IsGameMaster())
                                    continue;
                                float z = player->GetPositionZ();
                                float y = player->GetPositionY();
                                if (y < GAUNTLET_MAX_Y && z > GAUNTLET_END_Z)
                                {
                                    me->AddThreat(player, 1.0f);
                                    me->SetInCombatWith(player);
                                    player->SetInCombatWith(me);
                                }
                            }
                        }
                    }
                    else if (!_waitingForNextStep)
                    {
                        float movedDist = me->GetExactDist(_lastPos.GetPositionX(), _lastPos.GetPositionY(), _lastPos.GetPositionZ());
                        if (movedDist < 0.1f)
                            MoveToWaypoint(_currentStep);
                        _lastPos = me->GetPosition();
                    }
                }

                if (!_walkStarted)
                {
                    if (me->GetExactDist(HADRONOX_SPAWN_POS.GetPositionX(), HADRONOX_SPAWN_POS.GetPositionY(), HADRONOX_SPAWN_POS.GetPositionZ()) > 2.0f)
                        me->GetMotionMaster()->MovePoint(0, HADRONOX_SPAWN_POS.GetPositionX(), HADRONOX_SPAWN_POS.GetPositionY(), HADRONOX_SPAWN_POS.GetPositionZ(), FORCED_MOVEMENT_NONE, 0.f, 0.f, false);
                }
            }

            if (_reachedFinalWaypoint && me->IsInCombat())
            {
                Unit* topThreat = me->GetThreatMgr().GetCurrentVictim();
                bool fightingNpc = topThreat && !topThreat->IsControlledByPlayer() && (
                    topThreat->GetEntry() == NPC_ANUB_AR_CRUSHER ||
                    topThreat->GetEntry() == NPC_ANUB_AR_CHAMPION ||
                    topThreat->GetEntry() == NPC_ANUB_AR_NECROMANCER ||
                    topThreat->GetEntry() == NPC_ANUB_AR_CRYPTFIEND);

                if (fightingNpc)
                {
                    _leashCheckTimer += diff;
                    if (_leashCheckTimer >= HADRONOX_LEASH_CHECK)
                    {
                        _leashCheckTimer = 0;
                        if (GetDistXY(me->GetPosition(), _leashPos) > HADRONOX_LEASH_RANGE)
                        {
                            me->GetMotionMaster()->MovePoint(0,
                                _leashPos.GetPositionX(),
                                _leashPos.GetPositionY(),
                                _leashPos.GetPositionZ(),
                                FORCED_MOVEMENT_RUN);
                        }
                    }
                }
                else if (me->GetThreatMgr().GetThreatList().empty())
                    me->GetMotionMaster()->Clear();
                else if (Unit* top = me->GetThreatMgr().GetCurrentVictim())
                {
                    Player* player = top->ToPlayer();
                    if (player && player->GetPositionY() < GAUNTLET_MAX_Y && player->GetPositionZ() > GAUNTLET_END_Z)
                    {
                        if (!me->isMoving())
                            me->GetMotionMaster()->MoveChase(top);
                    }
                    else
                        me->AI()->EnterEvadeMode();
                }
            }

            switch (uint32 eventId = events.ExecuteEvent())
            {
                case EVENT_HADRONOX_CHECK:
                    if (me->IsAlive() && instance->IsBossDone(DATA_KRIKTHIR))
                    {
                        if (AnyPlayerInHadronoxUpperGauntlet())
                        {
                            if (!_startedSummons)
                            {
                                _startedSummons = true;
                                StartSummonEvents();
                            }
                        }

                        if (AnyPlayerInHadronoxGauntlet())
                        {
                            if (!_walkStarted && AnyPlayerBelowWalkTrigger())
                                StartWalkEvent();
                        }
                        else if (_walkStarted || _combatStarted || instance->GetBossState(DATA_HADRONOX) == IN_PROGRESS)
                            me->AI()->EnterEvadeMode();
                    }
                    events.ScheduleEvent(EVENT_HADRONOX_CHECK, 2s);
                    break;
                case EVENT_HADRONOX_STEP:
                    _currentStep = 0;
                    MoveToWaypoint(0);
                    break;
                case EVENT_HADRONOX_NEXT_WAYPOINT:
                    _waitingForNextStep = false;
                    me->RemoveAurasDueToSpell(35340);
                    Talk(SAY_HADRONOX_EMOTE);
                    if (_currentStep < 8)
                        MoveToWaypoint(_currentStep);
                    break;
                case EVENT_HADRONOX_SUMMON_ADD:
                {
                    // Using weight from original Hadronox script (2 champions, 3 necromancers, 6 cryptfiends per 30 sec)
                    uint32 roll = urand(0, 11);
                    uint32 entry = (roll < 2) ? NPC_ANUB_AR_CHAMPION : (roll < 5) ? NPC_ANUB_AR_NECROMANCER : NPC_ANUB_AR_CRYPTFIEND;
                    SummonAdd(entry);
                    events.ScheduleEvent(EVENT_HADRONOX_SUMMON_ADD, 2s);
                    break;
                }
                case EVENT_HADRONOX_PIERCE:
                    if (UpdateVictim() && !me->HasUnitState(UNIT_STATE_CASTING))
                        if (me->GetVictim() && IsValidSpellTarget(me, me->GetVictim()))
                            me->CastSpell(me->GetVictim(), SPELL_PIERCE_ARMOR, false);
                    events.ScheduleEvent(EVENT_HADRONOX_PIERCE, 8s);
                    break;
                case EVENT_HADRONOX_ACID:
                    if (UpdateVictim() && !me->HasUnitState(UNIT_STATE_CASTING))
                        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100, false))
                            if (IsValidSpellTarget(me, target))
                                me->CastSpell(target, SPELL_ACID_CLOUD, false);
                    events.ScheduleEvent(EVENT_HADRONOX_ACID, 25s);
                    break;
                case EVENT_HADRONOX_LEECH:
                    if (UpdateVictim() && !me->HasUnitState(UNIT_STATE_CASTING))
                        if (IsValidSpellTarget(me, me->GetVictim()))
                            me->CastSpell(me, SPELL_LEECH_POISON, false);
                    events.ScheduleEvent(EVENT_HADRONOX_LEECH, 12s);
                    break;
                case EVENT_HADRONOX_GRAB:
                    if (UpdateVictim() && !me->HasUnitState(UNIT_STATE_CASTING))
                        CastWebGrabOnValidTargets();
                    events.ScheduleEvent(EVENT_HADRONOX_GRAB, 25s);
                    break;
            }

            if (!UpdateVictim())
                return;

            DoMeleeAttackIfReady();
        }

        bool CheckEvadeIfOutOfCombatArea() const override
        {
            return me->isActiveObject() && !AnyPlayerValid();
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetAzjolNerubAI<boss_hadronoxAI>(creature);
    }
};

class npc_anub_ar_crusher : public CreatureScript
{
public:
    npc_anub_ar_crusher() : CreatureScript("npc_anub_ar_crusher") { }

    struct npc_anub_ar_crusherAI : public ScriptedAI
    {
        npc_anub_ar_crusherAI(Creature* c) : ScriptedAI(c), summons(me)
        {
            _eventStarted = false;
            _isSpawnedCrusher = me->ToTempSummon() && me->ToTempSummon()->GetSummonerUnit() && me->ToTempSummon()->GetSummonerUnit()->GetEntry() == NPC_ANUB_AR_CRUSHER;
            _pathStep = 0;
            _pathCheckTimer = 0;
            _finalDest = {0.0f, 0.0f, 0.0f, 0.0f};
        }

        EventMap events;
        SummonList summons;
        bool _eventStarted;
        bool _isSpawnedCrusher;
        uint32 _pathStep;
        uint32 _pathCheckTimer;
        Position _finalDest;

        void Reset() override
        {
            summons.DespawnAll();
            events.Reset();
            _eventStarted = false;
            if (!_isSpawnedCrusher)
                _pathStep = 0;
        }

        void JustSummoned(Creature* summon) override
        {
            summons.Summon(summon);
        }

        void SetDestination(const Position& dest)
        {
            _finalDest = dest;
            _pathStep = 1;
            me->GetMotionMaster()->MovePoint(1,
                addWaypoints[0].GetPositionX(),
                addWaypoints[0].GetPositionY(),
                addWaypoints[0].GetPositionZ(),
                FORCED_MOVEMENT_RUN);
        }

        void ScheduleAddSpawns()
        {
            events.ScheduleEvent(EVENT_CRUSHER_SPAWN_ADD1, 250ms);
            events.ScheduleEvent(EVENT_CRUSHER_SPAWN_ADD2, 500ms);
            events.ScheduleEvent(EVENT_CRUSHER_SPAWN_ADD3, 750ms);
            events.ScheduleEvent(EVENT_CRUSHER_SPAWN_ADD4, 1000ms);
        }

        // Two slots in HadronoxAddSlots::Positions with the greatest distance between them
        void GetFarthestSlotPair(int32& a, int32& b)
        {
            float bestDist = -1.0f;
            a = 0;
            b = 1;
            for (int i = 0; i < 4; ++i)
            {
                for (int j = i + 1; j < 4; ++j)
                {
                    float d = HadronoxAddSlots::Positions[i].GetExactDist(HadronoxAddSlots::Positions[j]);
                    if (d > bestDist)
                    {
                        bestDist = d;
                        a = i;
                        b = j;
                    }
                }
            }
        }

        // Two slots in HadronoxAddSlots::Positions with the smallest distance between them
        void GetClosestSlotPair(int32& a, int32& b)
        {
            float bestDist = FLT_MAX;
            a = 0;
            b = 1;
            for (int i = 0; i < 4; ++i)
            {
                for (int j = i + 1; j < 4; ++j)
                {
                    if (HadronoxAddSlots::Occupied[i] || HadronoxAddSlots::Occupied[j])
                        continue;
                    float d = HadronoxAddSlots::Positions[i].GetExactDist(HadronoxAddSlots::Positions[j]);
                    if (d < bestDist)
                    {
                        bestDist = d;
                        a = i;
                        b = j;
                    }
                }
            }
        }

        int32 PickUnoccupiedFromPair(int32 a, int32 b)
        {
            if (!HadronoxAddSlots::Occupied[a])
                return a;
            if (!HadronoxAddSlots::Occupied[b])
                return b;
            return -1;
        }

        void SpawnAddAt(uint32 spawnPointIndex, uint32 entry, int32 slotIndex)
        {
            if (slotIndex < 0)
                return;

            const Position& spawnPos = addSpawnPos[spawnPointIndex];
            HadronoxAddSlots::Occupied[slotIndex] = true;
            Creature* add = me->SummonCreature(entry,
                spawnPos.GetPositionX(), spawnPos.GetPositionY(), spawnPos.GetPositionZ(),
                0.0f, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 10000);

            if (add)
            {
                add->AI()->SetData(1, (uint32)slotIndex);
                static_cast<npc_hadronox_addAI*>(add->AI())->StartCrusherPath(HadronoxAddSlots::Positions[slotIndex]);
            }
        }

        void PullNearbyAdds()
        {
            std::list<Creature*> nearbyAdds;
            for (uint32 entry : {(uint32)NPC_ANUB_AR_CHAMPION, (uint32)NPC_ANUB_AR_NECROMANCER, (uint32)NPC_ANUB_AR_CRYPTFIEND})
            {
                std::list<Creature*> cl;
                me->GetCreaturesWithEntryInRange(cl, 10.0f, entry);
                nearbyAdds.merge(cl);
            }
            for (Creature* add : nearbyAdds)
            {
                if (!add->IsAlive())
                    continue;
                npc_hadronox_addAI* addAI = static_cast<npc_hadronox_addAI*>(add->AI());
                if (!addAI || addAI->_attackedByPlayer)
                    continue;
                if (TempSummon* ts = add->ToTempSummon())
                    if (Unit* summoner = ts->GetSummonerUnit())
                        if (summoner->GetEntry() == NPC_HADRONOX && addAI->_spawnedAbove735)
                            continue;
                addAI->PullIntoCombat(me);
            }
        }

        void JustEngagedWith(Unit*) override
        {
            if (_isSpawnedCrusher)
            {
                _pathStep = 0;
                me->GetMotionMaster()->Clear();
            }
            else
            {
                if (Creature* hadronox = me->FindNearestCreature(NPC_HADRONOX, 500.0f, true))
                    hadronox->AI()->DoAction(ACTION_CRUSHER_AGGRO_SAY);

                if (Creature* hadronox = me->FindNearestCreature(NPC_HADRONOX, 500.0f, true))
                    hadronox->AI()->DoAction(ACTION_START_WALK);

                const Position dest1 = {521.5181f,  563.6016f, 733.81036f, 0.0f};
                const Position dest2 = {539.27893f, 564.9075f, 731.92096f, 0.0f};

                Creature* c1 = me->SummonCreature(NPC_ANUB_AR_CRUSHER,
                    addSpawnPos[0].GetPositionX(), addSpawnPos[0].GetPositionY(), addSpawnPos[0].GetPositionZ(), 0.0f,
                    TEMPSUMMON_CORPSE_TIMED_DESPAWN, 10000);
                Creature* c2 = me->SummonCreature(NPC_ANUB_AR_CRUSHER,
                    addSpawnPos[1].GetPositionX(), addSpawnPos[1].GetPositionY(), addSpawnPos[1].GetPositionZ(), 0.0f,
                    TEMPSUMMON_CORPSE_TIMED_DESPAWN, 10000);

                if (c1 && c2)
                {
                    float c1DistToDest1 = c1->GetExactDist(dest1);
                    float c1DistToDest2 = c1->GetExactDist(dest2);

                    const Position& assignDest1 = (c1DistToDest1 <= c1DistToDest2) ? dest1 : dest2;
                    const Position& assignDest2 = (c1DistToDest1 <= c1DistToDest2) ? dest2 : dest1;

                    static_cast<npc_anub_ar_crusherAI*>(c1->AI())->SetDestination(assignDest1);
                    static_cast<npc_anub_ar_crusherAI*>(c2->AI())->SetDestination(assignDest2);
                }
                else
                {
                    if (c1)
                        static_cast<npc_anub_ar_crusherAI*>(c1->AI())->SetDestination(dest1);
                    if (c2)
                        static_cast<npc_anub_ar_crusherAI*>(c2->AI())->SetDestination(dest2);
                }

                ScheduleAddSpawns();
            }

            events.ScheduleEvent(EVENT_CRUSHER_SMASH, 8s, 0, 0);
            events.ScheduleEvent(EVENT_CHECK_HEALTH, 1s);

            PullNearbyAdds();
        }

        void EnterEvadeMode(EvadeReason /*why*/) override
        {
            summons.DespawnAll();
            if (!_isSpawnedCrusher)
                HadronoxAddSlots::Reset();

            if (!_isSpawnedCrusher)
            {
                if (Creature* hadronox = me->FindNearestCreature(NPC_HADRONOX, 500.0f, true))
                    hadronox->AI()->DoAction(ACTION_CRUSHER_EVADE);
            }
            else
                me->DespawnOrUnsummon();

            ScriptedAI::EnterEvadeMode();
        }

        void UpdateAI(uint32 diff) override
        {
            if (_isSpawnedCrusher && _pathStep > 0)
            {
                _pathCheckTimer += diff;
                if (_pathCheckTimer >= 200)
                {
                    _pathCheckTimer = 0;

                    if (_pathStep == 1)
                    {
                        if (me->GetExactDist(addWaypoints[0]) <= WAYPOINT_REACH)
                        {
                            _pathStep = 2;
                            me->GetMotionMaster()->MovePoint(2,
                                _finalDest.GetPositionX(),
                                _finalDest.GetPositionY(),
                                _finalDest.GetPositionZ(),
                                FORCED_MOVEMENT_RUN);
                        }
                        else if (!me->isMoving())
                        {
                            me->GetMotionMaster()->MovePoint(1,
                                addWaypoints[0].GetPositionX(),
                                addWaypoints[0].GetPositionY(),
                                addWaypoints[0].GetPositionZ(),
                                FORCED_MOVEMENT_RUN);
                        }
                    }
                    else if (_pathStep == 2)
                    {
                        if (me->GetExactDist(_finalDest) <= WAYPOINT_REACH)
                            _pathStep = 0;
                        else if (!me->isMoving())
                        {
                            me->GetMotionMaster()->MovePoint(2,
                                _finalDest.GetPositionX(),
                                _finalDest.GetPositionY(),
                                _finalDest.GetPositionZ(),
                                FORCED_MOVEMENT_RUN);
                        }
                    }
                }
            }

            if (!UpdateVictim())
                return;

            events.Update(diff);
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;

            switch (events.ExecuteEvent())
            {
                case EVENT_CRUSHER_SMASH:
                    if (me->GetVictim() && IsValidSpellTarget(me, me->GetVictim()))
                        me->CastSpell(me->GetVictim(), SPELL_SMASH, false);
                    events.ScheduleEvent(EVENT_CRUSHER_SMASH, 15s);
                    break;
                case EVENT_CHECK_HEALTH:
                    if (me->HealthBelowPct(30))
                    {
                        Talk(SAY_CRUSHER_EMOTE);
                        me->CastSpell(me, SPELL_FRENZY, false);
                        break;
                    }
                    events.ScheduleEvent(EVENT_CHECK_HEALTH, 1s);
                    break;
                case EVENT_CRUSHER_SPAWN_ADD1:
                    if (!_isSpawnedCrusher)
                    {
                        int32 a, b;
                        GetFarthestSlotPair(a, b);
                        SpawnAddAt(0, NPC_ANUB_AR_CRYPTFIEND, PickUnoccupiedFromPair(a, b));
                    }
                    break;
                case EVENT_CRUSHER_SPAWN_ADD2:
                    if (!_isSpawnedCrusher)
                    {
                        int32 a, b;
                        GetFarthestSlotPair(a, b);
                        SpawnAddAt(1, NPC_ANUB_AR_CRYPTFIEND, PickUnoccupiedFromPair(a, b));
                    }
                    break;
                case EVENT_CRUSHER_SPAWN_ADD3:
                    if (!_isSpawnedCrusher)
                    {
                        int32 a, b;
                        GetClosestSlotPair(a, b);
                        SpawnAddAt(0, NPC_ANUB_AR_NECROMANCER, PickUnoccupiedFromPair(a, b));
                    }
                    break;
                case EVENT_CRUSHER_SPAWN_ADD4:
                    if (!_isSpawnedCrusher)
                    {
                        int32 a, b;
                        GetClosestSlotPair(a, b);
                        SpawnAddAt(1, NPC_ANUB_AR_NECROMANCER, PickUnoccupiedFromPair(a, b));
                    }
                    break;
            }

            DoMeleeAttackIfReady();
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetAzjolNerubAI<npc_anub_ar_crusherAI>(creature);
    }
};

class spell_hadronox_leech_poison_aura : public AuraScript
{
    PrepareAuraScript(spell_hadronox_leech_poison_aura);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_LEECH_POISON_HEAL });
    }

    void HandleEffectRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (GetTargetApplication()->GetRemoveMode() == AURA_REMOVE_BY_DEATH)
            if (Unit* caster = GetCaster())
                caster->CastSpell(caster, SPELL_LEECH_POISON_HEAL, true);
    }

    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_hadronox_leech_poison_aura::HandleEffectRemove, EFFECT_0, SPELL_AURA_PERIODIC_LEECH, AURA_EFFECT_HANDLE_REAL);
    }
};

class achievement_hadronox_denied : public AchievementCriteriaScript
{
public:
    achievement_hadronox_denied() : AchievementCriteriaScript("achievement_hadronox_denied")
    {
    }

    bool OnCheck(Player* /*player*/, Unit* target, uint32 /*criteria_id*/) override
    {
        if (!target)
            return false;

        return target->GetAI()->GetData(target->GetEntry());
    }
};

void AddSC_boss_hadronox()
{
    new npc_anub_ar_champion();
    new npc_anub_ar_necromancer();
    new npc_anub_ar_cryptfiend();
    new boss_hadronox();
    new npc_anub_ar_crusher();
    RegisterSpellScript(spell_hadronox_leech_poison_aura);
    new achievement_hadronox_denied();
}