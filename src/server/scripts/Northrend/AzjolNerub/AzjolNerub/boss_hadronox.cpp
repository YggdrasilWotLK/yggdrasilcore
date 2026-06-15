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

enum Spells
{
    SPELL_SUMMON_ANUBAR_CHAMPION            = 53064,
    SPELL_SUMMON_ANUBAR_CRYPT_FIEND         = 53065,
    SPELL_SUMMON_ANUBAR_NECROMANCER         = 53066,
    SPELL_WEB_FRONT_DOORS                   = 53177,
    SPELL_WEB_SIDE_DOORS                    = 53185,
    SPELL_ACID_CLOUD                        = 53400,
    SPELL_LEECH_POISON                      = 53030,
    SPELL_LEECH_POISON_HEAL                 = 53800,
    SPELL_WEB_GRAB                          = 56640,
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
    EVENT_HADRONOX_SUMMON_CHAMPION  = 11,
    EVENT_HADRONOX_SUMMON_NECRO     = 12,
    EVENT_HADRONOX_SUMMON_CRYPT     = 13,
    EVENT_HADRONOX_STEP             = 14,

    EVENT_CRUSHER_SMASH             = 20,
    EVENT_CHECK_HEALTH              = 21,

    EVENT_CHAMPION_REND             = 1,
    EVENT_CHAMPION_PUMMEL           = 2,

    EVENT_NECRO_INFECTED_WOUND      = 1,
    EVENT_NECRO_CRUSHING_WEBS       = 2,

    EVENT_CRYPT_SHADOW_BOLT         = 1
};

enum Misc
{
    NPC_ANUB_AR_CRUSHER         = 28922,

    SAY_CRUSHER_AGGRO           = 0,
    SAY_CRUSHER_EMOTE           = 1,
    SAY_HADRONOX_EMOTE          = 0,

    ACTION_START_EVENT          = 2,
    ACTION_START_WALK           = 3,
    ACTION_STOP_SPAWNS          = 4,
    ACTION_CRUSHER_EVADE        = 5
};

constexpr float GAUNTLET_END_Z   = 640.0f;
constexpr float GAUNTLET_START_Z = 730.0f;
constexpr float STEP_REACH       = 3.0f;
constexpr float STEP_SPEED       = 3.5f;
constexpr float ADDS_RANGE       = 8.0f;
constexpr float SPELL_MAX_DIST   = 40.0f;
constexpr float SPELL_MAX_Z_DIFF = 20.0f;
constexpr float WAYPOINT_REACH   = 1.0f;
constexpr uint32 ADD_CHECK_MS    = 100;
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

