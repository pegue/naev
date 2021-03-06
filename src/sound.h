/*
 * See Licensing and Copyright notice in naev.h
 */



#ifndef SOUND_H
#  define SOUND_H


extern int sound_disabled;
extern double sound_defVolume;


/*
 * sound subsystem
 */
int sound_init (void);
void sound_exit (void);
int sound_update (void);
void sound_pause (void);
void sound_resume (void);


/*
 * sound manipulation functions
 */
int sound_get( char* name );
int sound_volume( const double vol );
double sound_getVolume (void);
int sound_play( int sound );
int sound_playPos( int sound, double x, double y );
void sound_stop( int voice );
int sound_updatePos( int voice, double x, double y );
int sound_updateListener( double dir, double x, double y );


/*
 * Group functions.
 */
int sound_reserve( int num );
int sound_createGroup( int tag, int start, int size );
int sound_playGroup( int group, int sound, int once );
void sound_stopGroup( int group );


/*
 * Not too portable functions that should be eliminated someday.
 */
void sound_pauseChannel( int num );
void sound_resumeChannel( int num );


#endif /* SOUND_H */
