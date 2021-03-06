/*
 * See Licensing and Copyright notice in naev.h
 */

/**
 * @file mission.c
 *
 * @brief Handles missions.
 */


#include "mission.h"

#include "naev.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "nlua.h"
#include "nluadef.h"
#include "nlua_space.h"
#include "nlua_faction.h"
#include "rng.h"
#include "log.h"
#include "hook.h"
#include "ndata.h"
#include "nxml.h"
#include "faction.h"
#include "player.h"
#include "base64.h"
#include "space.h"


#define XML_MISSION_ID        "Missions" /**< XML document identifier */
#define XML_MISSION_TAG       "mission" /**< XML mission tag. */

#define MISSION_DATA          "dat/mission.xml" /**< Path to missions XML. */
#define MISSION_LUA_PATH      "dat/missions/" /**< Path to Lua files. */


/*
 * current player missions
 */
static unsigned int mission_id = 0; /**< Mission ID generator. */
Mission player_missions[MISSION_MAX]; /**< Player's active missions. */


/*
 * mission stack
 */
static MissionData *mission_stack = NULL; /**< Unmuteable after creation */
static int mission_nstack = 0; /**< Mssions in stack. */


/*
 * prototypes
 */
/* static */
static unsigned int mission_genID (void);
static int mission_init( Mission* mission, MissionData* misn, int load );
static void mission_freeData( MissionData* mission );
static int mission_alreadyRunning( MissionData* misn );
static int mission_meetCond( MissionData* misn );
static int mission_meetReq( int mission, int faction,
      const char* planet, const char* sysname );
static int mission_matchFaction( MissionData* misn, int faction );
static int mission_location( const char* loc );
static MissionData* mission_parse( const xmlNodePtr parent );
static int missions_parseActive( xmlNodePtr parent );
static int mission_persistDataNode( lua_State *L, xmlTextWriterPtr writer, int intable );
static int mission_persistData( lua_State *L, xmlTextWriterPtr writer );
static int mission_unpersistDataNode( lua_State *L, xmlNodePtr parent );
static int mission_unpersistData( lua_State *L, xmlNodePtr parent );
/* externed */
int missions_saveActive( xmlTextWriterPtr writer );
int missions_loadActive( xmlNodePtr parent );


/**
 * @brief Generates a new id for the mission.
 *
 *    @return New id for the mission.
 */
static unsigned int mission_genID (void)
{
   unsigned int id;
   int i;
   id = ++mission_id; /* default id, not safe if loading */

   /* we save mission ids, so check for collisions with player's missions */
   for (i=0; i<MISSION_MAX; i++)
      if (id == player_missions[i].id) /* mission id was loaded from save */
         return mission_genID(); /* recursively try again */
   return id;
}

/**
 * @brief Gets id from mission name.
 *
 *    @param name Name to match.
 *    @return id of the matching mission.
 */
int mission_getID( const char* name )
{
   int i;

   for (i=0; i<mission_nstack; i++)
      if (strcmp(name,mission_stack[i].name)==0)
         return i;

   DEBUG("Mission '%s' not found in stack", name);
   return -1;
}


/**
 * @brief Gets a MissionData based on ID.
 *
 *    @param id ID to match.
 *    @return MissonData matching ID.
 */
MissionData* mission_get( int id )
{
   if ((id < 0) || (id >= mission_nstack)) return NULL;
   return &mission_stack[id];
}


/**
 * @brief Initializes a mission.
 *
 *    @param mission Mission to initialize.
 *    @param misn Data to use.
 *    @param load 1 if loading mision from save file, otherwise 0.
 *    @return ID of the newly created mission.
 */
static int mission_init( Mission* mission, MissionData* misn, int load )
{
   int i;
   char *buf;
   uint32_t bufsize;

   /* clear the mission */
   memset(mission,0,sizeof(Mission));

   /* we only need an id if not loading */
   if (load != 0)
      mission->id = 0;
   else
      mission->id = mission_genID();
   mission->data = misn;

   /* Init the timers. */
   for (i=0; i<MISSION_TIMER_MAX; i++) {
      mission->timer[i] = 0.;
      mission->tfunc[i] = NULL;
   }

   /* init lua */
   mission->L = nlua_newState();
   if (mission->L == NULL) {
      ERR("Unable to create a new lua state.");
      return -1;
   }
   nlua_loadBasic( mission->L ); /* pairs and such */
   misn_loadLibs( mission->L ); /* load our custom libraries */

   /* load the file */
   buf = ndata_read( misn->lua, &bufsize );
   if (buf == NULL) {
      WARN("Mission '%s' Lua script not found.", misn->lua );
      return -1;
   }
   if (luaL_dobuffer(mission->L, buf, bufsize, misn->lua) != 0) {
      WARN("Error loading mission file: %s\n"
          "%s\n"
          "Most likely Lua file has improper syntax, please check",
            misn->lua, lua_tostring(mission->L,-1));
      return -1;
   }
   free(buf);

   /* run create function */
   if (load == 0) { /* never run when loading */
      /* Failed to create. */
      if (misn_run( mission, "create")) {
         mission_cleanup(mission);
         return -1;
      }
   }

   return mission->id;
}


