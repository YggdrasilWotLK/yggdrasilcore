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

#include "CreatureScript.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"
#include "ahnkahet.h"

enum Spells
{
    SPELL_MIND_FLAY                         = 57941,
    SPELL_SHADOW_BOLT_VOLLEY                = 57942,
    SPELL_SHIVER                            = 57949,

    SPELL_INSANITY                          = 57496, //Dummy
    INSANITY_VISUAL                         = 57561,
    SPELL_CLONE_PLAYER                      = 57507, //casted on player during insanity
    SPELL_INSANITY_PHASING_1                = 57508, // Phase 16
    SPELL_INSANITY_PHASING_2                = 57509, // Phase 32
    SPELL_INSANITY_PHASING_3                = 57510, // Phase 64
    SPELL_INSANITY_PHASING_4                = 57511, // Phase 128
    SPELL_INSANITY_PHASING_5                = 57512, // Phase 256

    SPELL_WHISPER_AGGRO                     = 60291,
    SPELL_WHISPER_INSANITY                  = 60292,
    SPELL_WHISPER_SLAY_1                    = 60293,
    SPELL_WHISPER_SLAY_2                    = 60294,
    SPELL_WHISPER_SLAY_3                    = 60295,
    SPELL_WHISPER_DEATH_1                   = 60296,
    SPELL_WHISPER_DEATH_2                   = 60297
};

enum Texts
{
    SAY_AGGRO                               = 0,
    SAY_INSANITY                            = 1,
    SAY_SLAY_1                              = 2,
    SAY_SLAY_2                              = 3,
    SAY_SLAY_3                              = 4,
    SAY_DEATH_1                             = 5,
    SAY_DEATH_2                             = 6,

    WHISPER_AGGRO                           = 7,
    WHISPER_INSANITY                        = 8,
    WHISPER_SLAY_1                          = 9,
    WHISPER_SLAY_2                          = 10,
    WHISPER_SLAY_3                          = 11,
    WHISPER_DEATH_1                         = 12,
    WHISPER_DEATH_2                         = 13
};

enum Misc
{
    NPC_TWISTED_VISAGE                      = 30625,
    ACHIEV_QUICK_DEMISE_START_EVENT         = 20382,

    MAX_INSANITY_TARGETS                    = 5,
    DATA_SET_INSANITY_PHASE                 = 1,
};

enum Events
{
    EVENT_HERALD_MIND_FLAY                  = 1,
    EVENT_HERALD_SHADOW,
    EVENT_HERALD_SHIVER,
};

const std::array<uint32, MAX_INSANITY_TARGETS> InsanitySpells = { SPELL_INSANITY_PHASING_1, SPELL_INSANITY_PHASING_2, SPELL_INSANITY_PHASING_3, SPELL_INSANITY_PHASING_4, SPELL_INSANITY_PHASING_5 };

struct boss_volazj : public BossAI
{
    boss_volazj(Creature* pCreature) : BossAI(pCreature, DATA_HERALD_VOLAZJ),
        insanityTimes(0),
        insanityQueue(0),
        insanityPhase(false),
        insanityCheckTimer(0)
    { }

    void InitializeAI() override
    {
        BossAI::InitializeAI();
    }

    void Reset() override
    {
        _Reset();
        me->RemoveUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
        me->SetControlled(false, UNIT_STATE_STUNNED);
        ResetPlayersPhaseMask();
        instance->DoStopTimedAchievement(ACHIEVEMENT_TIMED_TYPE_EVENT, ACHIEV_QUICK_DEMISE_START_EVENT);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        _JustEngagedWith();
        insanityTimes = 0;
        insanityQueue = 0;
        insanityPhase = false;
        insanityCheckTimer = 0; // Reset insanityCheckTimer
        
        events.ScheduleEvent(EVENT_HERALD_MIND_FLAY, 8s);
        events.ScheduleEvent(EVENT_HERALD_SHADOW, 5s);
        events.ScheduleEvent(EVENT_HERALD_SHIVER, 15s);
        Talk(SAY_AGGRO);
        DoCastSelf(SPELL_WHISPER_AGGRO);
        instance->DoStartTimedAchievement(ACHIEVEMENT_TIMED_TYPE_EVENT, ACHIEV_QUICK_DEMISE_START_EVENT);
        me->SetInCombatWithZone();
        me->SetPhaseMask((1 | 16 | 32 | 64 | 128 | 256), true);
    }

    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        me->RemoveUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
        me->SetControlled(false, UNIT_STATE_STUNNED);
        ResetPlayersPhaseMask();

