/*
 * See Licensing and Copyright notice in naev.h
 */


#ifndef NLUA_H
#  define NLUA_H


#include "lua.h"


/*
 * standard lua stuff wrappers
 */
lua_State *nlua_newState (void); /* creates a new state */
int nlua_load( lua_State* L, lua_CFunction f );
int nlua_loadBasic( lua_State* L );


#endif /* NLUA_H */