/**
 * @brief Small wrapper for misn_run.
 *
 *    @param mission Mission to accept.
 *    @return 0 on success.
 *
 * @sa misn_run
 */
int mission_accept( Mission* mission )
{
   int ret;
   ret = misn_run( mission, "accept" );
   if (ret==0) return 1;
   return 0;
}


/**
 * @brief Checks to see if mission is already running.
 *
 *    @param misn Mission to check if is already running.
 *    @return 1 if already running, 0 if isn't.
 */
static int mission_alreadyRunning( MissionData* misn )
{
   int i;
   for (i=0; i<MISSION_MAX; i++)
      if (player_missions[i].data==misn)
         return 1;
   return 0;
}


static lua_State* mission_cond_L = NULL; /**< Mission conditional Lua state. */
/**
 * @brief Checks to see if the mission conditional data is met.
 *
 *    @param misn Mission to check.
 *    @return 1 if is met, 0 if it isn't.
 */
static int mission_meetCond( MissionData* misn )
{
   int b;
   int ret;
   char buf[256];

   /* Create environment if needed. */
   if (mission_cond_L == NULL) { /* must create the conditional environment */
      mission_cond_L = nlua_newState();
      misn_loadCondLibs( mission_cond_L );
   }

   /* Load the string. */ 
   snprintf( buf, 256, "return %s", misn->avail.cond ); /* Must convert to Lua syntax */
   ret = luaL_loadstring( mission_cond_L, buf );
   switch (ret) {
      case  LUA_ERRSYNTAX:
         WARN("Mission '%s' Lua conditional syntax error", misn->name );
         return 0;
      case LUA_ERRMEM:
         WARN("Mission '%s' Lua Conditional ran out of memory", misn->name );
         return 0;
      default:
         break;
   }

   /* Run the string. */
   ret = lua_pcall( mission_cond_L, 0, 1, 0 );
   switch (ret) {
      case LUA_ERRRUN:
         WARN("Mission '%s' Lua Conditional had a runtime error: %s",
               misn->name, lua_tostring(mission_cond_L, -1));
         return 0;
      case LUA_ERRMEM:
         WARN("Mission '%s' Lua Conditional ran out of memory", misn->name);
         return 0;
      case LUA_ERRERR:
         WARN("Mission '%s' Lua Conditional had an error while handling error function",
               misn->name);
         return 0;
      default:
         break;
   }

   /* Check the result. */
   if (lua_isboolean(mission_cond_L, -1)) {
      b = lua_toboolean(mission_cond_L, -1);
      lua_pop(mission_cond_L, 1);
      if (b)
         return 1;
      else
         return 0;
   }
   WARN("Mission '%s' Conditional Lua didn't return a boolean", misn->name);
   return 0;
}


/**
 * @brief Checks to see if a mission meets the requirements.
 *
 *    @param mission ID of the mission to check.
 *    @param faction Faction of the current planet.
 *    @param planet Name of the current planet.
 *    @param sysname Name of the current system.
 *    @return 1 if requirements are met, 0 if they aren't.
 */
static int mission_meetReq( int mission, int faction,
      const char* planet, const char* sysname )
{
   MissionData* misn;

   misn = mission_get( mission );
   if (misn == NULL) /* In case it doesn't exist */
      return 0;

   /* Must match planet, system or faction. */
   if (!((misn->avail.planet && (strcmp(misn->avail.planet,planet)==0)) ||
         (misn->avail.system && (strcmp(misn->avail.system,sysname)==0)) ||
         mission_matchFaction(misn,faction)))
      return 0;

   /* Must not be already done or running if unique. */
   if (mis_isFlag(misn,MISSION_UNIQUE) &&
         (player_missionAlreadyDone(mission) ||
          mission_alreadyRunning(misn)))
      return 0;

   /* Must meet Lua condition. */
   if ((misn->avail.cond != NULL) &&
         !mission_meetCond(misn))
      return 0;

   /* Must meet previous mission requirements. */
   if ((misn->avail.done != NULL) &&
         (player_missionAlreadyDone( mission_getID(misn->avail.done) ) == 0))
      return 0;

  return 1;
}


