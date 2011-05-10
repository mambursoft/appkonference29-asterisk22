/*
 * app_konference
 *
 * A channel independent conference application for Asterisk
 *
 * Copyright (C) 2002, 2003 Junghanns.NET GmbH
 * Copyright (C) 2003, 2004 HorizonLive.com, Inc.
 * Copyright (C) 2005, 2005 Vipadia Limited
 * Copyright (C) 2005, 2006 HorizonWimba, Inc.
 * Copyright (C) 2007 Wimba, Inc.
 *
 * This program may be modified and distributed under the
 * terms of the GNU General Public License. You should have received
 * a copy of the GNU General Public License along with this
 * program; if not, write to the Free Software Foundation, Inc.
 * 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "asterisk.h"

// SVN revision number, provided by make
#ifndef REVISION
#define REVISION "unknown"
#endif

static char *revision = REVISION;

ASTERISK_FILE_VERSION(__FILE__, REVISION)

#include "app_conference.h"
#include "conference.h"
#include "cli.h"

/*
 *
 * a conference has N threads, where N is the number of members
 *
 * each member thread reads frames from its channel adding them 
 * to its frame queue which is read by the conference thread
 *
 * the conference thread reads frames from each speaking members
 * queue, mixes them, and then queues them to the member threads
 *
 */

static char *app = "Konference";
static char *synopsis = "Channel Independent Conference";
static char *descrip = "Channel Independent Conference Application";

static char *app2 = "KonferenceCount";
static char *synopsis2 = "Channel Independent Conference Count";
static char *descrip2 = "Channel Independent Conference Count Application";

#if	ASTERISK == 14 || ASTERISK == 16
static int app_konference_main(struct ast_channel* chan, void* data)
#else
static int app_konference_main(struct ast_channel* chan, const char* data)
#endif
{
	int res ;
	struct ast_module_user *u ;

	u = ast_module_user_add(chan);

	// call member thread function
	res = member_exec( chan, data ) ;

	ast_module_user_remove(u);

	return res ;
}

#if	ASTERISK == 14 || ASTERISK == 16
static int app_konferencecount_main(struct ast_channel* chan, void* data)
#else
static int app_konferencecount_main(struct ast_channel* chan, const char* data)
#endif
{
	int res ;
	struct ast_module_user *u ;

	u = ast_module_user_add(chan);

	// call count thread function
	res = count_exec( chan, data ) ;

	ast_module_user_remove(u);

	return res ;
}

static int unload_module( void )
{
	int res = 0;

	ast_log( LOG_NOTICE, "Unloading app_konference module\n" ) ;

	ast_module_user_hangup_all();

	unregister_conference_cli() ;

	res |= ast_unregister_application( app ) ;
	res |= ast_unregister_application( app2 ) ;

	dealloc_conference() ;

	return res ;
}

static int load_module( void )
{
	int res = 0;

	ast_log( LOG_NOTICE, "Loading app_konference module revision=%s, asterisk version=%.1f\n", revision, (float)ASTERISK/10) ;

	init_conference() ;

	register_conference_cli() ;

	res |= ast_register_application( app, app_konference_main, synopsis, descrip ) ;
	res |= ast_register_application( app2, app_konferencecount_main, synopsis2, descrip2 ) ;

	return res ;
}

#define AST_MODULE "Konference"
AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY,
		"Channel Independent Conference Application");
#undef AST_MODULE

