/*
 * See Licensing and Copyright notice in naev.h
 */


#ifndef NLUA_VAR
#  define NLUA_VAR

#include "lua.h"


/* checks if a flag exists on the variable stack */
int var_checkflag( char* str );
void var_cleanup (void);

/* individual library stuff */
int lua_loadVar( lua_State *L, int readonly );


#endif /* NLUA_VAR */