/**
 * @brief Runs missions matching location, all Lua side and one-shot.
 *
 *    @param loc Location to match.
 *    @param faction Faction of the planet.
 *    @param planet Name of the current planet.
 *    @param sysname Name of the current system.
 */
void missions_run( int loc, int faction, const char* planet, const char* sysname )
{
   MissionData* misn;
   Mission mission;
   int i;
   double chance;

   for (i=0; i<mission_nstack; i++) {
      misn = &mission_stack[i];
      if (misn->avail.loc==loc) {

         if (!mission_meetReq(i, faction, planet, sysname))
            continue;

         chance = (double)(misn->avail.chance % 100)/100.;
         if (chance == 0.) /* We want to consider 100 -> 100% not 0% */
            chance = 1.;

         if (RNGF() < chance) {
            mission_init( &mission, misn, 0 );
            mission_cleanup(&mission); /* it better clean up for itself or we do it */
         }
      }
   }
}


/**
 * @brief Starts a mission.
 *
 *  Mission must still call misn.accept() to actually get added to the player's
 * active missions.
 *
 *    @param name Name of the mission to start.
 *    @return 0 on success.
 */
int mission_start( const char *name )
{
   Mission mission;
   MissionData *mdat;

   /* Try to get the mission. */
   mdat = mission_get( mission_getID(name) );
   if (mdat == NULL)
      return -1;

   /* Try to run the mission. */
   mission_init( &mission, mdat, 0 );
   mission_cleanup( &mission ); /* Clean up in case not accepted. */

   return 0;
}


/**
 * @brief Marks all active systems that need marking.
 */
void mission_sysMark (void)
{
   int i;

   space_clearMarkers();

   for (i=0; i<MISSION_MAX; i++) {
      if ((player_missions[i].id != 0) &&
            (player_missions[i].sys_marker != NULL)) {
         space_addMarker(player_missions[i].sys_marker,
               player_missions[i].sys_markerType);
      }
   }
}


/**
 * @brief Marks the system of the computer mission to reflect where it will head to.
 *
 * Does not modify other markers.
 *
 *    @param misn Mission to mark.
 */
void mission_sysComputerMark( Mission* misn )
{
   StarSystem *sys;

   space_clearComputerMarkers();

   if (misn->sys_marker != NULL) {
      sys = system_get(misn->sys_marker);
      sys_setFlag(sys,SYSTEM_CMARKED);
   }
}


/**
 * @brief Links cargo to the mission for posterior cleanup.
 *
 *    @param misn Mission to link cargo to.
 *    @param cargo_id ID of cargo to link.
 *    @return 0 on success.
 */
int mission_linkCargo( Mission* misn, unsigned int cargo_id )
{
   misn->ncargo++;
   misn->cargo = realloc( misn->cargo, sizeof(unsigned int) * misn->ncargo);
   misn->cargo[ misn->ncargo-1 ] = cargo_id;

   return 0;
}


/**
 * @brief Unlinks cargo from the mission, removes it from the player.
 *
 *    @param misn Mission to unlink cargo from.
 *    @param cargo_id ID of cargo to unlink.
 *    @return returns 0 on success.
 */
int mission_unlinkCargo( Mission* misn, unsigned int cargo_id )
{
   int i;
   for (i=0; i<misn->ncargo; i++)
      if (misn->cargo[i] == cargo_id)
         break;

   if (i>=misn->ncargo) { /* not found */
      DEBUG("Mission '%s' attempting to unlink inexistant cargo %d.",
            misn->title, cargo_id);
      return 1;
   }

   /* shrink cargo size - no need to realloc */
   memmove( &misn->cargo[i], &misn->cargo[i+1],
         sizeof(unsigned int) * (misn->ncargo-i-1) );
   misn->ncargo--;

   return 0;
}



/**
 * @brief Updates the missions triggering timers if needed.
 *
 *    @param dt Current deltatick.
 */
