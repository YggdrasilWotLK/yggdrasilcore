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

#include "zulfarrak.h"
#include "Cell.h"
#include "CellImpl.h"
#include "CreatureScript.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "GameObjectScript.h"
#include "GridNotifiers.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptSystem.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"

/// @todo: this import is not necessary for compilation and marked as unused by the IDE
//  however, for some reasons removing it would cause a damn linking issue
//  there is probably some underlying problem with imports which should properly addressed
//  see: https://github.com/azerothcore/azerothcore-wotlk/issues/9766
#include "GridNotifiersImpl.h"

/*######
## npc_sergeant_bly
######*/

enum blySays
{
    SAY_1 = 0,
    SAY_2 = 1
};

enum blySpells
{
    SPELL_HEARTHSTONE            = 8690,
    SPELL_SHIELD_BASH          = 11972,
    SPELL_REVENGE              = 12170
};

#define GOSSIP_BLY                  "That's it!  I'm tired of helping you out.  It's time we settled things on the battlefield!"

class npc_sergeant_bly : public CreatureScript
{
public:
    npc_sergeant_bly() : CreatureScript("npc_sergeant_bly") { }

    struct npc_sergeant_blyAI : public ScriptedAI
    {
        npc_sergeant_blyAI(Creature* creature) : ScriptedAI(creature)
        {
            instance = creature->GetInstanceScript();
        }

        void InitializeAI() override
        {
            ableToPortHome = false;
            startedFight = false;
            me->SetFaction(FACTION_FRIENDLY);
            postGossipStep = 0;
            Text_Timer = 0;
            me->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);
        }

        InstanceScript* instance;

        bool startedFight;
        bool ableToPortHome;
        uint32 postGossipStep;
        uint32 Text_Timer;
        uint32 ShieldBash_Timer;
        uint32 Revenge_Timer; // Revenge is only cast while me has AURA_STATE_DEFENSE (set on dodge, parry or block)
        uint32 Porthome_Timer;
        ObjectGuid PlayerGUID;

        void Reset() override
        {
            ShieldBash_Timer = 5000;
            Revenge_Timer = 8000;
            Porthome_Timer = 2 * MINUTE * IN_MILLISECONDS;
            ableToPortHome = false;
            startedFight = false;
        }

        void EnterEvadeMode(EvadeReason /*reason*/) override
        {
            if (ableToPortHome || startedFight)
                return;

            if (instance && instance->GetData(DATA_PYRAMID) == PYRAMID_GATES_DESTROYED)
            {
                ableToPortHome = true;
                Porthome_Timer = 2 * MINUTE * IN_MILLISECONDS;
            }
        }

        void MovementInform(uint32 type, uint32 /*id*/) override
        {
            if (type != POINT_MOTION_TYPE)
            {
                return;
            }

            if (instance->GetData(DATA_PYRAMID) == PYRAMID_WAVE_3)
            {
                if (Creature* shadowpriest = ObjectAccessor::GetCreature(*me, instance->GetGuidData(NPC_SHADOWPRIEST_SEZZZIZ)))
                {
                    AttackStart(shadowpriest);
                    shadowpriest->CallAssistance();
                }
            }
        }

        void UpdateAI(uint32 diff) override
        {
            if (postGossipStep > 0 && postGossipStep < 4)
            {
                if (Text_Timer <= diff)
                {
                    switch (postGossipStep)
                    {
                        case 1:
                            startedFight = true;
                            //weegli doesn't fight - he goes & blows up the door
                            if (Creature* pWeegli = ObjectAccessor::GetCreature(*me, instance->GetGuidData(NPC_WEEGLI)))
                            {
                                pWeegli->AI()->DoAction(0);
                            }
                            Talk(SAY_1);
                            Text_Timer = 5000;
                            break;
                        case 2:
                            Talk(SAY_2);
                            Text_Timer = 5000;
                            break;
                        case 3:
                            me->SetFaction(FACTION_MONSTER);
                            Player* target = ObjectAccessor::GetPlayer(*me, PlayerGUID);

                            // Weegli doesn't fight - he runs to the gate (triggered via DoAction above)
                            switchFactionIfAlive(NPC_RAVEN, target);
                            switchFactionIfAlive(NPC_ORO, target);
                            switchFactionIfAlive(NPC_MURTA, target);

                            if (target)
                            {
                                AttackStart(target);
                            }
                    }

                    postGossipStep++;
                }
                else
                {
                    Text_Timer -= diff;
                }
            }

            // Start 2 minute hearthstone timer once Weegli opened the door.
            // If players don't challenge Bly via gossip in time, his crew hearths out.
            if (!startedFight && !ableToPortHome && instance && instance->GetData(DATA_PYRAMID) == PYRAMID_GATES_DESTROYED)
            {
                ableToPortHome = true;
                Porthome_Timer = 2 * MINUTE * IN_MILLISECONDS;
            }

            if (ableToPortHome)
            {
                if (Porthome_Timer <= diff)
                {
                    SendCrewHome();
                    ableToPortHome = false;
                }
                else
                {
                    Porthome_Timer -= diff;
                }
            }

            if (!UpdateVictim())
            {
                return;
            }

            if (ShieldBash_Timer <= diff)
            {
                DoCastVictim(SPELL_SHIELD_BASH);
                ShieldBash_Timer = 15000;
            }
            else
            {
                ShieldBash_Timer -= diff;
            }

            if (Revenge_Timer <= diff)
            {
                if (me->HasAuraState(AURA_STATE_DEFENSE))
                {
                    DoCastVictim(SPELL_REVENGE);
                    Revenge_Timer = 10000;
                }
            }
            else
            {
                Revenge_Timer -= diff;
            }

            DoMeleeAttackIfReady();
        }

