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

#ifndef _KONFERENCE_CONFERENCE_H
#define _KONFERENCE_CONFERENCE_H

//
// includes
//

#include "app_conference.h"
#include "member.h"

//
// defines
//

#define CONF_NAME_LEN 80

//
// struct declarations
//

struct ast_conf_listheaders
{
	// conference listheaders lock
	ast_rwlock_t lock;
	// speaker list
	ast_conf_listentry speakerlistheader;
	// member list
	ast_conf_listentry memberlistheader;
};

struct ast_conference
{
#ifdef	CACHE_CONTROL_BLOCKS
	AST_LIST_ENTRY(ast_conference) free_list;
#endif
	// name
	char name[CONF_NAME_LEN + 1];
	
	// start time
	struct timeval time_entered;

	// moderator count
	unsigned short moderators;

	// conference listener frame
	conf_frame *listener_frame;

	// conference volume
	int volume;

	int membercount;
        int id_count;

	// conference listheaders
	ast_conf_listheaders *listheaders;

	// conference list pointer
	ast_conference* next;

	// pointer to conference's bucket list head
	struct conference_bucket *bucket;
	// list entry for conference's bucket list
	AST_LIST_ENTRY(ast_conference) hash_entry;

	// pointer to translation paths
	struct ast_trans_pvt* from_slinear_paths[AC_SUPPORTED_FORMATS];

	// keep track of current delivery time
	struct timeval delivery_time;

	// listener mix buffer
#ifdef	VECTORS
	char listenerBuffer[AST_CONF_BUFFER_SIZE] __attribute__ ((aligned(16)));
#else
	char listenerBuffer[AST_CONF_BUFFER_SIZE];
#endif
	// listener mix frames
	struct ast_frame *mixAstFrame;
	conf_frame *mixConfFrame;
#ifdef	VIDEO
	// video mode
	ac_video_mode video_mode;
	// SFU list
	AST_RWDLLIST_HEAD(sfu_list, ast_conf_member) sfu_list;
	// video source
	ast_conf_member *video_source;
#endif
};

//
// function declarations
//

int hash(const char *channel_name);

ast_conference* join_conference(ast_conf_member* member, char* conf_name, int max_users);

// Find member, locked if found.
ast_conf_member *find_member(const char *chan);

void queue_frame_for_listener(ast_conference* conf, ast_conf_member* member);
void queue_frame_for_speaker(ast_conference* conf, ast_conf_member* member);
void queue_silent_frame(ast_conference* conf, ast_conf_member* member);

void remove_member(ast_conf_member* member, ast_conference* conf, char* conf_name);

// called by app_conference.c:load_module()
int init_conference(void);
// called by app_conference.c:unload_module()
void dealloc_conference(void);

// cli functions
void end_conference(const char *name);

void list_members(int fd, const char* name);
void list_conferences(int fd);
void list_all(int fd);

void kick_all(void);

void mute_conference(const char* confname);
void unmute_conference(const char* confname);

#if	ASTERISK_SRC_VERSION == 104 || ASTERISK_SRC_VERSION == 106
void play_sound_channel(const char *channel, char **file, int mute, int tone, int evnt, int n);
#else
void play_sound_channel(const char *channel, const char * const *file, int mute, int tone, int evnt, int n);
#endif

void stop_sound_channel(const char *channel);

void start_moh_channel(const char *channel);
void stop_moh_channel(const char *channel);

void adjust_talk_volume_channel(const char *channel, int up);
void adjust_listen_volume_channel(const char *channel, int up);

void adjust_volume_conference(const char *conference, int up);

#endif