void missions_update( const double dt )
{
   int i,j;

   /* Don't update if player is dead. */
   if ((player==NULL) || player_isFlag(PLAYER_DESTROYED))
      return;

   for (i=0; i<MISSION_MAX; i++) {

      /* Mission must be active. */
      if (player_missions[i].id != 0) {
         for (j=0; j<MISSION_TIMER_MAX; j++) {

            /* Timer must be active. */
            if (player_missions[i].timer[j] > 0.) {

               player_missions[i].timer[j] -= dt;

               /* Timer is up - trigger function. */
               if (player_missions[i].timer[j] < 0.) {
                  misn_run( &player_missions[i], player_missions[i].tfunc[j] );
                  player_missions[i].timer[j] = 0.;
                  free(player_missions[i].tfunc[j]);
                  player_missions[i].tfunc[j] = NULL;
               }
            }
         }
      }
   }
}


/**
 * @brief Cleans up a mission.
 *
 *    @param misn Mission to clean up.
 */
void mission_cleanup( Mission* misn )
{
   int i;

   if (misn->id != 0)
      hook_rmParent( misn->id ); /* remove existing hooks */
   if (misn->title != NULL)
      free(misn->title);
   if (misn->desc != NULL)
      free(misn->desc);
   if (misn->reward != NULL)
      free(misn->reward);
   if (misn->sys_marker != NULL)
      free(misn->sys_marker);
   if (misn->cargo != NULL) {
      for (i=0; i<misn->ncargo; i++) { /* must unlink all the cargo */
         if (player != NULL) /* Only remove if player exists. */
            pilot_rmMissionCargo( player, misn->cargo[i], 0 );
         mission_unlinkCargo( misn, misn->cargo[i] );
      }
      free(misn->cargo);
   }
   for (i=0; i<MISSION_TIMER_MAX; i++) {
      if (misn->tfunc[i] != NULL)
         free(misn->tfunc[i]);
   }
   if (misn->L)
      lua_close(misn->L);

   /* Clear the memory. */
   memset( misn, 0, sizeof(Mission) );
}


/**
 * @brief Frees MissionData.
 *
 *    @param mission MissionData to free.
 */
static void mission_freeData( MissionData* mission )
{
   if (mission->name) free(mission->name);
   if (mission->lua) free(mission->lua);
   if (mission->avail.planet) free(mission->avail.planet);
   if (mission->avail.system) free(mission->avail.system);
   if (mission->avail.factions) free(mission->avail.factions);
   if (mission->avail.cond) free(mission->avail.cond);
   if (mission->avail.done) free(mission->avail.done);
   memset( mission, 0, sizeof(MissionData) );
}


/**
 * @brief Checks to see if a mission matches the faction requirements.
 *
 *    @param misn Mission to check.
 *    @param faction Faction to check against.
 *    @return 1 if it meets the faction requirement, 0 if it doesn't.
 */
static int mission_matchFaction( MissionData* misn, int faction )
{
   int i;

   for (i=0; i<misn->avail.nfactions; i++)
      if (faction == misn->avail.factions[i])
         return 1;

   return 0;
}


/**
 * @brief Generates misisons for the computer - special case.
 *
 *    @param[out] n Missions created.
 *    @param faction Faction of the planet.
 *    @param planet Name of the planet.
 *    @param sysname Name of the current system.
 *    @return The stack of Missions created with n members.
 */
Mission* missions_computer( int *n, int faction,
      const char* planet, const char* sysname )
{
   int i,j, m;
   double chance;
   int rep;
   Mission* tmp;
   MissionData* misn;

   tmp = NULL;
   m = 0;
   for (i=0; i<mission_nstack; i++) {
      misn = &mission_stack[i];
      if (misn->avail.loc==MIS_AVAIL_COMPUTER) {

         if (!mission_meetReq(i, faction, planet, sysname))
            continue;

         chance = (double)(misn->avail.chance % 100)/100.;
         rep = misn->avail.chance / 100;

         for (j=0; j<rep; j++) /* random chance of rep appearances */
            if (RNGF() < chance) {
               m++;
               tmp = realloc( tmp, sizeof(Mission) * m);
               if (mission_init( &tmp[m-1], misn, 0 ) < 0)
                  m--;
            }
      }
   }

   (*n) = m;
   return tmp;
}


/**
 * @brief Gets location based on a human readable string.
 *
 *    @param loc String to get the location of.
 *    @return Location matching loc.
 */
static int mission_location( const char* loc )
{
   if (strcmp(loc,"None")==0) return MIS_AVAIL_NONE;
   else if (strcmp(loc,"Computer")==0) return MIS_AVAIL_COMPUTER;
   else if (strcmp(loc,"Bar")==0) return MIS_AVAIL_BAR;
   else if (strcmp(loc,"Outfit")==0) return MIS_AVAIL_OUTFIT;
   else if (strcmp(loc,"Shipyard")==0) return MIS_AVAIL_SHIPYARD;
   else if (strcmp(loc,"Land")==0) return MIS_AVAIL_LAND;
   else if (strcmp(loc,"Commodity")==0) return MIS_AVAIL_COMMODITY;
   return -1;
}


