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

#include "asterisk/autoconfig.h"
#include "cli.h"
#include "conference.h"

#ifdef AST_CLI_DEFINE

#define argc a->argc
#define argv a->argv
#define fd a->fd

#define SUCCESS CLI_SUCCESS
#define SHOWUSAGE CLI_SHOWUSAGE
#define FAILURE CLI_FAILURE

#define NEWCLI_SWITCH(cli_command,cli_usage) \
switch (cmd) { \
	case CLI_INIT: \
		e->command = cli_command; \
		e->usage = cli_usage; \
		return NULL; \
	case CLI_GENERATE: \
		if (a->pos > e->args) \
			return NULL; \
		return ast_cli_complete(a->word, choices, a->n); \
	default: \
		break; \
}

#else

#define SUCCESS RESULT_SUCCESS
#define SHOWUSAGE RESULT_SHOWUSAGE
#define FAILURE RESULT_FAILURE

#endif

//
// version 
//
static char conference_version_usage[] =
	"Usage: konference version\n"
	"       Display konference version\n"
;

#define CONFERENCE_VERSION_CHOICES { "konference", "version", NULL }
static char conference_version_summary[] = "Display konference version";

#ifndef AST_CLI_DEFINE
static struct ast_cli_entry cli_version = {
	CONFERENCE_VERSION_CHOICES,
	conference_version,
	conference_version_summary,
	conference_version_usage
} ;
int conference_version( int fd, int argc, char *argv[] ) {
#else
static char conference_version_command[] = "konference version";
char *conference_version(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
#if	ASTERISK == 14 || ASTERISK == 16
	static char *choices[] = CONFERENCE_VERSION_CHOICES;
#else
	static const char *const choices[] = CONFERENCE_VERSION_CHOICES;
#endif
	NEWCLI_SWITCH(conference_version_command,conference_version_usage)
#endif
	if ( argc < 2 )
		return SHOWUSAGE ;

	ast_cli( fd, "app_konference revision %s\n", REVISION) ;

	return SUCCESS ;
}

//
// restart conference
//
static char conference_restart_usage[] =
	"Usage: konference restart\n"
	"       Kick all users in all conferences\n"
;

#define CONFERENCE_RESTART_CHOICES { "konference", "restart", NULL }
static char conference_restart_summary[] = "Restart a conference";

#ifndef AST_CLI_DEFINE
static struct ast_cli_entry cli_restart = {
	CONFERENCE_RESTART_CHOICES,
	conference_restart,
	conference_restart_summary,
	conference_restart_usage
} ;
int conference_restart( int fd, int argc, char *argv[] ) {
#else
static char conference_restart_command[] = "konference restart";
char *conference_restart(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
#if	ASTERISK == 14 || ASTERISK == 16
	static char *choices[] = CONFERENCE_RESTART_CHOICES;
#else
	static const char *const choices[] = CONFERENCE_RESTART_CHOICES;
#endif
	NEWCLI_SWITCH(conference_restart_command,conference_restart_usage)
#endif
	if ( argc < 2 )
		return SHOWUSAGE ;

	kick_all();
	return SUCCESS ;
}
//
// stats functions
//
static char conference_show_stats_usage[] =
	"Usage: konference show stats\n"
	"       Display stats for active conferences\n"
;

#define CONFERENCE_SHOW_STATS_CHOICES { "konference", "show", "stats", NULL }
static char conference_show_stats_summary[] = "Show conference stats";

#ifndef AST_CLI_DEFINE
static struct ast_cli_entry cli_show_stats = {
	CONFERENCE_SHOW_STATS_CHOICES,
	conference_show_stats,
	conference_show_stats_summary,
	conference_show_stats_usage
} ;
int conference_show_stats( int fd, int argc, char *argv[] ) {
#else
static char conference_show_stats_command[] = "konference show stats";
char *conference_show_stats(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
#if	ASTERISK == 14 || ASTERISK == 16
	static char *choices[] = CONFERENCE_SHOW_STATS_CHOICES;
#else
	static const char *const choices[] = CONFERENCE_SHOW_STATS_CHOICES;
#endif
	NEWCLI_SWITCH(conference_show_stats_command,conference_show_stats_usage)
#endif
	if ( argc < 3 )
		return SHOWUSAGE ;

	// get count of active conferences
	int count = get_conference_count() ;

	ast_cli( fd, "\n\nCONFERENCE STATS, ACTIVE( %d )\n\n", count ) ;

	// if zero, go no further
	if ( count <= 0 )
		return SUCCESS ;

	//
	// get the conference stats
	//

	// array of stats structs
	ast_conference_stats stats[ count ] ;

	// get stats structs
	count = get_conference_stats( stats, count ) ;

	// make sure we were able to fetch some
	if ( count <= 0 )
	{
		ast_cli( fd, "!!! error fetching conference stats, available => %d !!!\n", count ) ;
		return SUCCESS ;
	}

	//
	// output the conference stats
	//

	// output header
	ast_cli( fd, "%-20.20s  %-40.40s\n", "Name", "Stats") ;
	ast_cli( fd, "%-20.20s  %-40.40s\n", "----", "-----") ;

	ast_conference_stats* s = NULL ;

	int i;

	for ( i = 0 ; i < count ; ++i )
	{
		s = &(stats[i]) ;

		// output this conferences stats
		ast_cli( fd, "%-20.20s\n", (char*)( &(s->name) )) ;
	}

	ast_cli( fd, "\n" ) ;

	//
	// drill down to specific stats
	//

	if ( argc == 4 )
	{
		// show stats for a particular conference
	}

	return SUCCESS ;
}

//
// list conferences
//
static char conference_list_usage[] =
	"Usage: konference list {<conference_name>}\n"
	"       List members of a conference\n"
;

#define CONFERENCE_LIST_CHOICES { "konference", "list", NULL }
static char conference_list_summary[] = "List members of a conference";

#ifndef AST_CLI_DEFINE
static struct ast_cli_entry cli_list = {
	CONFERENCE_LIST_CHOICES,
	conference_list,
	conference_list_summary,
	conference_list_usage
} ;
int conference_list( int fd, int argc, char *argv[] ) {
#else
static char conference_list_command[] = "konference list";
char *conference_list(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
#if	ASTERISK == 14 || ASTERISK == 16
	static char *choices[] = CONFERENCE_LIST_CHOICES;
#else
	static const char *const choices[] = CONFERENCE_LIST_CHOICES;
#endif
	NEWCLI_SWITCH(conference_list_command,conference_list_usage)
#endif
	if ( argc < 2 )
		return SHOWUSAGE ;

	if (argc >= 3)
	{
		int index;
		for (index = 2; index < argc; index++)
		{
			// get the conference name
			const char* name = argv[index] ;
			show_conference_list( fd, name );
		}
	}
	else
	{
		show_conference_stats(fd);
	}
	return SUCCESS ;
}

//
// kick member <member id>
//
static char conference_kick_usage[] =
	"Usage: konference kick <conference> <member id>\n"
	"       Kick member <member id> from conference <conference>\n"
;

#define CONFERENCE_KICK_CHOICES { "konference", "kick", NULL }
static char conference_kick_summary[] = "Kick member from a conference";

#ifndef AST_CLI_DEFINE
static struct ast_cli_entry cli_kick = {
	CONFERENCE_KICK_CHOICES,
	conference_kick,
	conference_kick_summary,
	conference_kick_usage
} ;
int conference_kick( int fd, int argc, char *argv[] ) {
#else
static char conference_kick_command[] = "konference kick";
char *conference_kick(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
#if	ASTERISK == 14 || ASTERISK == 16
	static char *choices[] = CONFERENCE_KICK_CHOICES;
#else
	static const char *const choices[] = CONFERENCE_KICK_CHOICES;
#endif
	NEWCLI_SWITCH(conference_kick_command,conference_kick_usage)
#endif
	if ( argc < 4 )
		return SHOWUSAGE ;

	// get the conference name
	const char* name = argv[2] ;

	int member_id;
	sscanf(argv[3], "%d", &member_id);

	int res = kick_member( name, member_id );

	if (res) ast_cli( fd, "User #: %d kicked\n", member_id) ;

	return SUCCESS ;
}

//
// kick member <channel>
//
static char conference_kickchannel_usage[] =
	"Usage: konference kickchannel <channel>\n"
	"       Kick channel from conference\n"
;

#define CONFERENCE_KICKCHANNEL_CHOICES { "konference", "kickchannel", NULL }
static char conference_kickchannel_summary[] = "Kick channel from conference";

#ifndef AST_CLI_DEFINE
static struct ast_cli_entry cli_kickchannel = {
	CONFERENCE_KICKCHANNEL_CHOICES,
	conference_kickchannel,
	conference_kickchannel_summary,
	conference_kickchannel_usage
} ;
int conference_kickchannel( int fd, int argc, char *argv[] ) {
#else
static char conference_kickchannel_command[] = "konference kickchannel";
char *conference_kickchannel(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
#if	ASTERISK == 14 || ASTERISK == 16
	static char *choices[] = CONFERENCE_KICKCHANNEL_CHOICES;
#else
	static const char *const choices[] = CONFERENCE_KICKCHANNEL_CHOICES;
#endif
	NEWCLI_SWITCH(conference_kickchannel_command,conference_kickchannel_usage)
#endif
	if ( argc < 3 )
		return SHOWUSAGE ;

	const char *channel = argv[2];

	struct ast_conf_member *member = find_member(channel, 1);
	if(!member) {
	    ast_cli(fd, "Member %s not found\n", channel);
	    return FAILURE;
	}

	member->kick_flag = 1;

	if ( !--member->use_count && member->delete_flag )
		ast_cond_signal ( &member->delete_var ) ;
	ast_mutex_unlock( &member->lock ) ;

	return SUCCESS ;
}

//
// mute member <member id>
//
static char conference_mute_usage[] =
	"Usage: konference mute <conference_name> <member id>\n"
	"       Mute member in a conference\n"
;

#define CONFERENCE_MUTE_CHOICES { "konference", "mute", NULL }
static char conference_mute_summary[] = "Mute member in a conference";

#ifndef AST_CLI_DEFINE
static struct ast_cli_entry cli_mute = {
	CONFERENCE_MUTE_CHOICES,
	conference_mute,
	conference_mute_summary,
	conference_mute_usage
} ;
int conference_mute( int fd, int argc, char *argv[] ) {
#else
static char conference_mute_command[] = "konference mute";
char *conference_mute(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
#if	ASTERISK == 14 || ASTERISK == 16
	static char *choices[] = CONFERENCE_MUTE_CHOICES;
#else
	static const char *const choices[] = CONFERENCE_MUTE_CHOICES;
#endif
	NEWCLI_SWITCH(conference_mute_command,conference_mute_usage)
#endif
	if ( argc < 4 )
		return SHOWUSAGE ;

	// get the conference name
	const char* name = argv[2] ;

	int member_id;
	sscanf(argv[3], "%d", &member_id);

	int res = mute_member( name, member_id );

	if (res) ast_cli( fd, "User #: %d muted\n", member_id) ;

	return SUCCESS ;
}

//
// mute conference
//
static char conference_muteconference_usage[] =
	"Usage: konference muteconference <conference_name>\n"
	"       Mute all members in a conference\n"
;

#define CONFERENCE_MUTECONFERENCE_CHOICES { "konference", "muteconference", NULL }
static char conference_muteconference_summary[] = "Mute all members in a conference";

#ifndef AST_CLI_DEFINE
static struct ast_cli_entry cli_muteconference = {
	CONFERENCE_MUTECONFERENCE_CHOICES,
	conference_muteconference,
	conference_muteconference_summary,
	conference_muteconference_usage
} ;
int conference_muteconference( int fd, int argc, char *argv[] ) {
#else
static char conference_muteconference_command[] = "konference muteconference";
char *conference_muteconference(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
#if	ASTERISK == 14 || ASTERISK == 16
	static char *choices[] = CONFERENCE_MUTECONFERENCE_CHOICES;
#else
	static const char *const choices[] = CONFERENCE_MUTECONFERENCE_CHOICES;
#endif
	NEWCLI_SWITCH(conference_muteconference_command,conference_muteconference_usage)
#endif
	if ( argc < 3 )
		return SHOWUSAGE ;

	// get the conference name
	const char* name = argv[2] ;

	int res = mute_conference ( name );

	if (res) ast_cli( fd, "Conference: %s muted\n", name) ;

	return SUCCESS ;
}

//
// mute member <channel>
//
static char conference_mutechannel_usage[] =
	"Usage: konference mutechannel <channel>\n"
	"       Mute channel in a conference\n"
;

#define CONFERENCE_MUTECHANNEL_CHOICES { "konference", "mutechannel", NULL }
static char conference_mutechannel_summary[] = "Mute channel in a conference";

#ifndef AST_CLI_DEFINE
static struct ast_cli_entry cli_mutechannel = {
	CONFERENCE_MUTECHANNEL_CHOICES,
	conference_mutechannel,
	conference_mutechannel_summary,
	conference_mutechannel_usage
} ;
int conference_mutechannel( int fd, int argc, char *argv[] ) {
#else
static char conference_mutechannel_command[] = "konference mutechannel";
char *conference_mutechannel(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
#if	ASTERISK == 14 || ASTERISK == 16
	static char *choices[] = CONFERENCE_MUTECHANNEL_CHOICES;
#else
	static const char *const choices[] = CONFERENCE_MUTECHANNEL_CHOICES;
#endif
	NEWCLI_SWITCH(conference_mutechannel_command,conference_mutechannel_usage)
#endif
	if ( argc < 3 )
		return SHOWUSAGE ;

	const char *channel = argv[2];

	struct ast_conf_member *member = find_member(channel, 1);
	if(!member) {
	    ast_cli(fd, "Member %s not found\n", channel);
	    return FAILURE;
	}

	member->mute_audio = 1;

	if ( !--member->use_count && member->delete_flag )
		ast_cond_signal ( &member->delete_var ) ;
	ast_mutex_unlock( &member->lock ) ;

	manager_event(
		EVENT_FLAG_CONF,
		"ConferenceMemberMute",
		"Channel: %s\r\n",
		channel
	) ;

	ast_cli( fd, "Channel #: %s muted\n", argv[2]) ;

	return SUCCESS ;
}
//
// unmute member <member id>
//
static char conference_unmute_usage[] =
	"Usage: konference unmute <conference_name> <member id>\n"
	"       Unmute member in a conference\n"
;

#define CONFERENCE_UNMUTE_CHOICES { "konference", "unmute", NULL }
static char conference_unmute_summary[] = "Unmute member in a conference";

#ifndef AST_CLI_DEFINE
static struct ast_cli_entry cli_unmute = {
	CONFERENCE_UNMUTE_CHOICES,
	conference_unmute,
	conference_unmute_summary,
	conference_unmute_usage
} ;
int conference_unmute( int fd, int argc, char *argv[] ) {
#else
static char conference_unmute_command[] = "konference unmute";
char *conference_unmute(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
#if	ASTERISK == 14 || ASTERISK == 16
	static char *choices[] = CONFERENCE_UNMUTE_CHOICES;
#else
	static const char *const choices[] = CONFERENCE_UNMUTE_CHOICES;
#endif
	NEWCLI_SWITCH(conference_unmute_command,conference_unmute_usage)
#endif
	if ( argc < 4 )
		return SHOWUSAGE ;

	// get the conference name
	const char* name = argv[2] ;

	int member_id;
	sscanf(argv[3], "%d", &member_id);

	int res = unmute_member( name, member_id );

	if (res) ast_cli( fd, "User #: %d unmuted\n", member_id) ;

	return SUCCESS ;
}

//
// unmute conference
//
static char conference_unmuteconference_usage[] =
	"Usage: konference unmuteconference <conference_name>\n"
	"       Unmute all members in a conference\n"
;

#define CONFERENCE_UNMUTECONFERENCE_CHOICES { "konference", "unmuteconference", NULL }
static char conference_unmuteconference_summary[] = "Unmute all members in a conference";

#ifndef AST_CLI_DEFINE
static struct ast_cli_entry cli_unmuteconference = {
	CONFERENCE_UNMUTECONFERENCE_CHOICES,
	conference_unmuteconference,
	conference_unmuteconference_summary,
	conference_unmuteconference_usage
} ;
int conference_unmuteconference( int fd, int argc, char *argv[] ) {
#else
static char conference_unmuteconference_command[] = "konference unmuteconference";
char *conference_unmuteconference(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
#if	ASTERISK == 14 || ASTERISK == 16
	static char *choices[] = CONFERENCE_UNMUTECONFERENCE_CHOICES;
#else
	static const char *const choices[] = CONFERENCE_UNMUTECONFERENCE_CHOICES;
#endif
	NEWCLI_SWITCH(conference_unmuteconference_command,conference_unmuteconference_usage)
#endif
	if ( argc < 3 )
		return SHOWUSAGE ;

	// get the conference name
	const char* name = argv[2] ;

	int res = unmute_conference ( name );

	if (res) ast_cli( fd, "Conference: %s unmuted\n", name) ;

	return SUCCESS ;
}

//
// unmute member <channel>
//
static char conference_unmutechannel_usage[] =
	"Usage: konference unmutechannel <channel>\n"
	"       Unmute channel in a conference\n"
;

#define CONFERENCE_UNMUTECHANNEL_CHOICES { "konference", "unmutechannel", NULL }
static char conference_unmutechannel_summary[] = "Unmute channel in a conference";

#ifndef AST_CLI_DEFINE
static struct ast_cli_entry cli_unmutechannel = {
	CONFERENCE_UNMUTECHANNEL_CHOICES,
	conference_unmutechannel,
	conference_unmutechannel_summary,
	conference_unmutechannel_usage
} ;
int conference_unmutechannel( int fd, int argc, char *argv[] ) {
#else
static char conference_unmutechannel_command[] = "konference unmutechannel";
char *conference_unmutechannel(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
#if	ASTERISK == 14 || ASTERISK == 16
	static char *choices[] = CONFERENCE_UNMUTECHANNEL_CHOICES;
#else
	static const char *const choices[] = CONFERENCE_UNMUTECHANNEL_CHOICES;
#endif
	NEWCLI_SWITCH(conference_unmutechannel_command,conference_unmutechannel_usage)
#endif
	if ( argc < 3 )
		return SHOWUSAGE ;

	const char *channel = argv[2];

	struct ast_conf_member *member = find_member(channel, 1);
	if(!member) {
	    ast_cli(fd, "Member %s not found\n", channel);
	    return FAILURE;
	}

	member->mute_audio = 0;

	if ( !--member->use_count && member->delete_flag )
		ast_cond_signal ( &member->delete_var ) ;
	ast_mutex_unlock( &member->lock ) ;

	manager_event(
		EVENT_FLAG_CONF,
		"ConferenceMemberUnmute",
		"Channel: %s\r\n",
		channel
	) ;

	ast_cli( fd, "Channel #: %s unmuted\n", argv[2]) ;

	return SUCCESS ;
}

//
// play sound
//
static char conference_play_sound_usage[] =
	"Usage: konference play sound <channel> (<sound-file>)+ [mute|tone]\n"
	"       Play sound(s) (<sound-file>)+ to conference member <channel>\n"
	"       If mute is specified, all other audio is muted while the sound is played back\n"
	"       If tone is specified, the sound is discarded if another sound is queued\n"
;

#define CONFERENCE_PLAY_SOUND_CHOICES { "konference", "play", "sound", NULL }
static char conference_play_sound_summary[] = "Play a sound to a conference member";

#ifndef AST_CLI_DEFINE
static struct ast_cli_entry cli_play_sound = {
	CONFERENCE_PLAY_SOUND_CHOICES,
	conference_play_sound,
	conference_play_sound_summary,
	conference_play_sound_usage
} ;
int conference_play_sound( int fd, int argc, char *argv[] ) {
#else
static char conference_play_sound_command[] = "konference play sound";
char *conference_play_sound(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
#if	ASTERISK == 14 || ASTERISK == 16
	static char *choices[] = CONFERENCE_PLAY_SOUND_CHOICES;
#else
	static const char *const choices[] = CONFERENCE_PLAY_SOUND_CHOICES;
#endif
	NEWCLI_SWITCH(conference_play_sound_command,conference_play_sound_usage)
#endif
	if ( argc < 5 )
		return SHOWUSAGE ;

	const char *channel = argv[3];
#if	ASTERISK == 14 || ASTERISK == 16
	char **file = &argv[4];
#else
	const char * const *file = &argv[4];
#endif

	int mute = (argc > 5 && !strcmp(argv[argc-1], "mute")?1:0);
	int tone = (argc > 5 && !strcmp(argv[argc-1], "tone")?1:0);

	int res = play_sound_channel(fd, channel, file, mute, tone, (!mute && !tone) ? argc - 4 : argc - 5);

	if ( !res )
	{
		ast_cli(fd, "Sound playback failed failed\n");
		return FAILURE;
	}
	return SUCCESS ;
}

//
// stop sounds
//
static char conference_stop_sounds_usage[] =
	"Usage: konference stop sounds <channel>\n"
	"       Stop sounds for conference member <channel>\n"
;

#define CONFERENCE_STOP_SOUNDS_CHOICES { "konference", "stop", "sounds", NULL }
static char conference_stop_sounds_summary[] = "Stop sounds for a conference member";

#ifndef AST_CLI_DEFINE
static struct ast_cli_entry cli_stop_sounds = {
	CONFERENCE_STOP_SOUNDS_CHOICES,
	conference_stop_sounds,
	conference_stop_sounds_summary,
	conference_stop_sounds_usage
} ;
int conference_stop_sounds( int fd, int argc, char *argv[] ) {
#else
static char conference_stop_sounds_command[] = "konference stop sounds";
char *conference_stop_sounds(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
#if	ASTERISK == 14 || ASTERISK == 16
	static char *choices[] = CONFERENCE_STOP_SOUNDS_CHOICES;
#else
	static const char *const choices[] = CONFERENCE_STOP_SOUNDS_CHOICES;
#endif
	NEWCLI_SWITCH(conference_stop_sounds_command,conference_stop_sounds_usage)
#endif
	if ( argc < 4 )
		return SHOWUSAGE ;

	const char *channel = argv[3];

	int res = stop_sound_channel(fd, channel);

	if ( !res )
	{
		ast_cli(fd, "Sound stop failed failed\n");
		return FAILURE;
	}
	return SUCCESS ;
}

//
// start moh
//
static char conference_start_moh_usage[] =
	"Usage: konference start moh <channel>\n"
	"       Start moh for conference member <channel>\n"
;

#define CONFERENCE_START_MOH_CHOICES { "konference", "start", "moh", NULL }
static char conference_start_moh_summary[] = "Start moh for a conference member";

#ifndef AST_CLI_DEFINE
static struct ast_cli_entry cli_start_moh = {
	CONFERENCE_START_MOH_CHOICES,
	conference_start_moh,
	conference_start_moh_summary,
	conference_start_moh_usage
} ;
int conference_start_moh( int fd, int argc, char *argv[] ) {
#else
static char conference_start_moh_command[] = "konference start moh";
char *conference_start_moh(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
#if	ASTERISK == 14 || ASTERISK == 16
	static char *choices[] = CONFERENCE_START_MOH_CHOICES;
#else
	static const char *const choices[] = CONFERENCE_START_MOH_CHOICES;
#endif
	NEWCLI_SWITCH(conference_start_moh_command,conference_start_moh_usage)
#endif
	if ( argc < 4 )
		return SHOWUSAGE ;

	const char *channel = argv[3];

	int res = start_moh_channel(fd, channel);

	if ( !res )
	{
		ast_cli(fd, "Start moh failed\n");
		return FAILURE;
	}
	return SUCCESS ;
}

//
// stop moh
//
static char conference_stop_moh_usage[] =
	"Usage: konference stop moh <channel>\n"
	"       Stop moh for conference member <channel>\n"
;

#define CONFERENCE_STOP_MOH_CHOICES { "konference", "stop", "moh", NULL }
static char conference_stop_moh_summary[] = "Stop moh for a conference member";

#ifndef AST_CLI_DEFINE
static struct ast_cli_entry cli_stop_moh = {
	CONFERENCE_STOP_MOH_CHOICES,
	conference_stop_moh,
	conference_stop_moh_summary,
	conference_stop_moh_usage
} ;
int conference_stop_moh( int fd, int argc, char *argv[] ) {
#else
static char conference_stop_moh_command[] = "konference stop moh";
char *conference_stop_moh(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
#if	ASTERISK == 14 || ASTERISK == 16
	static char *choices[] = CONFERENCE_STOP_MOH_CHOICES;
#else
	static const char *const choices[] = CONFERENCE_STOP_MOH_CHOICES;
#endif
	NEWCLI_SWITCH(conference_stop_moh_command,conference_stop_moh_usage)
#endif
	if ( argc < 4 )
		return SHOWUSAGE ;

	const char *channel = argv[3];

	int res = stop_moh_channel(fd, channel);

	if ( !res )
	{
		ast_cli(fd, "Sound moh failed\n");
		return FAILURE;
	}
	return SUCCESS ;
}


//
// adjust talk volume
//
static char conference_talkvolume_usage[] =
	"Usage: konference talkvolume <channel> ( up | down )\n"
	"       Adjust talk volume for conference member <channel>\n"
;

#define CONFERENCE_TALKVOLUME_CHOICES { "konference", "talkvolume", NULL }
static char conference_talkvolume_summary[] = "Adjust talk volume for a conference member";

#ifndef AST_CLI_DEFINE
static struct ast_cli_entry cli_talkvolume = {
	CONFERENCE_TALKVOLUME_CHOICES,
	conference_talkvolume,
	conference_talkvolume_summary,
	conference_talkvolume_usage
} ;
int conference_talkvolume( int fd, int argc, char *argv[] ) {
#else
static char conference_talkvolume_command[] = "konference talkvolume";
char *conference_talkvolume(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
#if	ASTERISK == 14 || ASTERISK == 16
	static char *choices[] = CONFERENCE_TALKVOLUME_CHOICES;
#else
	static const char *const choices[] = CONFERENCE_TALKVOLUME_CHOICES;
#endif
	NEWCLI_SWITCH(conference_talkvolume_command,conference_talkvolume_usage)
#endif
	if ( argc < 4 )
		return SHOWUSAGE ;

	const char *channel = argv[2];

	int up;
	if ( !strncasecmp( argv[3], "up", 2 ) )
		up = 1 ;
	else if ( !strncasecmp( argv[3], "down", 4 ) )
		up = 0 ;
	else
		return SHOWUSAGE ;

	int res = talk_volume_channel(fd, channel, up);

	if ( !res )
	{
		ast_cli(fd, "Channel %s talk volume adjust failed\n", channel);
		return FAILURE;
	}
	return SUCCESS ;
}

//
// adjust listen volume
//
static char conference_listenvolume_usage[] =
	"Usage: konference listenvolume <channel> ( up | down )\n"
	"       Adjust listen volume for conference member <channel>\n"
;

#define CONFERENCE_LISTENVOLUME_CHOICES { "konference", "listenvolume", NULL }
static char conference_listenvolume_summary[] = "Adjust listen volume for a conference member";

#ifndef AST_CLI_DEFINE
static struct ast_cli_entry cli_listenvolume = {
	CONFERENCE_LISTENVOLUME_CHOICES,
	conference_listenvolume,
	conference_listenvolume_summary,
	conference_listenvolume_usage
} ;
int conference_listenvolume( int fd, int argc, char *argv[] ) {
#else
static char conference_listenvolume_command[] = "konference listenvolume";
char *conference_listenvolume(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
#if	ASTERISK == 14 || ASTERISK == 16
	static char *choices[] = CONFERENCE_LISTENVOLUME_CHOICES;
#else
	static const char *const choices[] = CONFERENCE_LISTENVOLUME_CHOICES;
#endif
	NEWCLI_SWITCH(conference_listenvolume_command,conference_listenvolume_usage)
#endif
	if ( argc < 4 )
		return SHOWUSAGE ;

	const char *channel = argv[2];

	int up;
	if ( !strncasecmp( argv[3], "up", 2 ) )
		up = 1 ;
	else if ( !strncasecmp( argv[3], "down", 4 ) )
		up = 0 ;
	else
		return SHOWUSAGE ;

	int res = listen_volume_channel(fd, channel, up);

	if ( !res )
	{
		ast_cli(fd, "Channel %s listen volume adjust failed\n", channel);
		return FAILURE;
	}
	return SUCCESS ;
}

//
// adjust conference volume
//
static char conference_volume_usage[] =
	"Usage: konference volume <conference name> (up|down)\n"
	"       Raise or lower the conference volume\n"
;

#define CONFERENCE_VOLUME_CHOICES { "konference", "volume", NULL }
static char conference_volume_summary[] = "Adjusts conference volume";

#ifndef AST_CLI_DEFINE
static struct ast_cli_entry cli_volume = {
	CONFERENCE_VOLUME_CHOICES,
	conference_volume,
	conference_volume_summary,
	conference_volume_usage
} ;
int conference_volume( int fd, int argc, char *argv[] ) {
#else
static char conference_volume_command[] = "konference volume";
char *conference_volume(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
#if	ASTERISK == 14 || ASTERISK == 16
	static char *choices[] = CONFERENCE_VOLUME_CHOICES;
#else
	static const char *const choices[] = CONFERENCE_VOLUME_CHOICES;
#endif
	NEWCLI_SWITCH(conference_volume_command,conference_volume_usage)
#endif
	if ( argc < 4 )
		return SHOWUSAGE ;

	// conference name
	const char* conference = argv[2] ;

	int up;
	if ( !strncasecmp( argv[3], "up", 2 ) )
		up = 1 ;
	else if ( !strncasecmp( argv[3], "down", 4 ) )
		up = 0 ;
	else
		return SHOWUSAGE ;

	int res =  volume(fd, conference, up );
	
	if ( !res )
	{
		ast_cli( fd, "Conference %s volume adjust failed\n", conference) ;
		return SHOWUSAGE ;
	}

	return SUCCESS ;
}

//
// end conference
//
static char conference_end_usage[] =
	"Usage: konference end <conference name>\n"
	"       Ends a conference\n"
;

#define CONFERENCE_END_CHOICES { "konference", "end", NULL }
static char conference_end_summary[] = "Stops a conference";

#ifndef AST_CLI_DEFINE
static struct ast_cli_entry cli_end = {
	CONFERENCE_END_CHOICES,
	conference_end,
	conference_end_summary,
	conference_end_usage
} ;
int conference_end( int fd, int argc, char *argv[] ) {
#else
static char conference_end_command[] = "konference end";
char *conference_end(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
#if	ASTERISK == 14 || ASTERISK == 16
	static char *choices[] = CONFERENCE_END_CHOICES;
#else
	static const char *const choices[] = CONFERENCE_END_CHOICES;
#endif
	NEWCLI_SWITCH(conference_end_command,conference_end_usage)
#endif
	if ( argc < 3 )
		return SHOWUSAGE ;

	// conference name
	const char* name = argv[2] ;

	int hangup = (argc == 4 && !strcmp(argv[3], "nohangup") ? 0 : 1) ;

	// get the conference
	if ( end_conference( name, hangup ) )
	{
		ast_cli( fd, "unable to end the conference, name => %s\n", name ) ;
		return SHOWUSAGE ;
	}

	return SUCCESS ;
}

//
// cli initialization function
//

#ifdef AST_CLI_DEFINE
static struct ast_cli_entry app_konference_commands[] = {
	AST_CLI_DEFINE(conference_version, conference_version_summary),
	AST_CLI_DEFINE(conference_restart, conference_restart_summary),
	AST_CLI_DEFINE(conference_show_stats, conference_show_stats_summary),
	AST_CLI_DEFINE(conference_list, conference_list_summary),
	AST_CLI_DEFINE(conference_kick, conference_kick_summary),
	AST_CLI_DEFINE(conference_kickchannel, conference_kickchannel_summary),
	AST_CLI_DEFINE(conference_mute, conference_mute_summary),
	AST_CLI_DEFINE(conference_muteconference, conference_muteconference_summary),
	AST_CLI_DEFINE(conference_mutechannel, conference_mutechannel_summary),
	AST_CLI_DEFINE(conference_unmute, conference_unmute_summary),
	AST_CLI_DEFINE(conference_unmuteconference, conference_unmuteconference_summary),
	AST_CLI_DEFINE(conference_unmutechannel, conference_unmutechannel_summary),
	AST_CLI_DEFINE(conference_play_sound, conference_play_sound_summary),
	AST_CLI_DEFINE(conference_stop_sounds, conference_stop_sounds_summary),
	AST_CLI_DEFINE(conference_stop_moh, conference_stop_moh_summary),
	AST_CLI_DEFINE(conference_start_moh, conference_start_moh_summary),
	AST_CLI_DEFINE(conference_talkvolume, conference_talkvolume_summary),
	AST_CLI_DEFINE(conference_listenvolume, conference_listenvolume_summary),
	AST_CLI_DEFINE(conference_volume, conference_volume_summary),
	AST_CLI_DEFINE(conference_end, conference_end_summary),
};
#endif

void register_conference_cli( void )
{
#ifdef AST_CLI_DEFINE
	ast_cli_register_multiple(app_konference_commands, sizeof(app_konference_commands)/sizeof(struct ast_cli_entry));
#else
	ast_cli_register( &cli_version );
	ast_cli_register( &cli_restart );
	ast_cli_register( &cli_show_stats ) ;
	ast_cli_register( &cli_list );
	ast_cli_register( &cli_kick );
	ast_cli_register( &cli_kickchannel );
	ast_cli_register( &cli_mute );
	ast_cli_register( &cli_muteconference );
	ast_cli_register( &cli_mutechannel );
	ast_cli_register( &cli_unmute );
	ast_cli_register( &cli_unmuteconference );
	ast_cli_register( &cli_unmutechannel );
	ast_cli_register( &cli_play_sound ) ;
	ast_cli_register( &cli_stop_sounds ) ;
	ast_cli_register( &cli_stop_moh ) ;
	ast_cli_register( &cli_start_moh ) ;
	ast_cli_register( &cli_talkvolume ) ;
	ast_cli_register( &cli_listenvolume ) ;
	ast_cli_register( &cli_volume );
	ast_cli_register( &cli_end );
#endif
}

void unregister_conference_cli( void )
{
#ifdef AST_CLI_DEFINE
	ast_cli_unregister_multiple(app_konference_commands, sizeof(app_konference_commands)/sizeof(struct ast_cli_entry));
#else
	ast_cli_unregister( &cli_version );
	ast_cli_unregister( &cli_restart );
	ast_cli_unregister( &cli_show_stats ) ;
	ast_cli_unregister( &cli_list );
	ast_cli_unregister( &cli_kick );
	ast_cli_unregister( &cli_kickchannel );
	ast_cli_unregister( &cli_mute );
	ast_cli_unregister( &cli_muteconference );
	ast_cli_unregister( &cli_mutechannel );
	ast_cli_unregister( &cli_unmute );
	ast_cli_unregister( &cli_unmuteconference );
	ast_cli_unregister( &cli_unmutechannel );
	ast_cli_unregister( &cli_play_sound ) ;
	ast_cli_unregister( &cli_stop_sounds ) ;
	ast_cli_unregister( &cli_stop_moh ) ;
	ast_cli_unregister( &cli_start_moh );
	ast_cli_unregister( &cli_talkvolume ) ;
	ast_cli_unregister( &cli_listenvolume ) ;
	ast_cli_unregister( &cli_volume );
	ast_cli_unregister( &cli_end );
#endif
}