        void DoAction(int32 /*param*/) override
        {
            ableToPortHome = false;
            postGossipStep = 1;
            Text_Timer = 0;
        }

        void MakeNonInteractiveAndHearth(Creature* crew)
        {
            if (!crew || !crew->IsAlive())
                return;

            // Make non interactive so the 10s hearthstone can't be interrupted via gossip/combat
            crew->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
            crew->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
            crew->SetImmuneToPC(true);
            crew->GetMotionMaster()->Clear();
            crew->GetMotionMaster()->MoveIdle();
            crew->CastSpell(crew, SPELL_HEARTHSTONE);
            crew->DespawnOrUnsummon(11s);
        }

        void SendCrewHome()
        {
            // NOTE: Weegli is intentionally left out - he despawned after
            // blowing the gate and is not part of the crew anymore.
            if (Creature* raven = ObjectAccessor::GetCreature(*me, instance->GetGuidData(NPC_RAVEN)))
                MakeNonInteractiveAndHearth(raven);
            if (Creature* oro = ObjectAccessor::GetCreature(*me, instance->GetGuidData(NPC_ORO)))
                MakeNonInteractiveAndHearth(oro);
            if (Creature* murta = ObjectAccessor::GetCreature(*me, instance->GetGuidData(NPC_MURTA)))
                MakeNonInteractiveAndHearth(murta);
            MakeNonInteractiveAndHearth(me);
        }

        void switchFactionIfAlive(uint32 entry, Player* target)
        {
            if (Creature* crew = ObjectAccessor::GetCreature(*me, instance->GetGuidData(entry)))
            {
                if (crew->IsAlive())
                {
                    crew->SetFaction(FACTION_MONSTER);

                    if (target)
                    {
                        crew->AI()->AttackStart(target);
                    }
                }
            }
        }

        void sGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);

            if (action == GOSSIP_ACTION_INFO_DEF + 1)
            {
                CloseGossipMenuFor(player);
                PlayerGUID = player->GetGUID();
                DoAction(0);
            }
        }

        void sGossipHello(Player* player) override
        {
            if (instance->GetData(DATA_PYRAMID) >= PYRAMID_MOVED_DOWNSTAIRS && !startedFight)
            {
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_BLY, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
                SendGossipMenuFor(player, 1517, me->GetGUID());
            }
            else
            {
                if (instance->GetData(DATA_PYRAMID) == PYRAMID_NOT_STARTED)
                {
                    SendGossipMenuFor(player, 1515, me->GetGUID());
                }
                else
                {
                    SendGossipMenuFor(player, 1516, me->GetGUID());
                }
            }
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetZulFarrakAI<npc_sergeant_blyAI>(creature);
    }
};

/*######
+## go_troll_cage
+######*/

class go_troll_cage : public GameObjectScript
{
public:
    go_troll_cage() : GameObjectScript("go_troll_cage") { }

    struct go_troll_cageAI : public GameObjectAI
    {
        go_troll_cageAI(GameObject* go) : GameObjectAI(go), instance(go->GetInstanceScript()) { }

        InstanceScript* instance;

        bool GossipHello(Player* /*player*/, bool reportUse) override
        {
            if (reportUse)
            {
                return true;
            }

            instance->SetData(DATA_PYRAMID, PYRAMID_CAGES_OPEN);

            //setting gossip option as soon as the cages open
            if (Creature* bly = ObjectAccessor::GetCreature(*me, instance->GetGuidData(NPC_BLY)))
            {
                bly->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);
            }

            if (Creature* weegli = ObjectAccessor::GetCreature(*me, instance->GetGuidData(NPC_WEEGLI)))
            {
                weegli->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);
            }

            //set bly & co to aggressive & start moving to top of stairs
            initBlyCrewMember(NPC_BLY, 1884.99f, 1263, 41.52f);
            initBlyCrewMember(NPC_RAVEN, 1882.5f, 1263, 41.52f);
            initBlyCrewMember(NPC_ORO, 1886.47f, 1270.68f, 41.68f);
            initBlyCrewMember(NPC_WEEGLI, 1890, 1263, 41.52f);
            initBlyCrewMember(NPC_MURTA, 1891.19f, 1272.03f, 41.60f);
            return false;
        }

    private:
        void initBlyCrewMember(uint32 entry, float x, float y, float z)
        {
            if (Creature* crew = ObjectAccessor::GetCreature(*me, instance->GetGuidData(entry)))
            {
                crew->SetReactState(REACT_AGGRESSIVE);
                crew->SetWalk(true);
                crew->SetHomePosition(x, y, z, 4.78f);
                crew->GetMotionMaster()->MovePoint(1, { x, y, z, 4.78f });
                crew->SetFaction(FACTION_ESCORT_N_NEUTRAL_ACTIVE);

            }
        }
    };

    GameObjectAI* GetAI(GameObject* go) const override
    {
        return GetZulFarrakAI<go_troll_cageAI>(go);
    }
};