        switch (urand(0, 1))
        {
            case 0:
                Talk(SAY_DEATH_1);
                DoCastSelf(SPELL_WHISPER_DEATH_1, true);
                break;
            case 1:
                Talk(SAY_DEATH_2);
                DoCastSelf(SPELL_WHISPER_DEATH_2, true);
                break;
        }
    }

    void KilledUnit(Unit* victim) override
    {
        if (victim->IsPlayer())
        {
            switch (urand(0, 2))
            {
                case 0:
                    Talk(SAY_SLAY_1);
                    DoCastSelf(SPELL_WHISPER_SLAY_1);
                    break;
                case 1:
                    Talk(SAY_SLAY_2);
                    DoCastSelf(SPELL_WHISPER_SLAY_2);
                    break;
                case 2:
                    Talk(SAY_SLAY_3);
                    DoCastSelf(SPELL_WHISPER_SLAY_3);
                    break;
            }
        }
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damagetype*/, SpellSchoolMask /*damageSchoolMask*/) override
    {
        if (me->HealthBelowPctDamaged(66, damage) && insanityTimes == 0)
        {
            if (!me->HasUnitState(UNIT_STATE_CASTING))
            {
                if (insanityQueue == 0)
                {
                    DoCastSelf(SPELL_INSANITY);
                    insanityQueue = 1;
                }
            }
        }
        else if (me->HealthBelowPctDamaged(33, damage) && insanityTimes == 1)
        {
            if (!me->HasUnitState(UNIT_STATE_CASTING))
            {
                if (insanityQueue == 0)
                {
                    DoCastSelf(SPELL_INSANITY);
                    insanityQueue = 1;
                }
            }
        }
    }

    void StartInsanity()
    {
        me->InterruptNonMeleeSpells(false);
        insanityPhase = true;
        insanityCheckTimer = 0;
        
        me->RemoveAllAuras();
        DoCastSelf(INSANITY_VISUAL, true);
        me->SetUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
        me->SetControlled(true, UNIT_STATE_STUNNED);

        std::list<Player*> playerList;
        Map::PlayerList const& players = me->GetMap()->GetPlayers();
        for (auto const& i : players)
        {
            if (Player* player = i.GetSource())
            {
                if (player->IsAlive() && player->GetDistance(me) < 400)
                    playerList.push_back(player);
            }
        }
        
        uint32 insanityCounter = 0;
        uint8 playerCount = std::min(static_cast<uint8>(5), static_cast<uint8>(playerList.size()));

        if (playerCount > 1)
        {
            // Step 1: Make all players spawn their clones before phasing, to ensure mirror image packet reaches all clients
            std::unordered_map<Player*, std::vector<Creature*>> playerClones;
            for (Player* activePlayer : playerList)
            {
                for (Player* target : playerList)
                {
                    if (target == activePlayer || !target->IsAlive() || !activePlayer->IsAlive()) // Don't clone own phase, and don't clone dead players
                        continue;

                    if (Creature* clone = me->SummonCreature(NPC_TWISTED_VISAGE, target->GetPosition(), TEMPSUMMON_CORPSE_DESPAWN))
                    {
                        activePlayer->CastSpell(clone, SPELL_CLONE_PLAYER, true);
                        clone->SetUInt32Value(UNIT_FIELD_MINDAMAGE, target->GetUInt32Value(UNIT_FIELD_MINDAMAGE));
                        clone->SetUInt32Value(UNIT_FIELD_MAXDAMAGE, target->GetUInt32Value(UNIT_FIELD_MAXDAMAGE));
                        playerClones[activePlayer].push_back(clone);
                    }
                }
            }

            // Step 2: Phase players
            for (Player* activePlayer : playerList)
            {
                if (!activePlayer->IsAlive()) // Failsafe to not phase dead players as the phase sticks after ress
                    break;

                if (insanityCounter >= MAX_INSANITY_TARGETS)
                    break;

                activePlayer->CastSpell(activePlayer, InsanitySpells[insanityCounter], true);
                ++insanityCounter;
            }

            // Step 3: Assign the appropriate phases to all clones and set them in combat with their targets
            uint32 phases1[] = {16, 32, 64, 128};
            uint32 phases2[] = {32, 64, 128, 256};
            uint32 phases3[] = {16, 64, 128, 256};
            uint32 phases4[] = {16, 32, 128, 256};
            uint32 phases5[] = {16, 32, 64, 256};

            uint32* phases = phases1;
            size_t phasesSize = 4;

            for (auto& [activePlayer, clones] : playerClones)
            {
                switch (activePlayer->GetPhaseMask())
                {
                    case 16: phases = phases2; break;
                    case 32: phases = phases3; break;
                    case 64: phases = phases4; break;
                    case 128: phases = phases5; break;
                    case 256: phases = phases1; break;
                    default: phases = phases1; break;
                }
                    
                for (size_t i = 0; i < clones.size() && i < phasesSize; ++i)
                {
                    Creature* clone = clones[i];
                    Player* target = *(std::next(playerList.begin(), i % playerList.size()));

                    clone->SetPhaseMask(phases[i], true);
                    clone->SetInCombatWith(target);
                    target->SetInCombatWith(clone);
                    clone->AddThreat(target, 0.0f);
                }
            }
        }

        Talk(SAY_INSANITY);
        DoCastSelf(SPELL_WHISPER_INSANITY, true);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;
        
        if (insanityPhase)
        {
            insanityCheckTimer += diff;
            if (insanityCheckTimer >= 2000) // Check insanity phase status every 2 seconds to reduce load
            {
                insanityCheckTimer = 0;

                uint32 phaseMasks[] = { 16, 32, 64, 128, 256 }; // Define the phase masks to check
                bool phaseHasCreatures[5] = { false };
                bool phaseHasPlayers[5] = { false };

                // Check for clones in each phase
                std::list<Creature*> summonedCreatures;
                me->GetCreatureListWithEntryInGrid(summonedCreatures, NPC_TWISTED_VISAGE, 100.0f);

                for (Creature* summon : summonedCreatures)
                {
                    for (uint32 i = 0; i < 5; ++i)
                    {
                        if (summon->GetPhaseMask() & phaseMasks[i])
                            phaseHasCreatures[i] = true;
                    }
                }

                // Check if any players are alive in each phase
                Map::PlayerList const& players = me->GetMap()->GetPlayers();
                for (auto const& i : players)
                {
                    if (Player* player = i.GetSource())
                    {
                        for (uint32 j = 0; j < 5; ++j)
                        {
                            if (player->GetPhaseMask() & phaseMasks[j] && player->IsAlive())
                                phaseHasPlayers[j] = true;
                        }
                    }
                }

                // Move players between phases if their phase has no clones left
                for (auto const& i : players)
                {
                    if (Player* player = i.GetSource())
                    {
                        uint32 currentPhaseMask = player->GetPhaseMask();
                        bool currentPhaseHasCreatures = false;
                        bool currentPhaseHasPlayers = false;

                        for (uint32 l = 0; l < 5; ++l)
                        {
                            if (currentPhaseMask & phaseMasks[l])
                            {
                                currentPhaseHasCreatures = phaseHasCreatures[l];
                                currentPhaseHasPlayers = phaseHasPlayers[l];
                                break;
                            }
                        }

                        // Condition 1: If the player is in a phase with no clones but other phases have clones
                        if (!currentPhaseHasCreatures)
                        {
                            for (uint32 m = 0; m < 5; ++m)
                            {
                                if (phaseHasCreatures[m])
                                {
                                    player->SetPhaseMask(phaseMasks[m], true);
                                    break;
                                }
                            }
                        }
                        
                        // Condition 2: If clones are in a phase with no living players, move players to that phase if those players' phase has no clones
                        else if (currentPhaseHasCreatures && !currentPhaseHasPlayers)
                        {
                            for (uint32 n = 0; n < 5; ++n)
                            {
                                if (phaseHasCreatures[n] && !phaseHasPlayers[n])
                                {
                                    player->SetPhaseMask(phaseMasks[n], true);
                                    break;
                                }
                            }
                        }
                    }
                }
                
                if (!phaseHasCreatures[0] && !phaseHasCreatures[1] && !phaseHasCreatures[2]
                && !phaseHasCreatures[3] && !phaseHasCreatures[4])
                {
                    // Reset all players to phase 1 and remove insanity phase spell
                    Map::PlayerList const& players = me->GetMap()->GetPlayers();
                    for (auto const& i : players)
                    {
                        if (Player* player = i.GetSource())
                            ResetPlayersPhaseMask();
                    }

                    insanityPhase = false;
                    me->RemoveUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
                    me->SetControlled(false, UNIT_STATE_STUNNED);
                    me->RemoveAurasDueToSpell(INSANITY_VISUAL);
                    insanityQueue = 0;
                }
            }
        }

        if ((insanityQueue == 1) && (!me->HasUnitState(UNIT_STATE_CASTING)))
        {
            insanityQueue = 2;
            StartInsanity();
            insanityTimes++;
        }
        
        events.Update(diff);

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_HERALD_MIND_FLAY:
                    DoCastVictim(SPELL_MIND_FLAY);
                    events.Repeat(20s);
                    break;
                case EVENT_HERALD_SHADOW:
                    DoCastVictim(SPELL_SHADOW_BOLT_VOLLEY);
                    events.Repeat(5s);
                    break;
                case EVENT_HERALD_SHIVER:
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random))
                        DoCast(target, SPELL_SHIVER);
                    events.Repeat(15s);
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }

