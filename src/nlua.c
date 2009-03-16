/*
 * See Licensing and Copyright notice in naev.h
 */

/**
 * @file nlua.c
 *
 * @brief Handles creating and setting up basic Lua environments.
 */

#include "nlua.h"

#include "lauxlib.h"

#include "nluadef.h"
#include "log.h"
#include "naev.h"
#include "ndata.h"


/*
 * prototypes
 */
static int nlua_packfileLoader( lua_State* L );


/**
 * @brief Wrapper around luaL_newstate.
 *
 *    @return A newly created lua_State.
 */
lua_State *nlua_newState (void)
{
   lua_State *L;

   /* try to create the new state */
   L = luaL_newstate();
   if (L == NULL) {
      WARN("Failed to create new lua state.");
      return NULL;
   }

   return L;
}


/**
 * @brief Opens a lua library.
 *
 *    @param L Lua state to load the library into.
 *    @param f CFunction to load.
 */
int nlua_load( lua_State* L, lua_CFunction f )
{
   lua_pushcfunction(L, f);
   if (lua_pcall(L, 0, 0, 0))
      WARN("nlua include error: %s",lua_tostring(L,1));

   return 0;
}


/**
 * @brief Loads specially modified basic stuff.
 *
 *    @param L Lua State to load the basic stuff into.
 *    @return 0 on success.
 */
int nlua_loadBasic( lua_State* L )
{
   int i;
   const char *override[] = { /* unsafe functions */
         "collectgarbage",
         "dofile",
         "getfenv",
         "getmetatable",
         "load",
         "loadfile",
         "loadstring",
         "rawequal",
         "rawget",
         "rawset",
         "setfenv",
         "setmetatable",
         "END"
   };


   nlua_load(L,luaopen_base); /* open base. */

   /* replace non-safe functions */
   for (i=0; strcmp(override[i],"END")!=0; i++) {
      lua_pushnil(L);
      lua_setglobal(L, override[i]);
   }

   nlua_load(L,luaopen_math); /* open math. */
   nlua_load(L,luaopen_table); /* open table. */

   /* add our own */
   lua_register(L, "include", nlua_packfileLoader);

   return 0;
}


/**
 * @brief include( string module )
 *
 * Loads a module into the current Lua state from inside the data file.
 *
 *    @param module Name of the module to load.
 *    @return An error string on error.
 */
static int nlua_packfileLoader( lua_State* L )
{
   char *buf, *filename;
   uint32_t bufsize;

   NLUA_MIN_ARGS(1);

   if (!lua_isstring(L,1)) {
      NLUA_INVALID_PARAMETER();
      return 0;
   }

   filename = (char*) lua_tostring(L,1);

   /* try to locate the data */
   buf = ndata_read( filename, &bufsize );
   if (buf == NULL) {
      lua_pushfstring(L, "%s not found in ndata.", filename);
      return 1;
   }
   
   /* run the buffer */
   if (luaL_dobuffer(L, buf, bufsize, filename) != 0) {
      /* will push the current error from the dobuffer */
      lua_error(L);
      return 1;
   }

   /* cleanup, success */
   free(buf);
   return 0;
}