/*######
## npc_weegli_blastfuse
######*/

enum weegliSpells
{
    SPELL_BOMB                  = 8858,
    SPELL_GOBLIN_LAND_MINE      = 21688,
    SPELL_SHOOT                 = 6660,
    SPELL_WEEGLIS_BARREL        = 10772
};

enum weegliSays
{
    SAY_WEEGLI_OHNO             = 0,
    SAY_WEEGLI_OK_I_GO          = 1,
    SAY_WEEGLI_OUT_OF_HERE      = 2
};

#define GOSSIP_WEEGLI               "Will you blow up that door now?"

class npc_weegli_blastfuse : public CreatureScript
{
public:
    npc_weegli_blastfuse() : CreatureScript("npc_weegli_blastfuse") { }

    struct npc_weegli_blastfuseAI : public ScriptedAI
    {
        npc_weegli_blastfuseAI(Creature* creature) : ScriptedAI(creature)
        {
            instance = creature->GetInstanceScript();
            destroyingDoor = false;
            doorRunStarted = false;
            bombPlaced = false;
            outroTimer = 0;
            outroStage = 0;
        }

        uint32 Bomb_Timer;
        uint32 LandMine_Timer;
        uint32 outroTimer;
        uint8 outroStage;
        bool destroyingDoor;
        bool doorRunStarted;
        bool bombPlaced;
        InstanceScript* instance;

        static constexpr uint32 POINT_DOOR = 0;
        static constexpr uint32 POINT_GENERIC = 1;
        static constexpr uint32 POINT_ESCAPE_1 = 2;
        static constexpr uint32 POINT_ESCAPE_2 = 3;
        static constexpr uint32 POINT_ESCAPE_3 = 4;

        // Door staging in front of GO_END_DOOR (1854.53, 1142.89, 16.08)
        static constexpr float DOOR_X = 1858.57f;
        static constexpr float DOOR_Y = 1146.35f;
        static constexpr float DOOR_Z = 14.745f;
        static constexpr float DOOR_O = 3.85f;

        void InitializeAI() override
        {
            me->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);
            Reset();
        }

        void Reset() override
        {
            Bomb_Timer     = 10000;
            LandMine_Timer = 30000;
        }

        void AttackStart(Unit* victim) override
        {
            AttackStartCaster(victim, 10);//keep back & toss bombs/shoot
        }

        void UpdateAI(uint32 diff) override
        {
            // Bomb sequence must finish even if something attacks him.
            if (destroyingDoor)
            {
                if (me->IsInCombat())
                {
                    me->CombatStop(true);
                    me->GetThreatMgr().ClearAllThreat();
                }

                if (outroTimer <= diff)
                {
                    switch (outroStage)
                    {
                        case 0:
                            // Place the bomb at the gate, not where gossip happened.
                            // SPELL_WEEGLIS_BARREL summons Weegli's Armed Barrel (141612)
                            // which explodes and opens GO_END_DOOR via SmartScript.
                            DoCastSelf(SPELL_WEEGLIS_BARREL);
                            outroTimer = 2000;
                            ++outroStage;
                            break;
                        case 1:
                            Talk(SAY_WEEGLI_OUT_OF_HERE);
                            me->GetMotionMaster()->Clear(false);
                            me->GetMotionMaster()->MovePoint(POINT_ESCAPE_1, 1853.0f, 1183.0f, 8.5f);
                            // Open the gate even if the barrel SmartScript proximity fails.
                            // Instance also opens it on PYRAMID_GATES_DESTROYED.
                            if (instance)
                            {
                                instance->SetData(DATA_PYRAMID, PYRAMID_GATES_DESTROYED);
                                if (GameObject* door = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(GO_END_DOOR)))
                                    door->UseDoorOrButton();
                            }
                            destroyingDoor = false;
                            break;
                    }
                }
                else
                {
                    outroTimer -= diff;
                }
                return;
            }

            if (!UpdateVictim())
            {
                return;
            }

            if (Bomb_Timer <= diff)
            {
                DoCastVictim(SPELL_BOMB);
                Bomb_Timer = 10000;
            }
            else
            {
                Bomb_Timer -= diff;
            }

            if (LandMine_Timer <= diff)
            {
                DoCastSelf(SPELL_GOBLIN_LAND_MINE);
                LandMine_Timer = 30000;
            }
            else
            {
                LandMine_Timer -= diff;
            }

