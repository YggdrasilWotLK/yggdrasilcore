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

#ifndef YCORE_DEFINE_H
#define YCORE_DEFINE_H

#include "CompilerDefs.h"
#include <cinttypes>
#include <climits>

#define YCORE_LITTLEENDIAN 0
#define YCORE_BIGENDIAN    1

#if !defined(YCORE_ENDIAN)
#  if defined (BOOST_BIG_ENDIAN)
#    define YCORE_ENDIAN YCORE_BIGENDIAN
#  else
#    define YCORE_ENDIAN YCORE_LITTLEENDIAN
#  endif
#endif

#if YC_PLATFORM == YC_PLATFORM_WINDOWS
#  define YCORE_PATH_MAX MAX_PATH
#  define _USE_MATH_DEFINES
#else //YC_PLATFORM != YC_PLATFORM_WINDOWS
#  define YCORE_PATH_MAX PATH_MAX
#endif //YC_PLATFORM

#if !defined(COREDEBUG)
#  define YCORE_INLINE inline
#else //COREDEBUG
#  if !defined(YCORE_DEBUG)
#    define YCORE_DEBUG
#  endif //YCORE_DEBUG
#  define YCORE_INLINE
#endif //!COREDEBUG

#if YC_COMPILER == YC_COMPILER_GNU
#  define ATTR_PRINTF(F, V) __attribute__ ((format (printf, F, V)))
#else //YC_COMPILER != YC_COMPILER_GNU
#  define ATTR_PRINTF(F, V)
#endif //YC_COMPILER == YC_COMPILER_GNU

#ifdef YCORE_API_USE_DYNAMIC_LINKING
#  if YC_COMPILER == YC_COMPILER_MICROSOFT
#    define YC_API_EXPORT __declspec(dllexport)
#    define YC_API_IMPORT __declspec(dllimport)
#  elif YC_COMPILER == YC_COMPILER_GNU
#    define YC_API_EXPORT __attribute__((visibility("default")))
#    define YC_API_IMPORT
#  else
#    error compiler not supported!
#  endif
#else
#  define YC_API_EXPORT
#  define YC_API_IMPORT
#endif

#ifdef YCORE_API_EXPORT_COMMON
#  define YC_COMMON_API YC_API_EXPORT
#else
#  define YC_COMMON_API YC_API_IMPORT
#endif

#ifdef YCORE_API_EXPORT_DATABASE
#  define YC_DATABASE_API YC_API_EXPORT
#else
#  define YC_DATABASE_API YC_API_IMPORT
#endif

#ifdef YCORE_API_EXPORT_SHARED
#  define YC_SHARED_API YC_API_EXPORT
#else
#  define YC_SHARED_API YC_API_IMPORT
#endif

#ifdef YCORE_API_EXPORT_GAME
#  define YC_GAME_API YC_API_EXPORT
#else
#  define YC_GAME_API YC_API_IMPORT
#endif

#define UI64LIT(N) UINT64_C(N)
#define SI64LIT(N) INT64_C(N)

#define STRING_VIEW_FMT_ARG(str) static_cast<int>((str).length()), (str).data()

typedef std::int64_t int64;
typedef std::int32_t int32;
typedef std::int16_t int16;
typedef std::int8_t int8;
typedef std::uint64_t uint64;
typedef std::uint32_t uint32;
typedef std::uint16_t uint16;
typedef std::uint8_t uint8;

#endif //YCORE_DEFINE_H
