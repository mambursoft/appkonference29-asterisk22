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
#include "conf_frame.h"
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
	char name[256];
	struct ast_filestream *stream; // the stream
	int stopped;
	struct ast_conf_soundq *next;
};

struct ast_conf_member
{
	ast_mutex_t lock ; // member data mutex

	struct ast_channel* chan ; // member's channel

	struct ast_conference* conf ; // member's conference

	ast_cond_t delete_var ; // delete cv
	char delete_flag ; // delete flag
	int use_count ; // use count

	conf_frame *speaker_frame ; // member speaker frame

	// values passed to create_member () via *data
	int priority ;	// highest priority gets the channel
	char flags[MEMBER_FLAGS_LEN + 1] ;	// raw member-type flags
	char type[MEMBER_TYPE_LEN + 1] ;		// conference type
	char *spyee_channel_name ; // spyee  channel name
	char spyer ; // spyer flag
	int max_users ; // zero or max users for this conference
#if ( SILDET == 2 )
	// voice flags
	int vad_flag;
	int denoise_flag;
	int agc_flag;
#endif
	int via_telephone;
	// video conference params
	int id;
	// muting options - this member will not be heard/seen
	int mute_audio;
	int muted; // should incoming audio be muted while we play?
	// volume level adjustment for this member
	int talk_volume;
	int listen_volume;

	// moh flags
	char moh_flag;
	char moh_stop;

	// hold option flag
	char hold_flag;

	// this member will not hear/see
	int norecv_audio;
	// is this person a moderator?
	int ismoderator;
	int kick_conferees;
#if ( SILDET == 2 )
	// vad voice probability thresholds
	float vad_prob_start ;
	float vad_prob_continue ;
#endif
	// ready flag
	short ready_for_outgoing ;

	// input frame queue
	conf_frame* inFrames ;
	conf_frame* inFramesTail ;
	unsigned int inFramesCount ;
#ifdef	DTMF
	conf_frame* inDTMFFrames ;
	conf_frame* inDTMFFramesTail ;
	unsigned int inDTMFFramesCount ;
#endif
#ifdef	SMOOTHER
	// input/output smoother
	struct ast_smoother *inSmoother;
#ifdef	PACKER
	struct ast_packer *outPacker;
#endif
	int smooth_size_in;
	int smooth_size_out;
	int smooth_multiple;
#endif
	// frames needed by conference_exec
	unsigned int inFramesNeeded ;
#ifdef	AST_CONF_CACHE_LAST_FRAME
	// used when caching last frame
	conf_frame* inFramesLast ;
	unsigned int inFramesRepeatLast ;
	unsigned short okayToCacheLast ;
#endif
	// LL output frame queue
	conf_frame* outFrames ;
	conf_frame* outFramesTail ;
	unsigned int outFramesCount ;
#ifdef	DTMF
	conf_frame* outDTMFFrames ;
	conf_frame* outDTMFFramesTail ;
	unsigned int outDTMFFramesCount ;
#endif
	// relay dtmf to manager?
	short dtmf_relay;
	// initial nat delay flag
	short first_frame_received;

	// time we last dropped a frame
	struct timeval last_in_dropped ;
	struct timeval last_out_dropped ;

	// ( not currently used )
	// int samplesperframe ;

	// used for determining need to mix frames
	// and for management interface notification
	// and for VAD based video switching
	short local_speaking_state; // This flag will be true only if this member is speaking

	// pointer to next member in linked list
	struct ast_conf_member* next ;

	// pointer to prev member in linked list
	struct ast_conf_member* prev ;

	// pointer to member's bucket list head
	struct channel_bucket *bucket;
	// list entry for member's bucket list
	AST_LIST_ENTRY(ast_conf_member) hash_entry ;

	// spyer pointer to spyee or vice versa
	struct ast_conf_member* spy_partner ;
	// spyee pointer to whisper frame
	struct conf_frame* whisper_frame ;

