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

#ifndef __GITREVISION_H__
#define __GITREVISION_H__

#include "Define.h"

namespace GitRevision
{
    YC_COMMON_API char const* GetHash();
    YC_COMMON_API char const* GetDate();
    YC_COMMON_API char const* GetBranch();
    YC_COMMON_API char const* GetCMakeCommand();
    YC_COMMON_API char const* GetCMakeVersion();
    YC_COMMON_API char const* GetHostOSVersion();
    YC_COMMON_API char const* GetBuildDirectory();
    YC_COMMON_API char const* GetSourceDirectory();
    YC_COMMON_API char const* GetMySQLExecutable();
    YC_COMMON_API char const* GetFullVersion();
    YC_COMMON_API char const* GetCompanyNameStr();
    YC_COMMON_API char const* GetLegalCopyrightStr();
    YC_COMMON_API char const* GetFileVersionStr();
    YC_COMMON_API char const* GetProductVersionStr();
}

#endif