            if (me->isAttackReady() && !me->IsWithinMeleeRange(me->GetVictim()))
            {
                DoCastVictim(SPELL_SHOOT);
                me->SetSheath(SHEATH_STATE_RANGED);
            }
            else
            {
                me->SetSheath(SHEATH_STATE_MELEE);
                DoMeleeAttackIfReady();
            }
        }

        void JustReachedHome() override
        {
            if (instance->GetData(DATA_PYRAMID) == PYRAMID_CAGES_OPEN)
            {
                me->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);
                instance->SetData(DATA_PYRAMID, PYRAMID_ARRIVED_AT_STAIR);
                Talk(SAY_WEEGLI_OHNO);
            }
            else if (instance->GetData(DATA_PYRAMID) >= PYRAMID_KILLED_ALL_TROLLS && instance->GetData(DATA_PYRAMID) < PYRAMID_DESTROY_GATES)
            {
                instance->SetData(DATA_PYRAMID, PYRAMID_MOVED_DOWNSTAIRS);
            }
            // Do NOT start destroyingDoor here: bomb must only be placed
            // after the dedicated MovePoint(POINT_DOOR) arrival in MovementInform.
            // Starting it on any home arrival caused in-place bombs and a gate that never opens.
        }

        void MovementInform(uint32 type, uint32 id) override
        {
            if (type != POINT_MOTION_TYPE)
            {
                return;
            }

            if (id == POINT_DOOR)
            {
                // Only act on an actual arrival at the gate staging point.
                if (doorRunStarted && !bombPlaced)
                {
                    me->GetMotionMaster()->Clear(false);
                    me->StopMoving();
                    bombPlaced = true;
                    destroyingDoor = true;
                    outroTimer = 0;
                    outroStage = 0;
                }
                return;
            }

            switch (id)
            {
                case POINT_ESCAPE_1:
                    me->GetMotionMaster()->MovePoint(POINT_ESCAPE_2, 1829.0f, 1183.0f, 8.99f);
                    return;
                case POINT_ESCAPE_2:
                    me->GetMotionMaster()->MovePoint(POINT_ESCAPE_3, 1786.0f, 1142.0f, 8.99f);
                    return;
                case POINT_ESCAPE_3:
                    me->DespawnOrUnsummon();
                    return;
                default:
                    break;
            }

            if (instance->GetData(DATA_PYRAMID) == PYRAMID_CAGES_OPEN)
            {
                me->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);
                instance->SetData(DATA_PYRAMID, PYRAMID_ARRIVED_AT_STAIR);
                Talk(SAY_WEEGLI_OHNO);
            }
            else if (instance->GetData(DATA_PYRAMID) >= PYRAMID_KILLED_ALL_TROLLS && instance->GetData(DATA_PYRAMID) < PYRAMID_DESTROY_GATES)
            {
                instance->SetData(DATA_PYRAMID, PYRAMID_MOVED_DOWNSTAIRS);
            }
        }

        void DoAction(int32 /*param*/) override
        {
            // Called by Sergeant Bly when his fight starts: Weegli doesn't fight,
            // he runs to the gate and blows it up.
            DestroyDoor();
        }

        void DestroyDoor()
        {
            if (!me->IsAlive() || doorRunStarted)
            {
                return;
            }

            uint32 phase = instance ? instance->GetData(DATA_PYRAMID) : PYRAMID_NOT_STARTED;
            // DESTROY_GATES allowed as retry after restart/evade when AI state was lost.
            if (phase != PYRAMID_KILLED_ALL_TROLLS && phase != PYRAMID_MOVED_DOWNSTAIRS && phase != PYRAMID_DESTROY_GATES)
            {
                return;
            }

            doorRunStarted = true;
            bombPlaced = false;
            destroyingDoor = false;
            outroTimer = 0;
            outroStage = 0;
            me->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
            me->SetFaction(35);
            me->SetReactState(REACT_PASSIVE);
            me->SetImmuneToAll(true);
            me->CombatStop(true);
            me->GetThreatMgr().ClearAllThreat();
            me->SetWalk(false);
            me->GetMotionMaster()->Clear(false);
            me->GetMotionMaster()->MovePoint(POINT_DOOR, { DOOR_X, DOOR_Y, DOOR_Z, DOOR_O });
            me->SetHomePosition(DOOR_X, DOOR_Y, DOOR_Z, DOOR_O);
            Talk(SAY_WEEGLI_OK_I_GO);
            if (phase != PYRAMID_DESTROY_GATES)
                instance->SetData(DATA_PYRAMID, PYRAMID_DESTROY_GATES);
        }

        void sGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);

            if (action == GOSSIP_ACTION_INFO_DEF + 1)
            {
                CloseGossipMenuFor(player);
                //here we make him run to door, set the charge and run away off to nowhere
                DestroyDoor();
            }
        }

        void sGossipHello(Player* player) override
        {
            switch (instance->GetData(DATA_PYRAMID))
            {
                case PYRAMID_MOVED_DOWNSTAIRS:
                case PYRAMID_KILLED_ALL_TROLLS:
                    AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_WEEGLI, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
                    SendGossipMenuFor(player, 1514, me->GetGUID());  //if event can proceed to end
                    break;
                case PYRAMID_NOT_STARTED:
                    SendGossipMenuFor(player, 1511, me->GetGUID());  //if event not started
                    break;
                default:
                    SendGossipMenuFor(player, 1513, me->GetGUID());  //if event is in progress
            }
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetZulFarrakAI<npc_weegli_blastfuseAI>(creature);
    }
};