private:
    uint8 insanityTimes;
    uint8 insanityQueue;
    bool insanityPhase;
    uint32 insanityCheckTimer; // Granulation to reduce load of expensive script

    uint32 GetPlrInsanityAuraId(uint32 phaseMask) const
    {
        switch (phaseMask)
        {
            case 16:
                return SPELL_INSANITY_PHASING_1;
            case 32:
                return SPELL_INSANITY_PHASING_2;
            case 64:
                return SPELL_INSANITY_PHASING_3;
            case 128:
                return SPELL_INSANITY_PHASING_4;
            case 256:
                return SPELL_INSANITY_PHASING_5;
        }
        return 0;
    }

    void ResetPlayersPhaseMask()
    {
        Map::PlayerList const& players = me->GetMap()->GetPlayers();
        for (auto const& i : players)
        {
            if (Player* player = i.GetSource())
            {
                player->RemoveAurasDueToSpell(SPELL_INSANITY_PHASING_1);
                player->RemoveAurasDueToSpell(SPELL_INSANITY_PHASING_2);
                player->RemoveAurasDueToSpell(SPELL_INSANITY_PHASING_3);
                player->RemoveAurasDueToSpell(SPELL_INSANITY_PHASING_4);
                player->RemoveAurasDueToSpell(SPELL_INSANITY_PHASING_5);
                player->SetPhaseMask(1, true);
            }
        }
    }
};

