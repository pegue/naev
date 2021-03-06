/*
 * See Licensing and Copyright notice in naev.h
 */


#ifndef NLUA_PILOT_H
#  define NLUA_PILOT_H


#include "lua.h"

#include "pilot.h"


#define PILOT_METATABLE   "pilot" /**< Pilot metatable identifier. */


/**
 * @brief Lua Pilot wrapper.
 */
typedef struct LuaPilot_s {
   unsigned int pilot; /**< ID of the pilot. */
} LuaPilot; /**< Wrapper for a Pilot. */


/* 
 * Library loading
 */
int lua_loadPilot( lua_State *L, int readonly );

/*
 * Pilot operations
 */
LuaPilot* lua_topilot( lua_State *L, int ind );
LuaPilot* luaL_checkpilot( lua_State *L, int ind );
LuaPilot* lua_pushpilot( lua_State *L, LuaPilot pilot );
int lua_ispilot( lua_State *L, int ind );


#endif /* NLUA_PILOT_H */