/*######
## go_shallow_grave
######*/

enum ShallowGraveSpells
{
    SPELL_SUMMON_ZULFARRAK_ZOMBIES = 10247
};

class go_shallow_grave : public GameObjectScript
{
public:
    go_shallow_grave() : GameObjectScript("go_shallow_grave") { }

    struct go_shallow_graveAI : public GameObjectAI
    {
        go_shallow_graveAI(GameObject* go) : GameObjectAI(go) { }

        void Reset() override
        {
            _casted = false;
            _despawned = false;
        }

        bool GossipHello(Player* /*player*/, bool /*reportUse*/) override
        {
            // Cast 10247 once per grave on first click, but do not consume
            // the interaction so the chest loot window still opens
            if (!_casted)
            {
                _casted = true;

                // Summon chances are handled by
                // spell_zulfarrak_summon_zulfarrak_zombies in instance_zulfarrak.cpp
                me->CastSpell(nullptr, SPELL_SUMMON_ZULFARRAK_ZOMBIES);
            }

            return false;
        }

        void OnStateChanged(uint32 state, Unit* /*unit*/) override
        {
            // Chest is consumable, so core despawns it once loot is taken.
            // Enforce a long respawn here so it stays gone while the instance exists.
            if (state == GO_JUST_DEACTIVATED && !_despawned)
            {
                _despawned = true;
                me->DespawnOrUnsummon(1s, Seconds(WEEK));
            }
        }

    private:
        bool _casted = false;
        bool _despawned = false;
    };

    GameObjectAI* GetAI(GameObject* go) const override
    {
        return GetZulFarrakAI<go_shallow_graveAI>(go);
    }
};

enum ShadowPriestSezzizEnum
{
    SPELL_SHADOW_BOLT       = 15537,
    SPELL_PSYCHIC_SCREEM    = 13704,
    SPELL_RENEW             = 8362,
    SPELL_HEAL              = 12039
};

std::array<std::vector<std::pair<uint32, Position>>, 4> shadowpriestSezzizAdds =
{ {
    { { NPC_SANDFURY_ZEALOT, { 1874.12f, 1198.90f, 8.87f } }, { NPC_SANDFURY_ACOLYTE, { 1874.12f, 1198.90f, 8.87f } } },
    { { NPC_SANDFURY_ACOLYTE, { 895.26f, 1199.09f, 8.87f } }, { NPC_SANDFURY_ACOLYTE, { 895.26f, 1199.088f, 8.87f } } },
    { { NPC_SANDFURY_ZEALOT, { 1874.12f, 1198.90f, 8.87f } }, { NPC_SANDFURY_ACOLYTE, { 895.26f, 1199.09f, 8.87f } }, { NPC_SANDFURY_ACOLYTE, { 895.26f, 1199.09f, 8.87f } } },
    { { NPC_SANDFURY_ZEALOT, { 895.26f, 1199.09f, 8.87f } }, { NPC_SANDFURY_ZEALOT, { 1874.12f, 1198.90f, 8.87f } }, { NPC_SANDFURY_ACOLYTE, { 1874.12f, 1198.90f } }, { NPC_SANDFURY_ACOLYTE, { 895.26f, 1199.09f, 8.87f } } }
} };

class npc_shadowpriest_sezziz : public CreatureScript
{
public:
    npc_shadowpriest_sezziz() : CreatureScript("npc_shadowpriest_sezziz") {}

    struct npc_shadowpriest_sezzizAI : public ScriptedAI
    {
        npc_shadowpriest_sezzizAI(Creature* creature) : ScriptedAI(creature) { }

        void Reset() override
        {
            _shadowBoltTimer = urand(0, 1 * IN_MILLISECONDS);
            _physicScreemTimer = urand(1 * IN_MILLISECONDS, 16 * IN_MILLISECONDS);
            _missingHPForRenewTimer = urand(10 * IN_MILLISECONDS, 15 * IN_MILLISECONDS);
            _missingHPForHealTimer = urand(7 * IN_MILLISECONDS, 10 * IN_MILLISECONDS);
            _summonAddsTimer = 12 * IN_MILLISECONDS;
            _summmonAddsCount = 0;
        }

