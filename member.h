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

#ifndef _KONFERENCE_MEMBER_H
#define _KONFERENCE_MEMBER_H

//
// includes
//

#include "app_conference.h"
#include "conference.h"

//
// defines
//

#define MEMBER_FLAGS_LEN 10
#define MEMBER_TYPE_LEN 20

//
// struct declarations
//

struct ast_conf_soundq
{
	char name[160];
	struct ast_filestream *stream; // the stream
	struct ast_conf_soundq *next;
};

struct ast_conf_frameq
{
	ast_mutex_t lock;
	AST_LIST_HEAD_NOLOCK(, ast_frame) frames;
	unsigned int count;
};

struct spy_list
{
	AST_LIST_HEAD_NOLOCK(, ast_conf_member) head;
	AST_LIST_ENTRY(ast_conf_member) entry;
};

struct ast_conf_member
{
#ifdef	CACHE_CONTROL_BLOCKS
	AST_LIST_ENTRY(ast_conf_member) free_list;
#endif
	ast_mutex_t lock; // member data mutex

	struct ast_channel* chan; // member's channel

	ast_conference* conf; // member's conference

	ast_cond_t delete_var; // delete cv
	char delete_flag; // delete flag
	int use_count; // use count

	conf_frame *speaker_frame; // member speaker frame

	// values passed to create_member() via *data
	char flags[MEMBER_FLAGS_LEN + 1];	// raw member-type flags
	char type[MEMBER_TYPE_LEN + 1];		// conference type
	char *spyee_channel_name; // spyee  channel name

	// block ids
	int conf_id;
#if	defined(SPEAKER_SCOREBOARD) && defined(CACHE_CONTROL_BLOCKS)
	int score_id;
#endif

	// muting options - this member will not be heard/seen
	int mute_audio;
	int muted; // should incoming audio be muted while we play?
	// volume level adjustment for this member
	int talk_volume;
	int listen_volume;

	// this member will not hear/see
	int norecv_audio;
	// is this person a moderator?
	int ismoderator;
	int kick_conferees;
	int kick_flag;

	// ready flag
	short ready_for_outgoing;

	// input frame queue
	ast_conf_frameq incomingq;

	// output frame queue
	ast_conf_frameq outgoingq;
#ifdef	VIDEO
	// video queue
	ast_conf_frameq videoq;
#endif
	// relay dtmf to manager?
	short dtmf_relay;

	// speaking flag
	short is_speaking;

	// conference listheaders
	ast_conf_listheaders *listheaders;

	// speaker list entry
	ast_conf_listentry speakerlistentry;

	// member list entry
	ast_conf_listentry memberlistentry;

	// pointer to member's bucket list head
	struct channel_bucket *bucket;
	// list entry for member's bucket list
	AST_LIST_ENTRY(ast_conf_member) hash_entry;

	// spyer pointer to spyee list or vice versa
	spy_list spy_list;
	// spyee pointer to whisper frame
	conf_frame* whisper_frame;
	// speaker frame list entry
	AST_LIST_ENTRY(ast_conf_member) speaker_frame_list_entry;

	// start time
	struct timeval time_entered;

#if	SILDET == 2
	// voice flags
	int via_telephone;
	int vad_flag;
	int denoise_flag;
	int agc_flag;

	// vad voice probability thresholds
	float vad_prob_start;
	float vad_prob_continue;

	// pointer to speex preprocessor dsp
	SpeexPreprocessState *dsp;
	// translator for dsp
	struct ast_trans_pvt *to_dsp;
        // number of "silent" frames to ignore
	int ignore_vad_result;
#endif

	// audio formats
	ac_formats write_format;
	ac_formats read_format;

	// member frame translators
	struct ast_trans_pvt* to_slinear;
	struct ast_trans_pvt* from_slinear;

	// For playing sounds
	ast_conf_soundq *soundq;

	// send event when sound ends
	int sound_event;

	// speaker mix buffer
	char *speakerBuffer;

	// speaker mix frames
	struct ast_frame *mixAstFrame;
	conf_frame *mixConfFrame;
#ifdef	VIDEO
	//  video mode
	ac_video_mode video_mode;

	// SFU media routing vectors
	struct {
		/* index mapping where channel media is to be routed */
		struct ast_vector_int to_conference;
		/* index mapping where conference media is to be routed */
		struct ast_vector_int to_channel;
	} stream_map;

	// SFU topology
	struct ast_stream_topology *topology;

	// SFU list entry
	AST_RWDLLIST_ENTRY(ast_conf_member) sfu_entry;
#endif
};

//
// function declarations
//

#if	ASTERISK_SRC_VERSION == 104
int member_exec(struct ast_channel* chan, void* data);
#else
int member_exec(struct ast_channel* chan, const char* data);
#endif

ast_conf_member* create_member(struct ast_channel* chan, const char* data, char* conf_name, int* max_users);
void delete_member(ast_conf_member* member);

// incoming queue
void queue_incoming_frame(ast_conf_member* member, struct ast_frame* fr);
conf_frame* get_incoming_frame(ast_conf_member* member);

// outgoing queue
void queue_outgoing_frame(ast_conf_member* member, struct ast_frame* fr, struct timeval delivery);
struct ast_frame* get_outgoing_frame(ast_conf_member* member);

#ifdef	VIDEO
// video queue
void queue_video_frame(ast_conf_member* member, struct ast_frame* fr, int stream_num);
struct ast_frame* get_video_frame(ast_conf_member* member);
#endif

void member_process_spoken_frames(ast_conference* conf,
				  ast_conf_member *member,
				  conf_frame **spoken_frames,
				 int *listener_count,
				 int *speaker_count);

void member_process_outgoing_frames(ast_conference* conf,
				    ast_conf_member *member);
#endif
