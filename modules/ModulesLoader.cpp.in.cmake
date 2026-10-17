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

// This file was created automatically from your script configuration!
// Use CMake to reconfigure this file, never change it on your own!

#cmakedefine ACORE_IS_DYNAMIC_SCRIPTLOADER

#include "Define.h"
#include "ModuleLoadOrder.h"
#include <algorithm>
#include <string>
#include <vector>

// Add deprecated api loaders include
@AC_SCRIPTS_INCLUDES@
// Includes list
@ACORE_SCRIPTS_FORWARD_DECL@
// Load-order accessors (weak default; a module's own loader may define a strong one)
@ACORE_SCRIPTS_LOAD_ORDER_DEFS@
#ifdef ACORE_IS_DYNAMIC_SCRIPTLOADER
#  include "revision.h"
#  define AC_MODULES_API AC_API_EXPORT
extern "C" {

/// Exposed in script module to return the name of the script module
/// contained in this shared library.
AC_MODULES_API char const* GetScriptModule()
{
    return "@ACORE_CURRENT_SCRIPT_PROJECT@";
}

#else
#  include "ModulesScriptLoader.h"
#  define AC_MODULES_API
#endif

/// Exposed in script modules to register all scripts to the ScriptMgr.
AC_MODULES_API void AddModulesScripts()
{
    // Modules, ordered by declared load order then folder name
    std::vector<ModuleLoadEntry> modules =
    {
@ACORE_SCRIPTS_INVOKE@    };
    std::sort(modules.begin(), modules.end(),
        [](ModuleLoadEntry const& a, ModuleLoadEntry const& b)
        {
            if (a.loadOrder != b.loadOrder)
                return a.loadOrder < b.loadOrder;
            return std::string(a.name) < std::string(b.name);
        });
    for (ModuleLoadEntry const& module : modules)
        module.scripts();
    // Deprecated api modules
@AC_SCRIPTS_LIST@}

/// Exposed in script modules to get the build directive of the module.
AC_MODULES_API char const* GetModulesBuildDirective()
{
    return _BUILD_DIRECTIVE;
}

#ifdef ACORE_IS_DYNAMIC_SCRIPTLOADER
} // extern "C"
#endif