        void AttackStart(Unit* victim) override
        {
            ScriptedAI::AttackStartCaster(victim, 40.f);
        }

        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
            {
                return;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
            {
                return;
            }

            if (_summonAddsTimer <= diff)
            {
                for (auto& itr : shadowpriestSezzizAdds[_summmonAddsCount])
                {
                    if (Creature* add = me->SummonCreature(itr.first, itr.second, TEMPSUMMON_DEAD_DESPAWN, 10 * IN_MILLISECONDS))
                    {
                        add->AI()->AttackStart(me->GetVictim());
                    }
                }

                if (++_summmonAddsCount >= 4)
                {
                    _summmonAddsCount = 0;
                }

                _summonAddsTimer = urand(10 * IN_MILLISECONDS, 14 * IN_MILLISECONDS);
            }
            else
            {
                _summonAddsTimer -= diff;
            }

            if (_missingHPForHealTimer <= diff)
            {
                Unit* unit = nullptr;
                Acore::MostHPMissingInRange u_check(me, 40.f, 1500);
                Acore::UnitLastSearcher<Acore::MostHPMissingInRange> searcher(me, unit, u_check);
                Cell::VisitObjects(me, searcher, 40.f);
                if (unit)
                {
                    DoCast(unit, SPELL_HEAL);
                }

                _missingHPForHealTimer = urand(7 * IN_MILLISECONDS, 10 * IN_MILLISECONDS);
            }
            else
            {
                _missingHPForHealTimer -= diff;
            }

            if (_missingHPForRenewTimer <= diff)
            {
                Unit* unit = nullptr;
                Acore::MostHPMissingInRange u_check(me, 40.f, 700);
                Acore::UnitLastSearcher<Acore::MostHPMissingInRange> searcher(me, unit, u_check);
                Cell::VisitObjects(me, searcher, 40.f);
                if (unit)
                {
                    DoCast(unit, SPELL_RENEW);
                }

                _missingHPForRenewTimer = urand(10 * IN_MILLISECONDS, 15 * IN_MILLISECONDS);
            }
            else
            {
                _missingHPForRenewTimer -= diff;
            }

            if (_physicScreemTimer <= diff)
            {
                DoCastVictim(SPELL_PSYCHIC_SCREEM);
                _physicScreemTimer = urand(22 * IN_MILLISECONDS, 30 * IN_MILLISECONDS);
            }
            else
            {
                _physicScreemTimer -= diff;
            }

            if (_shadowBoltTimer <= diff)
            {
                DoCastVictim(SPELL_SHADOW_BOLT);
                _shadowBoltTimer = urand(3 * IN_MILLISECONDS, 4 * IN_MILLISECONDS);
            }
            else
            {
                _shadowBoltTimer -= diff;
            }
        }

    private:
        uint32 _shadowBoltTimer;
        uint32 _physicScreemTimer;
        uint32 _missingHPForRenewTimer;
        uint32 _missingHPForHealTimer;
        uint32 _summonAddsTimer;
        uint8 _summmonAddsCount;
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetZulFarrakAI<npc_shadowpriest_sezzizAI>(creature);
    }
};

/*######
 ## npc_zf_sandfury_wave (7787/7788/7789/8876/8877/7796)
 ######*/

static Position const sandfuryStairsA[2] = { { 1898.0f, 1226.0f, 9.32f, 0.0f }, { 1890.0f, 1262.0f, 41.4f, 0.0f } };
static Position const sandfuryStairsB[2] = { { 1878.0f, 1224.0f, 9.29f, 0.0f }, { 1884.0f, 1262.0f, 41.55f, 0.0f } };

class npc_zf_sandfury_wave : public CreatureScript
{
public:
    npc_zf_sandfury_wave() : CreatureScript("npc_zf_sandfury_wave") { }

    struct npc_zf_sandfury_waveAI : public ScriptedAI
    {
        npc_zf_sandfury_waveAI(Creature* creature) : ScriptedAI(creature) { }

        uint32 _t1 = 0, _t2 = 0, _t3 = 0, _t4 = 0;
        bool _selected = false;
        bool _hasDest = false;
        Position _dest;

        void Reset() override
        {
            _selected = false;
            _hasDest = false;

            switch (me->GetEntry())
            {
                case 7789:
                    _t1 = urand(1000, 5000);
                    _t2 = urand(8000, 12000);
                    break;
                case 8876:
                    _t1 = urand(1000, 3000);
                    _t2 = urand(11000, 13000);
                    _t3 = urand(8000, 12000);
                    _t4 = urand(22000, 32000);
                    break;
                case 7796:
                    _t2 = urand(20000, 30000);
                    _t3 = urand(12000, 16000);
                    break;
                default:
                    break;
            }
        }

        void JustEngagedWith(Unit* /*who*/) override
        {
            if (me->GetEntry() == 7796)
                DoCastSelf(19471);
        }

        void EnterEvadeMode(EvadeReason why) override
        {
            me->SetHomePosition(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), me->GetOrientation());
            ScriptedAI::EnterEvadeMode(why);

            Unit* target = nullptr;
            float bestDist = 100.0f;

            if (InstanceScript* instance = me->GetInstanceScript())
            {
                uint32 const crew[] = { NPC_BLY, NPC_RAVEN, NPC_ORO, NPC_WEEGLI };
                for (uint32 entry : crew)
                    if (Creature* member = ObjectAccessor::GetCreature(*me, instance->GetGuidData(entry)))
                        if (member->IsAlive() && me->IsWithinDistInMap(member, bestDist))
                        {
                            bestDist = me->GetDistance(member);
                            target = member;
                        }
            }