/**
 * @brief Parses a node of a mission.
 *
 *    @param parent Node containing the mission.
 *    @return The MissionData extracted from parent.
 */
static MissionData* mission_parse( const xmlNodePtr parent )
{
   MissionData *temp;
   xmlNodePtr cur, node;

   temp = malloc(sizeof(MissionData));
   memset( temp, 0, sizeof(MissionData) );

   /* get the name */
   temp->name = xml_nodeProp(parent,"name");
   if (temp->name == NULL) WARN("Mission in "MISSION_DATA" has invalid or no name");

   node = parent->xmlChildrenNode;

   char str[PATH_MAX] = "\0";

   do { /* load all the data */
      if (xml_isNode(node,"lua")) {
         snprintf( str, PATH_MAX, MISSION_LUA_PATH"%s.lua", xml_get(node) );
         temp->lua = strdup( str );
         str[0] = '\0';
      }
      else if (xml_isNode(node,"flags")) { /* set the various flags */
         cur = node->children;
         do {
            if (xml_isNode(cur,"unique"))
               mis_setFlag(temp,MISSION_UNIQUE);
         } while (xml_nextNode(cur));
      }
      else if (xml_isNode(node,"avail")) { /* mission availability */
         cur = node->children;
         do {
            if (xml_isNode(cur,"location")) {
               temp->avail.loc = mission_location( xml_get(cur) );
               continue;
            }
            xmlr_int(cur,"chance",temp->avail.chance);
            xmlr_strd(cur,"planet",temp->avail.planet);
            xmlr_strd(cur,"system",temp->avail.system);
            if (xml_isNode(cur,"faction")) {
               temp->avail.factions = realloc( temp->avail.factions, 
                     sizeof(int) * ++temp->avail.nfactions );
               temp->avail.factions[temp->avail.nfactions-1] =
                     faction_get( xml_get(cur) );
               continue;
            }
            xmlr_strd(cur,"cond",temp->avail.cond);
            xmlr_strd(cur,"done",temp->avail.done);
         } while (xml_nextNode(cur));
      }
   } while (xml_nextNode(node));

#define MELEMENT(o,s) \
   if (o) WARN("Mission '%s' missing/invalid '"s"' element", temp->name)
   MELEMENT(temp->lua==NULL,"lua");
   MELEMENT(temp->avail.loc==-1,"location");
#undef MELEMENT

   return temp;
}


/**
 * @brief Loads all the mission data.
 *
 *    @return 0 on success.
 */
int missions_load (void)
{
   uint32_t bufsize;
   char *buf = ndata_read( MISSION_DATA, &bufsize );

   MissionData *temp;

   xmlNodePtr node;
   xmlDocPtr doc = xmlParseMemory( buf, bufsize );

   node = doc->xmlChildrenNode;
   if (!xml_isNode(node,XML_MISSION_ID)) {
      ERR("Malformed '"MISSION_DATA"' file: missing root element '"XML_MISSION_ID"'");
      return -1;
   }

   node = node->xmlChildrenNode; /* first mission node */
   if (node == NULL) {
      ERR("Malformed '"MISSION_DATA"' file: does not contain elements");
      return -1;
   }

   do {
      if (xml_isNode(node,XML_MISSION_TAG)) {

         temp = mission_parse(node);
         mission_stack = realloc(mission_stack, sizeof(MissionData)*(++mission_nstack));
         memcpy(mission_stack+mission_nstack-1, temp, sizeof(MissionData));
         free(temp);
      }
   } while (xml_nextNode(node));

   xmlFreeDoc(doc);
   free(buf);

   DEBUG("Loaded %d Mission%s", mission_nstack, (mission_nstack==1) ? "" : "s" );

   return 0;
}


/**
 * @brief Frees all the mission data.
 */
void missions_free (void)
{
   int i;

   /* free the mission data */
   for (i=0; i<mission_nstack; i++)
      mission_freeData( &mission_stack[i] );
   free( mission_stack );
   mission_stack = NULL;
   mission_nstack = 0;

   /* frees the lua stack */
   if (mission_cond_L != NULL) {
      lua_close( mission_cond_L );
      mission_cond_L = NULL;
   }
}


/**
 * @brief Cleans up all the player's active missions.
 */
