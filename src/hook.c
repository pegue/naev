/*
 * See Licensing and Copyright notice in naev.h
 */


#include "hook.h"

#include <malloc.h>
#include <string.h>

#include "log.h"
#include "naev.h"


/*
 * the hook
 */
typedef struct Hook_ {
	int id;
	Mission* misn;
	char *func;
	char *stack;
} Hook;


/* 
 * the stack
 */
static unsigned int hook_id = 0; /* unique hook id */
static Hook* hook_stack = NULL;
static int hook_mstack = 0;
static int hook_nstack = 0;


/*
 * prototypes
 */
/* extern */
extern int misn_run( Mission *misn, char *func );


/*
 * prototypes
 */
int hook_run( Hook *hook )
{
	if (misn_run( hook->misn, hook->func )) /* error has occured */
		WARN("Hook [%s] '%d' -> '%s' failed", hook->stack,
				hook->id, hook->func);

	return 0;
}


/*
 * add/remove hooks
 */
int hook_add( Mission *misn, char *func, char *stack )
{
	Hook *new_hook;

	/* if memory must grow */
	if (hook_nstack+1 > hook_mstack) {
		hook_stack = realloc(hook_stack, (hook_mstack+5)*sizeof(Hook));
		hook_mstack += 5;
	}

	/* create the new hook */
	new_hook = &hook_stack[hook_nstack];
	new_hook->id = ++hook_id;
	new_hook->misn = misn;
	new_hook->func = func;
	new_hook->stack = stack;

	hook_nstack++;

	return new_hook->id;
}
void hook_rm( int id )
{
	int l,m,h;
	l = 0;
	h = hook_nstack-1;
	while (l <= h) {
		m = (l+h)/2;
		if (hook_stack[m].id > id) h = m-1;
		else if (hook_stack[m].id < id) l = m+1;
		else break;
	}

	if (m == (hook_nstack-1)) {
		hook_nstack--;
		return;
	}

	/* move it! */
	memmove( &hook_stack[m+1], &hook_stack[m+2], sizeof(Hook) * (hook_nstack-(m+2)) );
	hook_nstack--;
}
void hook_rmParent( unsigned int parent )
{
	int i;

	for (i=0; i<hook_nstack; i++)
		if (parent == hook_stack[i].misn->id) {
			hook_rm( hook_stack[i].id );
			i--;
		}
}


/*
 * runs all the hooks of stack
 */
int hooks_run( char* stack )
{
	int i;

	for (i=0; i<hook_nstack; i++)
		if (strcmp(stack, hook_stack[i].stack)==0)
			hook_run( &hook_stack[i] );
	
	return 0;
}


/*
 * clean up after ourselves
 */
void hook_cleanup (void)
{
	free( hook_stack );
	/* sane defaults just in case */
	hook_stack = NULL;
	hook_nstack = 0;
	hook_mstack = 0;
}