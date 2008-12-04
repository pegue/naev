/*
 * See Licensing and Copyright notice in naev.h
 */

/**
 * @file nxml.c
 *
 * Handles some complex xml parsing.
 */


#include "nxml.h"

#include "naev.h"


/**
 * @brief Parses a texture handling the sx and sy elements.
 *
 *    @param node Node to parse.
 *    @param path Path to get file from, should be in the format of
 *           "PREFIX%sSUFFIX".
 *    @param defsx Default X sprites.
 *    @param defsy Default Y sprites.
 *    @return The texture from the node or NULL if an error occurred.
 */
glTexture* xml_parseTexture( xmlNodePtr node,
      const char *path, int defsx, int defsy )
{
   int sx, sy;
   char *buf, filename[PATH_MAX];
   glTexture *tex;

   /*
    * Sane defaults.
    */
   sx = defsx;
   sy = defsy;

   /* Read x sprites. */
   xmlr_attr(node, "sx", buf );
   if (buf != NULL) {
      sx = atoi(buf);
      free(buf);
   }

   /* Read y sprites. */
   xmlr_attr(node, "sy", buf );
   if (buf != NULL) {
      sy = atoi(buf);
      free(buf);
   }

   /* Get graphic to load. */
   buf = xml_get( node );
   if (buf == NULL)
      return NULL;

   /* Convert name. */
   snprintf( filename, PATH_MAX, (path != NULL) ? path : "%s", buf );

   /* Load the graphic. */
   if ((sx == 1) && (sy == 1))
      tex = gl_newImage( filename );
   else
      tex = gl_newSprite( filename, sx, sy );

   /* Return result. */
   return tex;
}