void missions_cleanup (void)
{
   int i;

   for (i=0; i<MISSION_MAX; i++)
      mission_cleanup( &player_missions[i] );
}



/**
 * @brief Persists Lua data.
 *
 *    @param writer XML Writer to use to persist stuff.
 *    @param type Type of the data to save.
 *    @param name Name of the data to save.
 *    @param value Value of the data to save.
 *    @return 0 on success.
 */
static int mission_saveData( xmlTextWriterPtr writer,
      const char *type, const char *name, const char *value,
      int keynum )
{
   xmlw_startElem(writer,"data");

   xmlw_attr(writer,"type",type);
   xmlw_attr(writer,"name",name);
   if (keynum)
      xmlw_attr(writer,"keynum","1");
   xmlw_str(writer,"%s",value);

   xmlw_endElem(writer); /* "data" */

   return 0;
}


/**
 * @brief Persists the node on the top of the stack and pops it.
 *
 *    @param L Lua state with node to persist on top of the stack.
 *    @param writer XML Writer to use.
 *    @param Are we parsing a node in a table?  Avoids checking for extra __save.
 *    @return 0 on success.
 */
static int mission_persistDataNode( lua_State *L, xmlTextWriterPtr writer, int intable )
{
   int ret, b;
   LuaPlanet *p;
   LuaSystem *s;
   LuaFaction *f;
   char buf[PATH_MAX];
   const char *name, *str;
   int keynum;

   /* Default values. */
   ret   = 0;

   /* key, value */
   /* Handle different types of keys. */
   switch (lua_type(L, -2)) {
      case LUA_TSTRING:
         /* Can just tostring directly. */
         name     = lua_tostring(L,-2);
         /* Isn't a number key. */
         keynum   = 0;
         break;
      case LUA_TNUMBER:
         /* Can't tostring directly. */
         lua_pushvalue(L,-2);
         name     = lua_tostring(L,-1);
         lua_pop(L,1);
         /* Is a number key. */
         keynum   = 1;
         break;

      /* We only handle string or number keys, so ignore the rest. */
      default:
         lua_pop(L,1);
         /* key */
         return 0;
   }
   /* key, value */

   /* Now handle the value. */
   switch (lua_type(L, -1)) {
      /* Recursive for tables. */
      case LUA_TTABLE:
         /* Check if should save -- only if not in table.. */
         if (!intable) {
            lua_getfield(L, -1, "__save");
            b = lua_toboolean(L,-1);
            lua_pop(L,1);
            if (!b) /* No need to save. */
               break;
         }
         /* Start the table. */
         xmlw_startElem(writer,"data");
         xmlw_attr(writer,"type","table");
         xmlw_attr(writer,"name",name);
         if (keynum)
            xmlw_attr(writer,"keynum","1");
         lua_pushnil(L); /* key, value, nil */
         /* key, value, nil */
         while (lua_next(L, -2) != 0) {
            /* key, value, key, value */
            ret = mission_persistDataNode( L, writer, 1 );
            /* key, value, key */
         }
         /* key, value */
         xmlw_endElem(writer); /* "table" */
         break;

      /* Normal number. */
      case LUA_TNUMBER:
         mission_saveData( writer, "number",
               name, lua_tostring(L,-1), keynum );
         /* key, value */
         break;

      /* Boolean is either 1 or 0. */
      case LUA_TBOOLEAN:
         /* lua_tostring doesn't work on booleans. */
         if (lua_toboolean(L,-1)) buf[0] = '1';
         else buf[0] = '0';
         buf[1] = '\0';
         mission_saveData( writer, "bool",
               name, buf, keynum );
         /* key, value */
         break;

      /* String is saved normally. */
      case LUA_TSTRING:
         mission_saveData( writer, "string",
               name, lua_tostring(L,-1), keynum );
         /* key, value */
         break;

      /* User data must be handled here. */
      case LUA_TUSERDATA:
         if (lua_isplanet(L,-1)) {
            p = lua_toplanet(L,-1);
            mission_saveData( writer, "planet",
                  name, p->p->name, keynum );
            /* key, value */
            break;
         }
         else if (lua_issystem(L,-1)) {
            s = lua_tosystem(L,-1);
            mission_saveData( writer, "system",
                  name, s->s->name, keynum );
            /* key, value */
            break;
         }
         else if (lua_isfaction(L,-1)) {
            f = lua_tofaction(L,-1);
            str = faction_name( f->f );
            if (str == NULL)
               break;
            mission_saveData( writer, "faction",
                  name, str, keynum );
            /* key, value */
            break;
         }

      /* Rest gets ignored, like functions, etc... */
      default:
         /* key, value */
         break;
   }
   lua_pop(L,1);
   /* key */

   return ret;
}