            if (!target)
                target = me->SelectNearestPlayer(40.0f);

            if (target)
                me->GetMotionMaster()->MovePoint(0, target->GetPositionX(), target->GetPositionY(), target->GetPositionZ());
            else
                me->GetMotionMaster()->MoveTargetedHome();
        }

        void DoAction(int32 action) override
        {
            if (action == ZF_ACTION_ADD_SELECTED)
            {
                me->SetSpeed(MOVE_RUN, 0.7f);
                _selected = true;
            }
        }

        void UpdateStairsMovement()
        {
            InstanceScript* instance = me->GetInstanceScript();
            if (!instance)
                return;

            Creature* closest = nullptr;
            float closestDist = 0.0f;

            uint32 const crew[] = { NPC_BLY, NPC_RAVEN, NPC_ORO, NPC_WEEGLI, NPC_MURTA };
            for (uint32 entry : crew)
            {
                Creature* member = ObjectAccessor::GetCreature(*me, instance->GetGuidData(entry));
                if (!member || !member->IsAlive())
                    continue;

                float dist = me->GetExactDist2d(member);
                if (!closest || dist < closestDist)
                {
                    closest = member;
                    closestDist = dist;
                }
            }

            if (!closest)
                return;

            Position dest;
            if (closestDist > 50.0f)
                dest = me->GetExactDist2d(&sandfuryStairsA[0]) <= me->GetExactDist2d(&sandfuryStairsB[0]) ? sandfuryStairsA[0] : sandfuryStairsB[0];
            else
                dest = closest->GetPosition();

            if (_hasDest && _dest.GetExactDist2d(&dest) <= 1.0f)
                return;

            _dest = dest;
            _hasDest = true;
            me->GetMotionMaster()->MovePoint(1, dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(),
                FORCED_MOVEMENT_NONE, 0.0f, 0.0f, true, true, MOTION_SLOT_CONTROLLED);
        }

        void UpdateAI(uint32 diff) override
        {
            if (_selected)
                UpdateStairsMovement();

            if (!UpdateVictim())
                return;

            switch (me->GetEntry())
            {
                case 7789:
                    if (_t1 <= diff) { DoCastSelf(20798); _t1 = 1200000; } else _t1 -= diff;
                    if (_t2 <= diff) { DoCastVictim(14032); _t2 = urand(12000, 20000); } else _t2 -= diff;
                    break;
                case 8876:
                    if (HealthBelowPct(15)) { me->DoFleeToGetAssistance(); break; }
                    if (_t1 <= diff) { DoCastVictim(9613); _t1 = urand(3000, 4000); } else _t1 -= diff;
                    if (_t2 <= diff) { DoCastVictim(11981); _t2 = urand(13000, 24000); } else _t2 -= diff;
                    if (_t3 <= diff) { DoCastVictim(11639); _t3 = urand(12000, 20000); } else _t3 -= diff;
                    if (_t4 <= diff) { DoCastVictim(11980); _t4 = urand(32000, 50000); } else _t4 -= diff;
                    break;
                case 8877:
                    if (HealthBelowPct(30) && !me->HasAura(8599))
                        DoCastSelf(8599);
                    break;
                case 7796:
                    if (_t2 <= diff) { DoCastVictim(8600); _t2 = urand(30000, 60000); } else _t2 -= diff;
                    if (_t3 <= diff) { DoCastVictim(26141); _t3 = urand(12000, 20000); } else _t3 -= diff;
                    break;
                default:
                    break;
            }

            DoMeleeAttackIfReady();
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetZulFarrakAI<npc_zf_sandfury_waveAI>(creature);
    }
};

/*######
 ## npc_witch_doctor_zumrah (7271)
 ######*/

enum WitchDoctorZumrah
{
    SPELL_SHADOW_BOLT_VOLLEY    = 15245,
    SPELL_SHADOW_BOLT_ZUMRAH    = 12739,
    SPELL_WARD_OF_ZUMRAH        = 11086,
    SPELL_HEALING_WAVE          = 12491,

    FACTION_HOSTILE_ZUMRAH      = 37,

    TEXT_ZUMRAH_INTRO           = 0,
    TEXT_ZUMRAH_KILL            = 1,
    TEXT_ZUMRAH_AGGRO           = 2,
    TEXT_ZUMRAH_SUMMON          = 3,

    ZOMBIE_POP_INTERVAL         = 10 * IN_MILLISECONDS
};

float constexpr GRAVE_SEARCH_RANGE = 250.0f;

class npc_witch_doctor_zumrah : public CreatureScript
{
public:
    npc_witch_doctor_zumrah() : CreatureScript("npc_witch_doctor_zumrah") { }

    struct npc_witch_doctor_zumrahAI : public ScriptedAI
    {
        npc_witch_doctor_zumrahAI(Creature* creature) : ScriptedAI(creature) { }

