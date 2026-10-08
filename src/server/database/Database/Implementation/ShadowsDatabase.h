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

#ifndef _ShadowsDatabase_H
#define _ShadowsDatabase_H

#include "MySQLConnection.h"

enum ShadowsDatabaseStatements : uint32
{
    /*  Naming standard for defines:
        {DB}_{SEL/INS/UPD/DEL/REP}_{Summary of data changed}
        When updating more than one field, consider looking at the calling function
        name for a suiting suffix.
    */

    SHADOWS_SEL_CUSTOM_STRATEGY_BY_OWNER,
    SHADOWS_SEL_CUSTOM_STRATEGY_BY_OWNER_AND_NAME,
    SHADOWS_SEL_CUSTOM_STRATEGY_BY_OWNER_AND_NAME_AND_IDX,
    SHADOWS_DEL_CUSTOM_STRATEGY,
    SHADOWS_UPD_CUSTOM_STRATEGY,
    SHADOWS_INS_CUSTOM_STRATEGY,

    SHADOWS_SEL_DB_STORE,
    SHADOWS_DEL_DB_STORE,
    SHADOWS_INS_DB_STORE,

    SHADOWS_SEL_ENCHANTS,

    SHADOWS_SEL_EQUIP_CACHE,
    SHADOWS_INS_EQUIP_CACHE,

    SHADOWS_SEL_GUILD_TASKS_BY_VALUE,
    SHADOWS_SEL_GUILD_TASKS_BY_OWNER,
    SHADOWS_SEL_GUILD_TASKS_BY_OWNER_AND_TYPE,
    SHADOWS_SEL_GUILD_TASKS_BY_OWNER_DISTINCT,
    SHADOWS_SEL_GUILD_TASKS_BY_OWNER_ORDERED,
    SHADOWS_DEL_GUILD_TASKS,
    SHADOWS_INS_GUILD_TASKS,

    SHADOWS_SEL_RANDOM_BOTS_VALUE,
    SHADOWS_SEL_RANDOM_BOTS_BOT,
    SHADOWS_SEL_RANDOM_BOTS_BY_OWNER_AND_EVENT,
    SHADOWS_SEL_RANDOM_BOTS_BY_OWNER_AND_BOT,
    SHADOWS_SEL_RANDOM_BOTS_BY_EVENT_AND_VALUE,
    SHADOWS_INS_RANDOM_BOTS,
    SHADOWS_DEL_RANDOM_BOTS,
    SHADOWS_DEL_RANDOM_BOTS_BY_OWNER,
    SHADOWS_DEL_RANDOM_BOTS_BY_OWNER_AND_EVENT,
    SHADOWS_UPD_RANDOM_BOTS,

    SHADOWS_SEL_RARITY_CACHE,
    SHADOWS_INS_RARITY_CACHE,

    SHADOWS_SEL_RNDITEM_CACHE,
    SHADOWS_INS_RNDITEM_CACHE,

    SHADOWS_SEL_SPEECH,
    SHADOWS_SEL_SPEECH_PROBABILITY,

    SHADOWS_SEL_TELE_CACHE,
    SHADOWS_INS_TELE_CACHE,

    SHADOWS_SEL_TEXT,
    SHADOWS_SEL_DUNGEON_SUGGESTION,

    SHADOWS_SEL_TRAVELNODE,
    SHADOWS_INS_TRAVELNODE,
    SHADOWS_DEL_TRAVELNODE,

    SHADOWS_SEL_TRAVELNODE_LINK,
    SHADOWS_INS_TRAVELNODE_LINK,
    SHADOWS_DEL_TRAVELNODE_LINK,

    SHADOWS_SEL_TRAVELNODE_PATH,
    SHADOWS_INS_TRAVELNODE_PATH,
    SHADOWS_DEL_TRAVELNODE_PATH,

    SHADOWS_SEL_WEIGHTSCALES,
    SHADOWS_SEL_WEIGHTSCALE_DATA,

    SHADOWS_INS_EQUIP_CACHE_NEW,
    SHADOWS_DEL_EQUIP_CACHE_NEW,

    MAX_SHADOWS_STATEMENTS
};

class AC_DATABASE_API ShadowsDatabaseConnection : public MySQLConnection
{
public:
    typedef ShadowsDatabaseStatements Statements;

    //- Constructors for sync and async connections
    ShadowsDatabaseConnection(MySQLConnectionInfo& connInfo);
    ShadowsDatabaseConnection(ProducerConsumerQueue<SQLOperation*>* q, MySQLConnectionInfo& connInfo);
    ~ShadowsDatabaseConnection();

    //- Loads database type specific prepared statements
    void DoPrepareStatements() override;
};

#endif

#endif