/**
 * @brief Persists all the mission Lua data.
 *
 * Does not save anything in tables nor functions of any type.
 *
 *    @param L Lua state to save.
 *    @param writer XML Writer to use.
 *    @return 0 on success.
 */
static int mission_persistData( lua_State *L, xmlTextWriterPtr writer )
{
   int ret;

   lua_pushstring(L,"_G");
   lua_pushnil(L);
   /* str, nil */
   while (lua_next(L, LUA_GLOBALSINDEX) != 0) {
      /* str, key, value */
      ret = mission_persistDataNode( L, writer, 0 );
      /* str, key */
   }
   /* str */
   lua_pop(L,1);

   return ret;
}


/**
 * @brief Unpersists Lua data.
 *
 *    @param L State to unperisist data into.
 *    @param parent Node containing all the Lua persisted data.
 *    @return 0 on success.
 */
static int mission_unpersistDataNode( lua_State *L, xmlNodePtr parent )
{
   LuaPlanet p;
   LuaSystem s;
   LuaFaction f;
   xmlNodePtr node;
   char *name, *type, *buf, *num;
   int keynum;

   node = parent->xmlChildrenNode;
   do {
      if (xml_isNode(node,"data")) {
         /* Get general info. */
         xmlr_attr(node,"name",name);
         xmlr_attr(node,"type",type);
         /* Check to see if key is a number. */
         xmlr_attr(node,"keynum",num);
         if (num != NULL) {
            keynum = 1;
            lua_pushnumber(L, atof(name));
            free(num);
         }
         else
            lua_pushstring(L, name);

         /* handle data types */
         /* Recursive tables. */
         if (strcmp(type,"table")==0) {
            xmlr_attr(node,"name",buf);
            /* Create new table. */
            lua_newtable(L);
            /* Save data. */
            mission_unpersistDataNode(L,node);
            /* Set table. */
            free(buf);
         }
         else if (strcmp(type,"number")==0)
            lua_pushnumber(L,xml_getFloat(node));
         else if (strcmp(type,"bool")==0)
            lua_pushboolean(L,xml_getInt(node));
         else if (strcmp(type,"string")==0)
            lua_pushstring(L,xml_get(node));
         else if (strcmp(type,"planet")==0) {
            p.p = planet_get(xml_get(node));
            lua_pushplanet(L,p);
         }
         else if (strcmp(type,"system")==0) {
            s.s = system_get(xml_get(node));
            lua_pushsystem(L,s);
         }
         else if (strcmp(type,"faction")==0) {
            f.f = faction_get(xml_get(node));
            lua_pushfaction(L,f);
         }
         else {
            WARN("Unknown lua data type!");
            lua_pop(L,1);
            return -1;
         }

         /* Set field. */
         lua_settable(L, -3);
         
         /* cleanup */
         free(type);
         free(name);
      }
   } while (xml_nextNode(node));

   return 0;
}


/**
 * @brief Unpersists Lua data.
 *
 *    @param L State to unperisist data into.
 *    @param parent Node containing all the Lua persisted data.
 *    @return 0 on success.
 */
static int mission_unpersistData( lua_State *L, xmlNodePtr parent )
{
   int ret;

   lua_pushvalue(L,LUA_GLOBALSINDEX);
   ret = mission_unpersistDataNode(L,parent);
   lua_pop(L,1);

   return ret;
}


/**
 * @brief Saves the player's active missions.
 *
 *    @param writer XML Write to use to save missions.
 *    @return 0 on success.
 */