        uint32 _shadowBoltVolleyTimer;
        uint32 _shadowBoltTimer;
        uint32 _wardOfZumrahTimer;
        uint32 _healingWaveTimer;
        uint32 _graveTimer;
        bool _introDone;
        GuidList _zombies;

        void Reset() override
        {
            me->RestoreFaction();
            _shadowBoltVolleyTimer = urand(5000, 8000);
            _shadowBoltTimer = 1000;
            _wardOfZumrahTimer = urand(2000, 5000);
            _healingWaveTimer = urand(10000, 20000);
            _graveTimer = ZOMBIE_POP_INTERVAL;
            _introDone = false;
        }

        void MoveInLineOfSight(Unit* who) override
        {
            if (!_introDone && who->IsPlayer() && me->IsWithinDistInMap(who, 10.0f))
            {
                _introDone = true;
                me->SetFaction(FACTION_HOSTILE_ZUMRAH);
                Talk(TEXT_ZUMRAH_INTRO);
            }

            ScriptedAI::MoveInLineOfSight(who);
        }

        void JustEngagedWith(Unit* who) override
        {
            Talk(TEXT_ZUMRAH_AGGRO);
            _graveTimer = 0;

            for (ObjectGuid guid : _zombies)
                if (Creature* zombie = ObjectAccessor::GetCreature(*me, guid))
                    if (who && zombie->IsAlive() && !zombie->IsInCombat())
                        zombie->AI()->AttackStart(who);
        }

        void KilledUnit(Unit* /*who*/) override
        {
            Talk(TEXT_ZUMRAH_KILL);
        }

        void EnterEvadeMode(EvadeReason why) override
        {
            for (ObjectGuid guid : _zombies)
            {
                Creature* zombie = ObjectAccessor::GetCreature(*me, guid);
                if (!zombie || !zombie->IsAlive())
                    continue;

                Position pos = me->GetHomePosition();
                pos.m_positionX += frand(-3.0f, 3.0f);
                pos.m_positionY += frand(-3.0f, 3.0f);
                zombie->SetHomePosition(pos);
                zombie->GetMotionMaster()->MoveTargetedHome();
            }

            ScriptedAI::EnterEvadeMode(why);
        }

        void PopShallowGrave()
        {
            GameObject* grave = nullptr;
            float bestDist = GRAVE_SEARCH_RANGE;

            for (uint32 entry : { GO_SHALLOW_GRAVE, GO_SHALLOW_GRAVE_2 })
            {
                if (GameObject* go = me->FindNearestGameObject(entry, GRAVE_SEARCH_RANGE, true))
                {
                    float dist = me->GetDistance(go);
                    if (dist < bestDist)
                    {
                        bestDist = dist;
                        grave = go;
                    }
                }
            }

            if (!grave)
                return;

            float x = grave->GetPositionX();
            float y = grave->GetPositionY();
            float z = grave->GetPositionZ();
            grave->DespawnOrUnsummon();

            if (Creature* zombie = me->SummonCreature(NPC_ZULFARRAK_ZOMBIE, x, y, z, 0.0f, TEMPSUMMON_MANUAL_DESPAWN))
            {
                _zombies.push_back(zombie->GetGUID());
                if (Player* target = zombie->SelectNearestPlayer(100.0f))
                    zombie->AI()->AttackStart(target);
            }
        }

        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            if (_graveTimer <= diff)
            {
                PopShallowGrave();
                _graveTimer = ZOMBIE_POP_INTERVAL;
            }
            else
                _graveTimer -= diff;

            if (_shadowBoltVolleyTimer <= diff)
            {
                DoCastSelf(SPELL_SHADOW_BOLT_VOLLEY);
                _shadowBoltVolleyTimer = urand(12000, 16000);
            }
            else
                _shadowBoltVolleyTimer -= diff;

            if (_shadowBoltTimer <= diff)
            {
                DoCastVictim(SPELL_SHADOW_BOLT_ZUMRAH);
                _shadowBoltTimer = urand(3000, 4000);
            }
            else
                _shadowBoltTimer -= diff;

            if (_wardOfZumrahTimer <= diff)
            {
                Talk(TEXT_ZUMRAH_SUMMON);
                DoCastSelf(SPELL_WARD_OF_ZUMRAH);
                _wardOfZumrahTimer = urand(30000, 36000);
            }
            else
                _wardOfZumrahTimer -= diff;

            if (_healingWaveTimer <= diff)
            {
                if (HealthBelowPct(70))
                {
                    DoCastSelf(SPELL_HEALING_WAVE);
                    _healingWaveTimer = urand(10000, 20000);
                }
            }
            else
                _healingWaveTimer -= diff;

            DoMeleeAttackIfReady();
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetZulFarrakAI<npc_witch_doctor_zumrahAI>(creature);
    }
};

void AddSC_zulfarrak()
{
    new npc_sergeant_bly();
    new npc_weegli_blastfuse();
    new npc_shadowpriest_sezziz();
    new npc_zf_sandfury_wave();
    new npc_witch_doctor_zumrah();
    new go_troll_cage();
    new go_shallow_grave();
}