class spell_volazj_whisper : public SpellScript
{
    PrepareSpellScript(spell_volazj_whisper);

    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_WHISPER_AGGRO,
            SPELL_WHISPER_INSANITY,
            SPELL_WHISPER_SLAY_1,
            SPELL_WHISPER_SLAY_2,
            SPELL_WHISPER_SLAY_3,
            SPELL_WHISPER_DEATH_1,
            SPELL_WHISPER_DEATH_2
        });
    }

    bool Load() override { return GetCaster()->IsCreature(); }

    void HandleScriptEffect(SpellEffIndex /* effIndex */)
    {
        Unit* target = GetHitPlayer();
        Creature* caster = GetCaster()->ToCreature();
        if (!target || !caster)
        {
            return;
        }

        uint32 text = 0;
        switch (GetSpellInfo()->Id)
        {
            case SPELL_WHISPER_AGGRO:    text = WHISPER_AGGRO;    break;
            case SPELL_WHISPER_INSANITY: text = WHISPER_INSANITY; break;
            case SPELL_WHISPER_SLAY_1:   text = WHISPER_SLAY_1;   break;
            case SPELL_WHISPER_SLAY_2:   text = WHISPER_SLAY_2;   break;
            case SPELL_WHISPER_SLAY_3:   text = WHISPER_SLAY_3;   break;
            case SPELL_WHISPER_DEATH_1:  text = WHISPER_DEATH_1;  break;
            case SPELL_WHISPER_DEATH_2:  text = WHISPER_DEATH_2;  break;
            default: return;
        }
        caster->AI()->Talk(text, target);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_volazj_whisper::HandleScriptEffect, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

void AddSC_boss_volazj()
{
    RegisterAhnKahetCreatureAI(boss_volazj);
    RegisterSpellScript(spell_volazj_whisper);
}