int missions_saveActive( xmlTextWriterPtr writer )
{
   int i,j;

   xmlw_startElem(writer,"missions");

   for (i=0; i<MISSION_MAX; i++) {
      if (player_missions[i].id != 0) {
         xmlw_startElem(writer,"mission");

         /* data and id are attributes becaues they must be loaded first */
         xmlw_attr(writer,"data",player_missions[i].data->name);
         xmlw_attr(writer,"id","%u",player_missions[i].id);

         xmlw_elem(writer,"title",player_missions[i].title);
         xmlw_elem(writer,"desc",player_missions[i].desc);
         xmlw_elem(writer,"reward",player_missions[i].reward);
         if (player_missions[i].sys_marker != NULL) {
            xmlw_startElem(writer,"marker");
            xmlw_attr(writer,"type","%d",player_missions[i].sys_markerType);
            xmlw_str(writer,player_missions[i].sys_marker);
            xmlw_endElem(writer); /* "marker" */
         }

         /* Cargo */
         xmlw_startElem(writer,"cargos");
         for (j=0; j<player_missions[i].ncargo; j++)
            xmlw_elem(writer,"cargo","%u", player_missions[i].cargo[j]);
         xmlw_endElem(writer); /* "cargos" */

         /* Timers. */
         xmlw_startElem(writer,"timers");
         for (j=0; j<MISSION_TIMER_MAX; j++) {
            if (player_missions[i].timer[j] > 0.) {
               xmlw_startElem(writer,"timer");
              
               xmlw_attr(writer,"id","%d",j);
               xmlw_attr(writer,"func","%s",player_missions[i].tfunc[j]);
               xmlw_str(writer,"%f",player_missions[i].timer[j]);

               xmlw_endElem(writer); /* "timer" */
            }
         }
         xmlw_endElem(writer); /* "timers" */

         /* write lua magic */
         xmlw_startElem(writer,"lua");

         /* prepare the data */
         mission_persistData(player_missions[i].L, writer);

         xmlw_endElem(writer); /* "lua" */

         xmlw_endElem(writer); /* "mission" */
      }
   }

   xmlw_endElem(writer); /* "missions" */

   return 0;
}


/**
 * @brief Loads the player's active missions from a save.
 *
 *    @param parent Node containing the player's active missions.
 *    @return 0 on success.
 */
int missions_loadActive( xmlNodePtr parent )
{
   xmlNodePtr node;

   /* cleanup old missions */
   missions_cleanup();

   node = parent->xmlChildrenNode;
   do {
      if (xml_isNode(node,"missions"))
         if (missions_parseActive( node ) < 0) return -1;
   } while (xml_nextNode(node));

   return 0;
}


/**
 * @brief Parses the actual individual mission nodes.
 *
 *    @param parent Parent node to parse.
 *    @return 0 on success.
 */
static int missions_parseActive( xmlNodePtr parent )
{
   Mission *misn;
   MissionData *data;
   int m, i;
   char *buf;

   xmlNodePtr node, cur, nest;

   m = 0; /* start with mission 0 */
   node = parent->xmlChildrenNode;
   do {
      if (xml_isNode(node,"mission")) {
         misn = &player_missions[m];

         /* process the attributes to create the mission */
         xmlr_attr(node,"data",buf);
         data = mission_get(mission_getID(buf));
         if (data == NULL) {
            WARN("Mission '%s' from savegame not found in game - ignoring.", buf);
            free(buf);
            continue;
         }
         else
            mission_init( misn, data, 1 );
         free(buf);

         /* this will orphan an identifier */
         xmlr_attr(node,"id",buf);
         misn->id = atol(buf);
         free(buf);

         cur = node->xmlChildrenNode;
         do {

            xmlr_strd(cur,"title",misn->title);
            xmlr_strd(cur,"desc",misn->desc);
            xmlr_strd(cur,"reward",misn->reward);

            /* Get the marker. */
            if (xml_isNode(cur,"marker")) {
               xmlr_attr(cur,"type",buf);
               misn->sys_markerType = (buf != NULL) ? atoi(buf) : 0;
               if (buf != NULL)
                  free(buf);
               misn->sys_marker = xml_getStrd(cur);
            }

            /* Cargo. */
            if (xml_isNode(cur,"cargos")) {
               nest = cur->xmlChildrenNode;
               do {
                  if (xml_isNode(nest,"cargo"))
                     mission_linkCargo( misn, xml_getLong(nest) );
               } while (xml_nextNode(nest));
            }

            /* Timers. */
            if (xml_isNode(cur,"timers")) {
               nest = cur->xmlChildrenNode;
               do {
                  if (xml_isNode(nest,"timer")) {
                     xmlr_attr(nest,"id",buf);
                     i = atoi(buf);
                     free(buf);
                     xmlr_attr(nest,"func",buf);
                     /* Set the timer. */
                     misn->timer[i] = xml_getFloat(nest);
                     misn->tfunc[i] = buf;
                  }
               } while (xml_nextNode(nest));
            }

            if (xml_isNode(cur,"lua"))
               /* start the unpersist routine */
               mission_unpersistData(misn->L, cur);

         } while (xml_nextNode(cur));



         m++; /* next mission */
         if (m >= MISSION_MAX) break; /* full of missions, must be an error */
      }
   } while (xml_nextNode(node));

   return 0;
}


