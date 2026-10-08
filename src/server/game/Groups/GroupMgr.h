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

#ifndef _GROUPMGR_H
#define _GROUPMGR_H

#include "Group.h"

#include <utility>
#include <vector>

class GroupMgr
{
private:
    GroupMgr();
    ~GroupMgr();

public:
    static GroupMgr* instance();

    typedef std::map<uint32, Group*> GroupContainer;

    Group* GetGroupByGUID(ObjectGuid::LowType guid) const;

    void InitGroupIds();
    void RegisterGroupId(ObjectGuid::LowType groupId);
    ObjectGuid::LowType GenerateGroupId();

    void LoadGroups();
    void AddGroup(Group* group);
    void RemoveGroup(Group* group);
    // Crash-proof: only the manager frees groups. Destroy unregisters, marks
    // the group disbanded (zombie: live memory, all ops no-op) and defers the
    // actual delete so stale holders cannot UAF. Never `delete` a Group outside.
    void DestroyGroup(Group* group);

protected:
    typedef std::vector<bool> GroupIds;
    GroupIds            _groupIds;
    ObjectGuid::LowType _nextGroupId;
    GroupContainer      GroupStore;
    // Zombie graveyard: (group, timestamp). Swept after a grace period.
    std::vector<std::pair<Group*, uint32>> _groupGraveyard;
    void SweepGroupGraveyard();
};

#define sGroupMgr GroupMgr::instance()

#endif