	// accounting values
	unsigned long frames_in ;
	unsigned long frames_in_dropped ;
	unsigned long frames_out ;
	unsigned long frames_out_dropped ;

#ifdef	DTMF
	unsigned long dtmf_frames_in ;
	unsigned long dtmf_frames_in_dropped ;
	unsigned long dtmf_frames_out ;
	unsigned long dtmf_frames_out_dropped ;
#endif

	// for counting sequentially dropped frames
	unsigned int sequential_drops ;
	unsigned long since_dropped ;

	// start time
	struct timeval time_entered ;
	struct timeval lastsent_timeval ;

	// flag indicating we should remove this member
	char kick_flag ;

#if ( SILDET == 2 )
	// pointer to speex preprocessor dsp
	SpeexPreprocessState *dsp ;
        // number of frames to ignore speex_preprocess()
	int ignore_speex_count;
#endif

	// audio format this member is using
	int write_format ;
	int read_format ;

	int write_format_index ;
	int read_format_index ;

	// member frame translators
	struct ast_trans_pvt* to_slinear ;
	struct ast_trans_pvt* from_slinear ;

	// For playing sounds
	struct ast_conf_soundq *soundq;
} ;

struct conf_member
{
	struct ast_conf_member* realmember ;
	struct conf_member* next ;
} ;

//
// function declarations
//

#if	ASTERISK == 14
int member_exec( struct ast_channel* chan, void* data ) ;
#else
int member_exec( struct ast_channel* chan, const char* data ) ;
#endif

struct ast_conf_member* check_active_video( int id, struct ast_conference *conf );

struct ast_conf_member* create_member( struct ast_channel* chan, const char* data, char* conf_name ) ;
struct ast_conf_member* delete_member( struct ast_conf_member* member ) ;

#ifdef	CACHE_CONTROL_BLOCKS
void freembrblocks(void);
#endif

// incoming queue
int queue_incoming_frame( struct ast_conf_member* member, struct ast_frame* fr ) ;
#ifdef	DTMF
int queue_incoming_dtmf_frame( struct ast_conf_member* member, const struct ast_frame* fr ) ;
#endif
conf_frame* get_incoming_frame( struct ast_conf_member* member ) ;
#ifdef	DTMF
conf_frame* get_incoming_dtmf_frame( struct ast_conf_member* member ) ;
#endif
// outgoing queue
int queue_outgoing_frame( struct ast_conf_member* member, const struct ast_frame* fr, struct timeval delivery ) ;
int __queue_outgoing_frame( struct ast_conf_member* member, const struct ast_frame* fr, struct timeval delivery ) ;
conf_frame* get_outgoing_frame( struct ast_conf_member* member ) ;

conf_frame* get_outgoing_video_frame( struct ast_conf_member* member ) ;
#ifdef	DTMF
int queue_outgoing_dtmf_frame( struct ast_conf_member* member, const struct ast_frame* fr ) ;
#endif
#ifdef	DTMF
conf_frame* get_outgoing_dtmf_frame( struct ast_conf_member* member ) ;
#endif
void member_process_spoken_frames(struct ast_conference* conf,
				  struct ast_conf_member *member,
				  struct conf_frame **spoken_frames,
				  long time_diff,
				 int *listener_count,
				 int *speaker_count);

void member_process_outgoing_frames(struct ast_conference* conf,
				    struct ast_conf_member *member);

#ifdef	PACKER
//
// packer functions
//

struct ast_packer;

extern struct ast_packer *ast_packer_new(int bytes);
extern void ast_packer_set_flags(struct ast_packer *packer, int flags);
extern int ast_packer_get_flags(struct ast_packer *packer);
extern void ast_packer_free(struct ast_packer *s);
extern void ast_packer_reset(struct ast_packer *s, int bytes);
extern int ast_packer_feed(struct ast_packer *s, const struct ast_frame *f);
extern struct ast_frame *ast_packer_read(struct ast_packer *s);
#endif

#endif