const Position addSpawnPos[2] =
{
    {577.2f, 613.45f, 771.51f, 0.0f},
    {483.68f, 612.75f, 771.42f, 0.0f}
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
        _spawnedAbove745 = creature->GetPositionZ() >= 745.0f;
        _chasingHadronox = false;
        _checkTimer = 0;
    }

    uint32 _currentWaypoint;
    bool _reachedHadronox;
    bool _attackedByPlayer;
    bool _spawnedAbove745;
    bool _chasingHadronox;
    uint32 _checkTimer;
    EventMap events;

    bool IsHeroic() const { return me->GetMap()->IsHeroic(); }

    void Reset() override
    {
        _currentWaypoint = 0;
        _reachedHadronox = false;
        _attackedByPlayer = false;
        _chasingHadronox = false;
        _checkTimer = 0;
        events.Reset();
        if (_spawnedAbove745)
            IssueMove();
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

    // Issues MovePoint to current waypoint, or MovePoint to Hadronox if closer.
    void IssueMove()
    {
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
            me->GetMotionMaster()->Clear();
            me->SetReactState(REACT_AGGRESSIVE);
            if (who->IsPlayer())
                AttackStart(who);
            ScheduleCombatEvents();
        }
    }

    void JustEngagedWith(Unit* /*who*/) override {}

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
        if (!_spawnedAbove745 || _reachedHadronox || _attackedByPlayer)
            return;

        Creature* hadronox = FindHadronox();

        if (hadronox && me->GetExactDist(hadronox) <= ADDS_RANGE)
        {
            EngageHadronox(hadronox);
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
            me->DespawnOrUnsummon(10000ms);
        }

        void UpdateAI(uint32 diff) override
        {
            npc_hadronox_addAI::UpdateAI(diff);

            if (_spawnedAbove745 && !ShouldUseCombatAbilities())
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
            me->DespawnOrUnsummon(10000ms);
        }

        void UpdateAI(uint32 diff) override
        {
            npc_hadronox_addAI::UpdateAI(diff);

            if (_spawnedAbove745 && !ShouldUseCombatAbilities())
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
            me->DespawnOrUnsummon(10000ms);
        }

        void UpdateAI(uint32 diff) override
        {
            npc_hadronox_addAI::UpdateAI(diff);

            if (_spawnedAbove745 && !ShouldUseCombatAbilities())
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
            _currentStep = -1;
            _spawnCount = 0;
            _movementCheckTimer = 0;
            _lastPos = creature->GetPosition();
        }

        bool _walkStarted;
        bool _spawnsActive;
        bool _combatStarted;
        bool _playerAttacked;
        bool _crusherAggroSaid;
        int32 _currentStep;
        uint32 _spawnCount;
        uint32 _movementCheckTimer;
        Position _lastPos;

        void Reset() override
        {
            BossAI::Reset();
            _walkStarted = false;
            _spawnsActive = true;
            _combatStarted = false;
            _playerAttacked = false;
            _crusherAggroSaid = false;
            _currentStep = -1;
            _spawnCount = 0;
            _movementCheckTimer = 0;
            _lastPos = me->GetPosition();
            me->SummonCreature(NPC_ANUB_AR_CRUSHER, 531.5281f, 553.8509f, 732.56f, 5.053f);
            me->SummonCreature(NPC_ANUB_AR_CRYPTFIEND, 524.07886f, 550.6797f, 731.873f, 5.053f);
            me->SummonCreature(NPC_ANUB_AR_CHAMPION, 541.0999f, 555.21924f, 732.152f, 5.053f);
            events.ScheduleEvent(EVENT_HADRONOX_CHECK, 1s);
        }

        void MoveToWaypoint(int32 index)
        {
            if (index < 0 || index >= 8)
                return;
            if (index == 6)
                me->CastSpell(me, SPELL_WEB_FRONT_DOORS, true);
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
            events.ScheduleEvent(EVENT_HADRONOX_SUMMON_CHAMPION, 15s);
            events.ScheduleEvent(EVENT_HADRONOX_SUMMON_NECRO, 10s);
            events.ScheduleEvent(EVENT_HADRONOX_SUMMON_CRYPT, 5s);
        }

        Position GetNextSpawnPos()
        {
            return addSpawnPos[_spawnCount % 2];
        }

        void SummonAdd(uint32 entry)
        {
            if (!_spawnsActive)
                return;
            Position pos = GetNextSpawnPos();
            me->SummonCreature(entry, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), 0.0f, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5000);
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
        }

        void EnterEvadeMode(EvadeReason /*why*/) override
        {
            if (_spawnsActive && AnyPlayerInHadronoxGauntlet())
                return;
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
                _spawnsActive = false;
                events.CancelEvent(EVENT_HADRONOX_STEP);
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
                if (player->GetPositionY() < 625.0f && player->GetPositionZ() > GAUNTLET_END_Z && player->GetPositionZ() < GAUNTLET_START_Z)
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
                if (player->IsAlive() && y < 625.0f && z > GAUNTLET_END_Z)
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
                me->CastSpell(target, SPELL_WEB_GRAB, false);
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

                    if (distToDest <= STEP_REACH)
                    {
                        _currentStep++;
                        if (_currentStep < 8)
                        {
                            if (_currentStep >= 4)
                                Talk(SAY_HADRONOX_EMOTE);
                            MoveToWaypoint(_currentStep);
                        }
                    }
                    else
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

            switch (uint32 eventId = events.ExecuteEvent())
            {
                case EVENT_HADRONOX_CHECK:
                    if (me->IsAlive() && instance->IsBossDone(DATA_KRIKTHIR))
                    {
                        if (AnyPlayerInHadronoxGauntlet())
                        {
                            if (!_crusherAggroSaid)
                            {
                                _crusherAggroSaid = true;
                                if (Creature* crusher = me->FindNearestCreature(NPC_ANUB_AR_CRUSHER, 300.0f, true))
                                    crusher->AI()->Talk(SAY_CRUSHER_AGGRO);
                                StartSummonEvents();
                            }
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
                case EVENT_HADRONOX_SUMMON_CHAMPION:
                    SummonAdd(NPC_ANUB_AR_CHAMPION);
                    events.ScheduleEvent(EVENT_HADRONOX_SUMMON_CHAMPION, 15s);
                    break;
                case EVENT_HADRONOX_SUMMON_NECRO:
                    SummonAdd(NPC_ANUB_AR_NECROMANCER);
                    events.ScheduleEvent(EVENT_HADRONOX_SUMMON_NECRO, 10s);
                    break;
                case EVENT_HADRONOX_SUMMON_CRYPT:
                    SummonAdd(NPC_ANUB_AR_CRYPTFIEND);
                    events.ScheduleEvent(EVENT_HADRONOX_SUMMON_CRYPT, 5s);
                    break;
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
                    events.ScheduleEvent(EVENT_HADRONOX_GRAB, 12s);
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
        }

        EventMap events;
        SummonList summons;
        bool _eventStarted;

        void Reset() override
        {
            summons.DespawnAll();
            events.Reset();
            _eventStarted = false;
        }

        void JustEngagedWith(Unit*) override
        {
            if (Creature* hadronox = me->FindNearestCreature(NPC_HADRONOX, 500.0f, true))
                hadronox->AI()->DoAction(ACTION_START_WALK);

            events.ScheduleEvent(EVENT_CRUSHER_SMASH, 8s, 0, 0);
            events.ScheduleEvent(EVENT_CHECK_HEALTH, 1s);
        }

        void EnterEvadeMode(EvadeReason /*why*/) override
        {
            if (Creature* hadronox = me->FindNearestCreature(NPC_HADRONOX, 500.0f, true))
                hadronox->AI()->DoAction(ACTION_CRUSHER_EVADE);

            ScriptedAI::EnterEvadeMode();
        }

        void UpdateAI(uint32 diff) override
        {
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