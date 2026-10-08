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

#ifdef MOD_SHADOWS

#include "ShadowsDatabase.h"
#include "MySQLPreparedStatement.h"

void ShadowsDatabaseConnection::DoPrepareStatements()
{
    if (!m_reconnecting)
        m_stmts.resize(MAX_SHADOWS_STATEMENTS);

    PrepareStatement(SHADOWS_SEL_CUSTOM_STRATEGY_BY_OWNER, "SELECT DISTINCT name FROM shadows_custom_strategy WHERE owner = ?", CONNECTION_SYNCH);
    PrepareStatement(SHADOWS_SEL_CUSTOM_STRATEGY_BY_OWNER_AND_NAME, "SELECT idx, action_line FROM shadows_custom_strategy WHERE owner = ? AND name = ? ORDER BY idx", CONNECTION_SYNCH);
    PrepareStatement(SHADOWS_SEL_CUSTOM_STRATEGY_BY_OWNER_AND_NAME_AND_IDX, "SELECT action_line FROM shadows_custom_strategy WHERE owner = ? AND name = ? AND idx = ?", CONNECTION_SYNCH);
    PrepareStatement(SHADOWS_DEL_CUSTOM_STRATEGY, "DELETE FROM shadows_custom_strategy WHERE name = ? AND owner = ? AND idx = ?", CONNECTION_ASYNC);
    PrepareStatement(SHADOWS_UPD_CUSTOM_STRATEGY, "UPDATE shadows_custom_strategy SET action_line = ? WHERE name = ? AND owner = ? AND idx = ?", CONNECTION_ASYNC);
    PrepareStatement(SHADOWS_INS_CUSTOM_STRATEGY, "INSERT INTO shadows_custom_strategy (name, owner, idx, action_line) VALUES (?, ?, ?, ?)", CONNECTION_ASYNC);

    PrepareStatement(SHADOWS_SEL_DB_STORE, "SELECT `key`,`value` FROM `shadows_db_store` WHERE `guid` = ?", CONNECTION_SYNCH);
    PrepareStatement(SHADOWS_DEL_DB_STORE, "DELETE FROM `shadows_db_store` WHERE `guid` = ?", CONNECTION_ASYNC);
    PrepareStatement(SHADOWS_INS_DB_STORE, "INSERT INTO `shadows_db_store` (`guid`, `key`, `value`) VALUES (?, ?, ?)", CONNECTION_ASYNC);

    PrepareStatement(SHADOWS_SEL_ENCHANTS, "SELECT class, spec, spellid, slotid FROM shadows_enchants", CONNECTION_SYNCH);

    PrepareStatement(SHADOWS_SEL_EQUIP_CACHE, "SELECT clazz, lvl, slot, quality, item FROM shadows_equip_cache", CONNECTION_SYNCH);
    PrepareStatement(SHADOWS_INS_EQUIP_CACHE, "INSERT INTO shadows_equip_cache (clazz, lvl, slot, quality, item) VALUES (?, ?, ?, ?, ?)", CONNECTION_ASYNC);

    PrepareStatement(SHADOWS_SEL_GUILD_TASKS_BY_VALUE, "SELECT `value`, `time`, validIn FROM shadows_guild_tasks WHERE `value` = ? AND guildid = ? AND `type` = ?", CONNECTION_SYNCH);
    PrepareStatement(SHADOWS_SEL_GUILD_TASKS_BY_OWNER, "SELECT `value`, `time`, validIn, guildid FROM shadows_guild_tasks WHERE owner = ? AND `type` = ?", CONNECTION_SYNCH);
    PrepareStatement(SHADOWS_SEL_GUILD_TASKS_BY_OWNER_AND_TYPE, "SELECT `value`, `time`, validIn FROM shadows_guild_tasks WHERE owner = ? AND guildid = ? AND `type` = ?", CONNECTION_SYNCH);
    PrepareStatement(SHADOWS_SEL_GUILD_TASKS_BY_OWNER_DISTINCT, "SELECT DISTINCT guildid FROM shadows_guild_tasks WHERE owner = ?", CONNECTION_SYNCH);
    PrepareStatement(SHADOWS_SEL_GUILD_TASKS_BY_OWNER_ORDERED, "SELECT `value`, `time`, validIn, guildid FROM shadows_guild_tasks WHERE owner = ? AND type = ? ORDER BY guildid", CONNECTION_SYNCH);
    PrepareStatement(SHADOWS_DEL_GUILD_TASKS, "DELETE FROM shadows_guild_tasks WHERE owner = ? AND guildid = ? AND `type` = ?", CONNECTION_ASYNC);
    PrepareStatement(SHADOWS_INS_GUILD_TASKS, "INSERT INTO shadows_guild_tasks (owner, guildid, `time`, validIn, `type`, `value`) VALUES (?, ?, ?, ?, ?, ?)", CONNECTION_ASYNC);

    PrepareStatement(SHADOWS_SEL_RANDOM_BOTS_VALUE, "SELECT value FROM shadows_random_bots WHERE event = ?", CONNECTION_SYNCH);
    PrepareStatement(SHADOWS_SEL_RANDOM_BOTS_BOT, "SELECT `bot` FROM shadows_random_bots WHERE event = ?", CONNECTION_SYNCH);
    PrepareStatement(SHADOWS_SEL_RANDOM_BOTS_BY_OWNER_AND_EVENT, "SELECT bot FROM shadows_random_bots WHERE owner = ? AND event = ?", CONNECTION_SYNCH);
    PrepareStatement(SHADOWS_SEL_RANDOM_BOTS_BY_OWNER_AND_BOT, "SELECT `event`, `value`, `time`, validIn, `data` FROM shadows_random_bots WHERE owner = ? AND bot = ?", CONNECTION_SYNCH);
    PrepareStatement(SHADOWS_SEL_RANDOM_BOTS_BY_EVENT_AND_VALUE, "SELECT bot FROM shadows_random_bots WHERE event = ? AND value = ?", CONNECTION_SYNCH);
    PrepareStatement(SHADOWS_INS_RANDOM_BOTS, "INSERT INTO shadows_random_bots (owner, bot, `time`, validIn, event, `value`, `data`) VALUES (?, ?, ?, ?, ?, ?, ?)", CONNECTION_ASYNC);
    PrepareStatement(SHADOWS_DEL_RANDOM_BOTS, "DELETE FROM shadows_random_bots", CONNECTION_ASYNC);
    PrepareStatement(SHADOWS_DEL_RANDOM_BOTS_BY_OWNER, "DELETE FROM shadows_random_bots WHERE owner = ? AND bot = ?", CONNECTION_ASYNC);
    PrepareStatement(SHADOWS_DEL_RANDOM_BOTS_BY_OWNER_AND_EVENT, "DELETE FROM shadows_random_bots WHERE owner = ? AND bot = ? AND event = ?", CONNECTION_ASYNC);
    PrepareStatement(SHADOWS_UPD_RANDOM_BOTS, "UPDATE shadows_random_bots SET validIn = ? WHERE event = ? AND bot = ?", CONNECTION_ASYNC);

    PrepareStatement(SHADOWS_SEL_RARITY_CACHE, "SELECT item, rarity FROM shadows_rarity_cache", CONNECTION_SYNCH);
    PrepareStatement(SHADOWS_INS_RARITY_CACHE, "INSERT INTO shadows_rarity_cache (item, rarity) VALUES (?, ?)", CONNECTION_ASYNC);

    PrepareStatement(SHADOWS_SEL_RNDITEM_CACHE, "SELECT lvl, type, item FROM shadows_rnditem_cache", CONNECTION_SYNCH);
    PrepareStatement(SHADOWS_INS_RNDITEM_CACHE, "INSERT INTO shadows_rnditem_cache (lvl, type, item) VALUES (?, ?, ?)", CONNECTION_ASYNC);

    PrepareStatement(SHADOWS_SEL_SPEECH, "SELECT name, text, type FROM shadows_speech", CONNECTION_SYNCH);
    PrepareStatement(SHADOWS_SEL_SPEECH_PROBABILITY, "SELECT name, probability FROM shadows_speech_probability", CONNECTION_SYNCH);

    PrepareStatement(SHADOWS_SEL_TELE_CACHE, "SELECT map_id, x, y, z, level FROM shadows_tele_cache", CONNECTION_SYNCH);
    PrepareStatement(SHADOWS_INS_TELE_CACHE, "INSERT INTO shadows_tele_cache (level, map_id, x, y, z) VALUES (?, ?, ?, ?, ?)", CONNECTION_ASYNC);

    PrepareStatement(SHADOWS_SEL_TRAVELNODE, "SELECT id, name, map_id, x, y, z, linked FROM shadows_travelnode", CONNECTION_SYNCH);
    PrepareStatement(SHADOWS_INS_TRAVELNODE, "INSERT INTO `shadows_travelnode` (`id`, `name`, `map_id`, `x`, `y`, `z`, `linked`) VALUES (?, ?, ?, ?, ?, ?, ?)", CONNECTION_ASYNC);
    PrepareStatement(SHADOWS_DEL_TRAVELNODE, "DELETE FROM shadows_travelnode", CONNECTION_ASYNC);

    PrepareStatement(SHADOWS_SEL_TRAVELNODE_LINK, "SELECT node_id, to_node_id,type,object,distance,swim_distance, extra_cost,calculated, max_creature_0,max_creature_1,max_creature_2 FROM shadows_travelnode_link", CONNECTION_SYNCH);
    PrepareStatement(SHADOWS_INS_TRAVELNODE_LINK, "INSERT INTO `shadows_travelnode_link` (`node_id`, `to_node_id`,`type`,`object`,`distance`,`swim_distance`, `extra_cost`,`calculated`, `max_creature_0`,`max_creature_1`,`max_creature_2`) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", CONNECTION_ASYNC);
    PrepareStatement(SHADOWS_DEL_TRAVELNODE_LINK, "DELETE FROM shadows_travelnode_link", CONNECTION_ASYNC);

    PrepareStatement(SHADOWS_SEL_TRAVELNODE_PATH, "SELECT node_id, to_node_id, nr, map_id, x, y, z FROM shadows_travelnode_path order by node_id, to_node_id, nr", CONNECTION_SYNCH);
    PrepareStatement(SHADOWS_INS_TRAVELNODE_PATH, "INSERT INTO `shadows_travelnode_path` (`node_id`, `to_node_id`, `nr`, `map_id`, `x`, `y`, `z`) VALUES (?, ?, ?, ?, ?, ?, ?)", CONNECTION_ASYNC);
    PrepareStatement(SHADOWS_DEL_TRAVELNODE_PATH, "DELETE FROM shadows_travelnode_path", CONNECTION_ASYNC);

    PrepareStatement(SHADOWS_SEL_TEXT, "SELECT `name`, `text`, `say_type`, `reply_type`, `text_loc1`, `text_loc2`, `text_loc3`, `text_loc4`, `text_loc5`, `text_loc6`, `text_loc7`, `text_loc8` FROM `ai_shadow_texts`", CONNECTION_SYNCH);
    PrepareStatement(
        SHADOWS_SEL_DUNGEON_SUGGESTION,
        "SELECT"
        "   d.`name`, "
        "   d.`difficulty`, "
        "   d.`min_level`, "
        "   d.`max_level`, "
        "   a.`abbrevation`, "
        "   s.`strategy` "
        "FROM shadows_dungeon_suggestion_definition d "
        "LEFT OUTER JOIN shadows_dungeon_suggestion_abbrevation a "
        "   ON d.slug = a.definition_slug "
        "LEFT OUTER JOIN shadows_dungeon_suggestion_strategy s "
        "   ON d.slug = s.definition_slug "
        "   AND d.difficulty = s.difficulty "
        "WHERE d.expansion <= ?;",
        CONNECTION_SYNCH
    );

    PrepareStatement(SHADOWS_SEL_WEIGHTSCALES, "SELECT id, name, class FROM shadows_weightscales", CONNECTION_SYNCH);
    PrepareStatement(SHADOWS_SEL_WEIGHTSCALE_DATA, "SELECT id, field, val FROM shadows_weightscale_data", CONNECTION_SYNCH);

    PrepareStatement(SHADOWS_INS_EQUIP_CACHE_NEW, "INSERT INTO shadows_item_info_cache (id, quality, slot, source, sourceId, team, faction, factionRepRank, minLevel, "
            "scale_1, scale_2, scale_3, scale_4, scale_5, scale_6, scale_7, scale_8, scale_9, scale_10, scale_11, scale_12, scale_13, scale_14, scale_15, "
            "scale_16, scale_17, scale_18, scale_19, scale_20, scale_21, scale_22, scale_23, scale_24, scale_25, scale_26, scale_27, scale_28, scale_29, scale_30, scale_31, scale_32) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", CONNECTION_ASYNC);
    PrepareStatement(SHADOWS_DEL_EQUIP_CACHE_NEW, "DELETE FROM shadows_item_info_cache WHERE id = ?", CONNECTION_ASYNC);
}

ShadowsDatabaseConnection::ShadowsDatabaseConnection(MySQLConnectionInfo& connInfo) : MySQLConnection(connInfo)
{
}

ShadowsDatabaseConnection::ShadowsDatabaseConnection(ProducerConsumerQueue<SQLOperation*>* q, MySQLConnectionInfo& connInfo) : MySQLConnection(q, connInfo)
{
}

ShadowsDatabaseConnection::~ShadowsDatabaseConnection()
{
}

#endif
