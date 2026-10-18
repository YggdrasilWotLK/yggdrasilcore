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
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _MODULES_LOAD_ORDER_H_
#define _MODULES_LOAD_ORDER_H_

// Load order of a module that does not declare one of its own.
#define YC_MODULE_DEFAULT_LOAD_ORDER 99

// A module overrides the generated default by defining a strong
// Addmod_<identifier>LoadOrder() in its own C++ loader.
#if defined(__GNUC__) || defined(__clang__)
#  define YC_MODULE_LOAD_ORDER_WEAK __attribute__((weak))
#else
#  define YC_MODULE_LOAD_ORDER_WEAK
#endif

// One module entry, ordered by (loadOrder, name) before its scripts load.
struct ModuleLoadEntry
{
    int loadOrder;
    char const* name;
    void (*scripts)();
};

#endif
