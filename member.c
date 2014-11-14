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

#include <stdio.h>
#include "asterisk/autoconfig.h"
#include "member.h"
#include "frame.h"

#include "asterisk/musiconhold.h"

#ifdef	CACHE_CONTROL_BLOCKS
AST_MUTEX_DEFINE_STATIC(mbrblocklist_lock);

#ifdef	SPEAKER_SCOREBOARD
static int last_score_id;
AST_MUTEX_DEFINE_STATIC(speaker_scoreboard_lock);
#endif

#endif

char kick_data[160+AST_FRIENDLY_OFFSET] = { [AST_FRIENDLY_OFFSET] = KICK };
char stop_data[160+AST_FRIENDLY_OFFSET] = { [AST_FRIENDLY_OFFSET] = STOP };
char hold_data[160+AST_FRIENDLY_OFFSET] = { [AST_FRIENDLY_OFFSET] = HOLD };
char cont_data[160+AST_FRIENDLY_OFFSET] = { [AST_FRIENDLY_OFFSET] = CONT };

#if     ASTERISK_SRC_VERSION == 104
struct ast_frame kick_frame = {.frametype = AST_FRAME_TEXT, .data = &kick_data[AST_FRIENDLY_OFFSET], .datalen = 160};
struct ast_frame stop_frame = {.frametype = AST_FRAME_TEXT, .data = &stop_data[AST_FRIENDLY_OFFSET], .datalen = 160};
struct ast_frame hold_frame = {.frametype = AST_FRAME_TEXT, .data = &hold_data[AST_FRIENDLY_OFFSET], .datalen = 160};
struct ast_frame cont_frame = {.frametype = AST_FRAME_TEXT, .data = &cont_data[AST_FRIENDLY_OFFSET], .datalen = 160};
#else
struct ast_frame kick_frame = {.frametype = AST_FRAME_TEXT, .data.ptr = &kick_data[AST_FRIENDLY_OFFSET], .datalen = 160};
struct ast_frame stop_frame = {.frametype = AST_FRAME_TEXT, .data.ptr = &stop_data[AST_FRIENDLY_OFFSET], .datalen = 160};
struct ast_frame hold_frame = {.frametype = AST_FRAME_TEXT, .data.ptr = &hold_data[AST_FRIENDLY_OFFSET], .datalen = 160};
struct ast_frame cont_frame = {.frametype = AST_FRAME_TEXT, .data.ptr = &cont_data[AST_FRIENDLY_OFFSET], .datalen = 160};
#endif

// process an incoming frame.  Returns 0 normally, 1 if hangup was received.
static int process_incoming(ast_conf_member *member, ast_conference *conf, struct ast_frame *f)
{
	switch (f->frametype)
	{
		case AST_FRAME_VOICE:
		{
			if (member->mute_audio
				|| member->muted
				||  conf->membercount == 1)
			{
				// free the input frame
				ast_frfree(f);
				return 0;
			}
#if	SILDET == 1 || SILDET == 2
			// reset silence detection flag
			int is_silent_frame = 0;
			//
			// make sure we have a valid dsp and frame type
			//
			if (member->dsp)
			{
				// send the frame to the preprocessor
				f = convert_frame(member->to_dsp, f, 1);
#if	SILDET == 1
#if	ASTERISK_SRC_VERSION == 104
				if (!WebRtcVad_Process(member->dsp, AST_CONF_SAMPLE_RATE, f->data, AST_CONF_BLOCK_SAMPLES))
#else
				if (!WebRtcVad_Process(member->dsp, AST_CONF_SAMPLE_RATE, f->data.ptr, AST_CONF_BLOCK_SAMPLES))
#endif
#elif	SILDET == 2
#if	ASTERISK_SRC_VERSION == 104
				if (!speex_preprocess(member->dsp, f->data, NULL))
#else
				if (!speex_preprocess(member->dsp, f->data.ptr, NULL))
#endif
#endif
				{
					//
					// we ignore the preprocessor's outcome if we've seen voice frames
					// in within the last AST_CONF_FRAMES_TO_SKIP frames
					//
					if (member->ignore_vad_result > 0)
					{
						// skip speex_preprocess(), and decrement counter
						if (!--member->ignore_vad_result) {
#if	defined(SPEAKER_SCOREBOARD) && defined(CACHE_CONTROL_BLOCKS)
							*(speaker_scoreboard + member->score_id) = '\x00';
#else
							manager_event(
								EVENT_FLAG_CONF,
								"ConferenceState",
								"Channel: %s\r\n"
								"Flags: %s\r\n"
								"State: %s\r\n",
#if	ASTERISK_SRC_VERSION < 1100
								member->chan->name,
#else
								ast_channel_name(member->chan),
#endif
								member->flags,
								"silent"
							);
#endif
						}
					}
					else
					{
						// set silent_frame flag
						is_silent_frame = 1;
					}
				}
				else
				{
					if (!member->ignore_vad_result) {
#if	defined(SPEAKER_SCOREBOARD) && defined(CACHE_CONTROL_BLOCKS)
						*(speaker_scoreboard + member->score_id) = '\x01';
#else
						manager_event(
							EVENT_FLAG_CONF,
							"ConferenceState",
							"Channel: %s\r\n"
							"Flags: %s\r\n"
							"State: %s\r\n",
#if	ASTERISK_SRC_VERSION < 1100
							member->chan->name,
#else
							ast_channel_name(member->chan),
#endif
							member->flags,
							"speaking"
						);
#endif
					}
					// voice detected, reset skip count
					member->ignore_vad_result = AST_CONF_FRAMES_TO_IGNORE;
				}
			}
			if (!is_silent_frame)
#endif
				queue_incoming_frame(member, f);
			break;
		}
		// In Asterisk 1.4 AST_FRAME_DTMF is equivalent to AST_FRAME_DTMF_END
		case AST_FRAME_DTMF:
		{
			if (member->dtmf_relay)
			{
				// output to manager...
				manager_event(
					EVENT_FLAG_CONF,
					"ConferenceDTMF",
					"ConferenceName: %s\r\n"
					"Type: %s\r\n"
					"UniqueID: %s\r\n"
					"Channel: %s\r\n"
					"CallerID: %s\r\n"
					"CallerIDName: %s\r\n"
					"Key: %c\r\n"
					"Count: %d\r\n"
					"Flags: %s\r\n"
					"Mute: %d\r\n",
					conf->name,
					member->type,
#if	ASTERISK_SRC_VERSION < 1100
					member->chan->uniqueid,
					member->chan->name,
#else
					ast_channel_uniqueid(member->chan),
					ast_channel_name(member->chan),
#endif
#if	ASTERISK_SRC_VERSION == 104 || ASTERISK_SRC_VERSION == 106
					member->chan->cid.cid_num ? member->chan->cid.cid_num : "unknown",
					member->chan->cid.cid_name ? member->chan->cid.cid_name : "unknown",
					f->subclass,
#else
#if	ASTERISK_SRC_VERSION < 1100
					member->chan->caller.id.number.str ? member->chan->caller.id.number.str : "unknown",
					member->chan->caller.id.name.str ? member->chan->caller.id.name.str: "unknown",
#else
					S_COR(ast_channel_caller(member->chan)->id.number.valid, ast_channel_caller(member->chan)->id.number.str, "<unknown>"),
					S_COR(ast_channel_caller(member->chan)->id.name.valid, ast_channel_caller(member->chan)->id.name.str, "<unknown>"),
#endif
					f->subclass.integer,
#endif
					conf->membercount,
					member->flags,
					member->mute_audio
					);

			}
			break;
		}
		case AST_FRAME_CONTROL:
		{
#if	ASTERISK_SRC_VERSION == 104 || ASTERISK_SRC_VERSION == 106
			if (f->subclass == AST_CONTROL_HANGUP)
#else
			if (f->subclass.integer == AST_CONTROL_HANGUP)
#endif
			{
				ast_frfree(f);

				return 1; // break while (42)
			}
			break;
		}
		case AST_FRAME_TEXT:
		{
#if     ASTERISK_SRC_VERSION == 104
			switch (((const char *)f->data)[0])
#else
			switch (((const char *)f->data.ptr)[0])
#endif
			{
				case KICK:
				{
						member->kick_flag = 1;
						ast_frfree(f);
						return 1; // break while (42)
				}
				case 'P':
				{
#if	ASTERISK_SRC_VERSION < 1100
					if (!ast_test_flag(member->chan, AST_FLAG_MOH))
#else
					if (!ast_test_flag(ast_channel_flags(member->chan), AST_FLAG_MOH))
#endif
					{
						ast_conf_soundq *newsound;
						ast_conf_soundq **q;
#if     ASTERISK_SRC_VERSION == 104
						char *sound = (char*)f->data;
#else
						char *sound = (char*)f->data.ptr;
#endif
						newsound = ast_calloc(1, sizeof(ast_conf_soundq));
						ast_copy_string(newsound->name, &sound[1], 160-1);
						for (q=&member->soundq; *q; q = &((*q)->next));
						*q = newsound;
						if (member->soundq == newsound)
							ast_activate_generator(member->chan, &play_sound_generator, (void *)member);
					}
					break;
				}
				case STOP:
				{
#if     ASTERISK_SRC_VERSION < 1100
					if (!ast_test_flag(member->chan, AST_FLAG_MOH))
#else
					if (!ast_test_flag(ast_channel_flags(member->chan), AST_FLAG_MOH))
#endif
					{
						ast_conf_soundq *next;
						ast_conf_soundq *sound = member->soundq;

						member->soundq = NULL;

						while (sound)
						{
							if (sound->stream)
								ast_stopstream(member->chan);
							next = sound->next;
							ast_free(sound);
							sound = next;
						}

						member->muted = 0;

						ast_deactivate_generator(member->chan);
					}
					break;
				}
				case HOLD:
				{
					ast_conf_soundq *next;
					ast_conf_soundq *sound = member->soundq;

					member->soundq = NULL;

					while (sound)
					{
						if (sound->stream)
							ast_stopstream(member->chan);
						next = sound->next;
						ast_free(sound);
						sound = next;
					}

					member->muted = 1;
					member->ready_for_outgoing = 0;

					ast_moh_start(member->chan, NULL, NULL);
					break;
				}
				case CONT:
				{
#if     ASTERISK_SRC_VERSION < 1100
					if (ast_test_flag(member->chan, AST_FLAG_MOH))
#else
					if (ast_test_flag(ast_channel_flags(member->chan), AST_FLAG_MOH))
#endif
					{
						member->muted = 0;
						member->ready_for_outgoing = 1;

						ast_moh_stop(member->chan);
					}
					break;
				}
				default:
				{
#if     ASTERISK_SRC_VERSION == 104
					ast_log(LOG_NOTICE, "processing unknown msg text = %s channel = %s\n", (char *)f->data, member->chan->name);
#elif	ASTERISK_SRC_VERSION < 1100
					ast_log(LOG_NOTICE, "processing unknown msg text = %s channel = %s\n", (char *)f->data.ptr, member->chan->name);
#else
					ast_log(LOG_NOTICE, "processing unknown msg text = %s channel = %s\n", (char *)f->data.ptr, ast_channel_name(member->chan));
#endif
					break;
				}
			}
		}
		default:
		{
			break;
		}
	}

	// free the input frame
	ast_frfree(f);

	return 0;
}

// process outgoing frames for the channel
static void process_outgoing(ast_conf_member *member)
{
	struct ast_frame *cf;

	while ((cf = get_outgoing_frame(member)))
	{
		// send the frame
		ast_write(member->chan, cf);

                // free voice frame
#ifdef  CACHE_AST_FRAMES
		astframe_cache_put(&member->outgoingq.cache, cf);
#else
		ast_frfree(cf);
#endif
	}
}

//
// main member thread function
//

#if	ASTERISK_SRC_VERSION == 104
int member_exec(struct ast_channel* chan, void* data)
#else
int member_exec(struct ast_channel* chan, const char* data)
#endif
{
	//
	// If the call has not yet been answered, answer the call
	// Note: asterisk apps seem to check _state, but it seems like it's safe
	// to just call ast_answer.  It will just do nothing if it is up.
	// it will also return -1 if the channel is a zombie, or has hung up.
	//

	if (ast_answer(chan))
	{
		ast_log(LOG_ERROR, "unable to answer call\n");
		return -1;
	}

	//
	// create a new member for the conference
 	//

	int max_users = AST_CONF_MAX_USERS;
	char conf_name[CONF_NAME_LEN + 1]  = { 0 };
	ast_conf_member *member;

	if (!(member = create_member(chan, (const char*)(data), conf_name, &max_users)))
	{
		// unable to create member, return an error
		ast_log(LOG_ERROR, "unable to create member\n");
		return -1;
	}

	//
	// setup a conference for the new member
	//

	ast_conference *conf;

	if (!(conf = join_conference(member, conf_name, max_users)))
	{
		delete_member(member);
		const char *konference = pbx_builtin_getvar_helper(chan, "KONFERENCE");
		return !strcmp(konference, "MAXUSERS") ? 0 : -1;
	}

	member->listheaders = conf->listheaders;
	ao2_ref(conf->listheaders, +1);

	// add member to channel table
#if	ASTERISK_SRC_VERSION < 1100
	member->bucket = &(channel_table[hash(member->chan->name) % CHANNEL_TABLE_SIZE]);
#else
	member->bucket = &(channel_table[hash(ast_channel_name(member->chan)) % CHANNEL_TABLE_SIZE]);
#endif

	AST_LIST_LOCK(member->bucket);
	AST_LIST_INSERT_HEAD(member->bucket, member, hash_entry);
	AST_LIST_UNLOCK(member->bucket);

	manager_event(
		EVENT_FLAG_CONF,
		"ConferenceJoin",
		"ConferenceName: %s\r\n"
		"Type: %s\r\n"
		"UniqueID: %s\r\n"
		"Member: %d\r\n"
#if	defined(SPEAKER_SCOREBOARD) && defined(CACHE_CONTROL_BLOCKS)
		"ScoreID: %d\r\n"
#endif
		"Flags: %s\r\n"
		"Channel: %s\r\n"
		"CallerID: %s\r\n"
		"CallerIDName: %s\r\n"
		"Moderators: %d\r\n"
		"Count: %d\r\n",
		conf->name,
		member->type,
#if	ASTERISK_SRC_VERSION < 1100
		member->chan->uniqueid,
#else
		ast_channel_uniqueid(member->chan),
#endif
		member->conf_id,
#if	defined(SPEAKER_SCOREBOARD) && defined(CACHE_CONTROL_BLOCKS)
		member->score_id,
#endif
		member->flags,
#if	ASTERISK_SRC_VERSION < 1100
		member->chan->name,
#else
		ast_channel_name(member->chan),
#endif
#if	ASTERISK_SRC_VERSION == 104 || ASTERISK_SRC_VERSION == 106
		member->chan->cid.cid_num ? member->chan->cid.cid_num : "unknown",
		member->chan->cid.cid_name ? member->chan->cid.cid_name : "unknown",
#else
#if	ASTERISK_SRC_VERSION < 1100
		member->chan->caller.id.number.str ? member->chan->caller.id.number.str : "unknown",
		member->chan->caller.id.name.str ? member->chan->caller.id.name.str: "unknown",
#else
		S_COR(ast_channel_caller(member->chan)->id.number.valid, ast_channel_caller(member->chan)->id.number.str, "<unknown>"),
		S_COR(ast_channel_caller(member->chan)->id.name.valid, ast_channel_caller(member->chan)->id.name.str, "<unknown>"),
#endif
#endif
		conf->moderators,
		conf->membercount
	);

	// if spyer setup failed, set variable and exit conference
	if (member->spyee_channel_name && AST_LIST_EMPTY(&member->spy_list.head))
	{
		remove_member(member, conf, conf_name);
		pbx_builtin_setvar_helper(member->chan, "KONFERENCE", "SPYFAILED");
		return 0;
	}

	// tell conference thread we're ready for frames
	member->ready_for_outgoing = 1;

	//
	// member thread loop
	//

	int left;
	struct ast_frame *f; // frame received from ast_read()

	while (42)
	{
		// wait for an event on this channel
		if ((left  = ast_waitfor(chan, AST_CONF_WAITFOR_LATENCY)) > 0)
		{
			// a frame has come in before the latency timeout
			// was reached, so we process the frame

			if (!(f = ast_read(chan)) || process_incoming(member, conf, f))
			{
				// they probably want to hangup...
				break;
			}

		}
		else if (left == 0)
		{
			// no frame has arrived yet
#if	ASTERISK_SRC_VERSION < 1100
			// ast_log(LOG_NOTICE, "no frame available from channel, channel => %s\n", chan->name);
#else
			// ast_log(LOG_NOTICE, "no frame available from channel, channel => %s\n", ast_channel_name(chan));
#endif
		}
		else if (left < 0)
		{
			// an error occured
			ast_log(
				LOG_NOTICE,
				"an error occured waiting for a frame, channel => %s, error => %d\n",
#if	ASTERISK_SRC_VERSION < 1100
				chan->name, left
#else
				ast_channel_name(chan), left
#endif
			);
			break;
		}

		// process outgoing frames
		process_outgoing(member);
	}

	//
	// clean up
	//
	if (member->kick_flag)
		pbx_builtin_setvar_helper(member->chan, "KONFERENCE", "KICKED");
	remove_member(member, conf, conf_name);
	return 0;
}

//
// manange member functions
//

ast_conf_member* create_member(struct ast_channel *chan, const char* data, char* conf_name, int* max_users)
{
	ast_conf_member *member;
#ifdef	CACHE_CONTROL_BLOCKS
#ifdef	SPEAKER_SCOREBOARD
	int score_id;
#endif
	ast_mutex_lock(&mbrblocklist_lock);
	if (!AST_LIST_EMPTY(&mbrBlockList))
	{
		// get member control block from the free list
		member  = AST_LIST_REMOVE_HEAD(&mbrBlockList, free_list);
		ast_mutex_unlock(&mbrblocklist_lock);
#ifdef	SPEAKER_SCOREBOARD
		score_id = member->score_id;
#endif
		memset(member,0,sizeof(ast_conf_member));
	}
	else
	{
		ast_mutex_unlock(&mbrblocklist_lock);
#endif
		// allocate new member control block
		if (!(member = ast_calloc(1,  sizeof(ast_conf_member))))
		{
			ast_log(LOG_ERROR, "unable to calloc ast_conf_member\n");
			return NULL;
		}
#ifdef	CACHE_CONTROL_BLOCKS
#ifdef	SPEAKER_SCOREBOARD
		ast_mutex_lock(&speaker_scoreboard_lock);
		score_id = last_score_id < SPEAKER_SCOREBOARD_SIZE ? ++last_score_id : 0;
		ast_mutex_unlock(&speaker_scoreboard_lock);
	}
	// initialize score board identifier
	member->score_id = score_id;
	// initialize score board entry
	*(speaker_scoreboard + score_id) = '\x00';
#else
	}
#endif
#endif

	// initialize mutexes
	ast_mutex_init(&member->lock);
	ast_mutex_init(&member->incomingq.lock);
	ast_mutex_init(&member->outgoingq.lock);

	// initialize cv
	ast_cond_init(&member->delete_var, NULL);

	// Default values for parameters that can get overwritten by dialplan arguments
#if	SILDET == 2
	member->vad_prob_start = AST_CONF_PROB_START;
	member->vad_prob_continue = AST_CONF_PROB_CONTINUE;
#endif

	//
	// initialize member with passed data values
	//
	char argstr[256] = { 0 };

	// copy the passed data
	strncpy(argstr, data, sizeof(argstr) - 1);

	// point to the copied data
	char *stringp = argstr;

	// parse the id
	char *token;
	if ((token = strsep(&stringp, argument_delimiter)))
	{
		strncpy(conf_name, token, CONF_NAME_LEN);
	}
	else
	{
#if	ASTERISK_SRC_VERSION < 1100
		ast_log(LOG_ERROR, "create_member unable to parse member data: channel name = %s, data = %s\n", chan->name, data);
#else
		ast_log(LOG_ERROR, "create_member unable to parse member data: channel name = %s, data = %s\n", ast_channel_name(chan), data);
#endif
		ast_free(member);
		return NULL;
	}

	// parse the flags
	if ((token = strsep(&stringp, argument_delimiter)))
	{
		strncpy(member->flags, token, MEMBER_FLAGS_LEN);
	}

	while ((token = strsep(&stringp, argument_delimiter)))
	{
#if	SILDET == 2
		static const char arg_vad_prob_start[] = "vad_prob_start";
		static const char arg_vad_prob_continue[] = "vad_prob_continue";
#endif
		static const char arg_max_users[] = "max_users";
		static const char arg_conf_type[] = "type";
		static const char arg_chanspy[] = "spy";

		char *value = token;
		const char *key = strsep(&value, "=");
		
		if (!key || !value)
		{
			ast_log(LOG_WARNING, "Incorrect argument %s\n", token);
			continue;
		}

		if (!strncasecmp(key, arg_max_users, sizeof(arg_max_users) - 1))
		{
			*max_users = strtol(value, (char **)NULL, 10);
		} else if (!strncasecmp(key, arg_conf_type, sizeof(arg_conf_type) - 1))
		{
			strncpy(member->type, value, MEMBER_TYPE_LEN);
		} else if (!strncasecmp(key, arg_chanspy, sizeof(arg_chanspy) - 1))
		{
			member->spyee_channel_name = ast_malloc(strlen(value) + 1);
			strcpy(member->spyee_channel_name, value);
#if	SILDET == 2
		} else if (!strncasecmp(key, arg_vad_prob_start, sizeof(arg_vad_prob_start) - 1))
		{
			member->vad_prob_start = strtof(value, (char **)NULL);
		} else if (!strncasecmp(key, arg_vad_prob_continue, sizeof(arg_vad_prob_continue) - 1))
		{
			member->vad_prob_continue = strtof(value, (char **)NULL);
#endif
		} else
		{
			ast_log(LOG_WARNING, "unknown parameter %s with value %s\n", key, value);
		}

	}

	//
	// initialize member with default values
	//

	// keep pointer to member's channel
	member->chan = chan;

	// set default if no type parameter
	if (!(*(member->type))) {
		strcpy(member->type, AST_CONF_TYPE_DEFAULT);
	}

	// record start time
	member->time_entered = ast_tvnow();
	//
	// parse passed flags
	//

	// temp pointer to flags string
	char* flags = member->flags;

	int i;

	for (i = 0; i < strlen(flags); ++i)
	{
		{
			// flags are L, l, a, T, V, D, A, R, M, x
			switch (flags[i])
			{
				// mute/no_recv options
			case 'L':
				member->mute_audio = 1;
				break;
			case 'l':
				member->norecv_audio = 1;
				break;
#if	SILDET == 1
				//Telephone connection
			case 'a':
			case 'T':
				member->via_telephone = 1;
				break;
#elif	SILDET == 2
				//Telephone connection
			case 'a':
				member->vad_flag = 1;
			case 'T':
				member->via_telephone = 1;
				break;
			case 'V':
				member->vad_flag = 1;
				break;
				// speex preprocessing options
			case 'D':
				member->denoise_flag = 1;
				break;
			case 'A':
				member->agc_flag = 1;
				break;
#endif
				// dtmf/moderator options
			case 'R':
				member->dtmf_relay = 1;
				break;
			case 'M':
				member->ismoderator = 1;
				break;
			case 'x':
				member->kick_conferees = 1;
				break;
			default:
				break;
			}
		}
	}

	//
	// configure silence detection and preprocessing
	// if the user is coming in via the telephone,
	// and is not listen-only
	//
#if	SILDET == 1
	if (member->via_telephone)
	{
		// create a webrtc preprocessor

		if (!WebRtcVad_Create(&member->dsp))
		{
			WebRtcVad_Init(member->dsp);
#if	AST_CONF_VAD_MODE
			WebRtcVad_set_mode(member->dsp, AST_CONF_VAD_MODE);
#endif
		}
		else
		{
#if	ASTERISK_SRC_VERSION < 1100
			ast_log(LOG_WARNING, "unable to initialize member dsp, channel => %s\n", chan->name);
#else
			ast_log(LOG_WARNING, "unable to initialize member dsp, channel => %s\n", ast_channel_name(chan));
#endif
		}
	}
#elif	SILDET == 2
	if (member->via_telephone)
	{
		// create a speex preprocessor

		if ((member->dsp = speex_preprocess_state_init(AST_CONF_BLOCK_SAMPLES, AST_CONF_SAMPLE_RATE)))
		{
			// set speex preprocessor options
			speex_preprocess_ctl(member->dsp, SPEEX_PREPROCESS_SET_VAD, &(member->vad_flag));
			speex_preprocess_ctl(member->dsp, SPEEX_PREPROCESS_SET_DENOISE, &(member->denoise_flag));
			speex_preprocess_ctl(member->dsp, SPEEX_PREPROCESS_SET_AGC, &(member->agc_flag));

			speex_preprocess_ctl(member->dsp, SPEEX_PREPROCESS_SET_PROB_START, &member->vad_prob_start);
			speex_preprocess_ctl(member->dsp, SPEEX_PREPROCESS_SET_PROB_CONTINUE, &member->vad_prob_continue);
		}
		else
		{
#if	ASTERISK_SRC_VERSION < 1100
			ast_log(LOG_WARNING, "unable to initialize member dsp, channel => %s\n", chan->name);
#else
			ast_log(LOG_WARNING, "unable to initialize member dsp, channel => %s\n", ast_channel_name(chan));
#endif
		}
	}
#endif	// SILDET

	// set translation paths
#if	SILDET == 1 || SILDET == 2

	if (member->dsp)
	{
#if	ASTERISK_SRC_VERSION < 1000
		member->to_dsp = ast_translator_build_path(AST_FORMAT_CONFERENCE, chan->readformat);
#elif	ASTERISK_SRC_VERSION < 1100
		member->to_dsp = ast_translator_build_path(&ast_format_conference, &chan->readformat);
#elif	ASTERISK_SRC_VERSION < 1300
		member->to_dsp = ast_translator_build_path(&ast_format_conference, ast_channel_readformat(chan));
#else
		member->to_dsp = ast_translator_build_path(ast_format_conference, ast_channel_readformat(chan));
#endif
	}
	else
	{
#if	ASTERISK_SRC_VERSION < 1000
		member->to_slinear = ast_translator_build_path(AST_FORMAT_CONFERENCE, member->chan->readformat);
#elif	ASTERISK_SRC_VERSION < 1100
		member->to_slinear = ast_translator_build_path(&ast_format_conference, &member->chan->readformat);
#elif	ASTERISK_SRC_VERSION < 1300
		member->to_slinear = ast_translator_build_path(&ast_format_conference, ast_channel_readformat(chan));
#else
		member->to_slinear = ast_translator_build_path(ast_format_conference, ast_channel_readformat(chan));
#endif
	}
#else	// SILDET

#if	ASTERISK_SRC_VERSION < 1000
	member->to_slinear = ast_translator_build_path(AST_FORMAT_CONFERENCE, member->chan->readformat);
#elif	ASTERISK_SRC_VERSION < 1100
	member->to_slinear = ast_translator_build_path(&ast_format_conference, &member->chan->readformat);
#elif	ASTERISK_SRC_VERSION < 1300
	member->to_slinear = ast_translator_build_path(&ast_format_conference, ast_channel_readformat(chan));
#else
	member->to_slinear = ast_translator_build_path(ast_format_conference, ast_channel_readformat(chan));
#endif

#endif	// SILDET

#if	ASTERISK_SRC_VERSION < 1000
	member->from_slinear = ast_translator_build_path(member->chan->writeformat, AST_FORMAT_CONFERENCE);
#elif	ASTERISK_SRC_VERSION < 1100
	member->from_slinear = ast_translator_build_path(&member->chan->writeformat, &ast_format_conference);
#elif	ASTERISK_SRC_VERSION < 1300
	member->from_slinear = ast_translator_build_path(ast_channel_writeformat(member->chan), &ast_format_conference);
#else
	member->from_slinear = ast_translator_build_path(ast_channel_writeformat(member->chan), ast_format_conference);
#endif

	// index for converted_frames array
#if	ASTERISK_SRC_VERSION < 1000
	switch (member->chan->writeformat)
#elif	ASTERISK_SRC_VERSION < 1100
	switch (member->chan->writeformat.id)
#elif	ASTERISK_SRC_VERSION < 1300
	switch (ast_channel_writeformat(member->chan)->id)
#else
	switch (ast_format_compatibility_format2bitfield(ast_channel_writeformat(member->chan)))
#endif
	{
		case AST_FORMAT_CONFERENCE:
			member->write_format = AC_CONF;
			break;

		case AST_FORMAT_ULAW:
			member->write_format = AC_ULAW;
			break;
#ifdef  AC_USE_ALAW
	        case AST_FORMAT_ALAW:
			member->write_format = AC_ALAW;
			break;
#endif
#ifdef	AC_USE_GSM
		case AST_FORMAT_GSM:
			member->write_format = AC_GSM;
			break;
#endif
#ifdef	AC_USE_SPEEX
		case AST_FORMAT_SPEEX:
			member->write_format = AC_SPEEX;
			break;
#endif
#ifdef AC_USE_G729A
		case AST_FORMAT_G729A:
			member->write_format = AC_G729A;
			break;
#endif
#ifdef AC_USE_G722
		case AST_FORMAT_SLINEAR:
			member->write_format = AC_SLINEAR;
			break;
		case AST_FORMAT_G722:
			member->write_format = AC_G722;
			break;
#endif
		default:
			delete_member(member);
			return NULL;
	}

	// index for converted_frames array
#if	SILDET == 1 || SILDET == 2

#if	ASTERISK_SRC_VERSION < 1000
	switch (member->dsp ? AST_FORMAT_CONFERENCE : member->chan->readformat)
#elif	ASTERISK_SRC_VERSION < 1100
	switch (member->dsp ? AST_FORMAT_CONFERENCE : member->chan->readformat.id)
#elif	ASTERISK_SRC_VERSION < 1300
	switch (member->dsp ? AST_FORMAT_CONFERENCE : ast_channel_readformat(member->chan)->id)
#else
	switch (member->dsp ? AST_FORMAT_CONFERENCE : ast_format_compatibility_format2bitfield(ast_channel_readformat(member->chan)))
#endif

#else	// SILDET

#if	ASTERISK_SRC_VERSION < 1000
	switch (member->chan->readformat)
#elif	ASTERISK_SRC_VERSION < 1100
	switch (member->chan->readformat.id)
#elif	ASTERISK_SRC_VERSION < 1300
	switch (ast_channel_readformat(member->chan)->id)
#else
	switch (ast_format_compatibility_format2bitfield(ast_channel_readformat(member->chan)))
#endif

#endif	// SILDET
	{
		case AST_FORMAT_CONFERENCE:
			member->read_format = AC_CONF;
#ifdef	CACHE_AST_FRAMES
			member->pkt_size = 320;
#endif
			break;

		case AST_FORMAT_ULAW:
			member->read_format = AC_ULAW;
#ifdef	CACHE_AST_FRAMES
#if	SILDET == 1 || SILDET == 2
			member->pkt_size = !member->dsp ? 160 : 320;
#else
			member->pkt_size = 160;
#endif
#endif
			break;
#ifdef  AC_USE_ALAW
		case AST_FORMAT_ALAW:
			member->read_format = AC_ALAW;
#ifdef	CACHE_AST_FRAMES
#if	SILDET == 1 || SILDET == 2
			member->pkt_size = !member->dsp ? 160 : 320;
#else
			member->pkt_size = 160;
#endif
#endif
			break;
#endif
#ifdef	AC_USE_GSM
		case AST_FORMAT_GSM:
			member->read_format = AC_GSM;
			break;
#endif
#ifdef	AC_USE_SPEEX
		case AST_FORMAT_SPEEX:
			member->read_format = AC_SPEEX;
			break;
#endif
#ifdef AC_USE_G729A
		case AST_FORMAT_G729A:
			member->read_format = AC_G729A;
			break;
#endif
#ifdef AC_USE_G722
		case AST_FORMAT_SLINEAR:
			member->read_format = AC_SLINEAR;
			break;
		case AST_FORMAT_G722:
			member->read_format = AC_G722;
			break;
#endif
		default:
			delete_member(member);
			return NULL;
	}
	//
	// finish up
	//

	return member;
}

void delete_member(ast_conf_member* member)
{
	ast_mutex_lock(&member->lock);

	member->delete_flag = 1;
	if (member->use_count)
		ast_cond_wait(&member->delete_var, &member->lock);

	ast_mutex_unlock(&member->lock);

	// destroy member mutex and condition variable
	ast_mutex_destroy(&member->lock);
	ast_cond_destroy(&member->delete_var);

	// destroy incoming/outgoing queue mutexes
	ast_mutex_destroy(&member->incomingq.lock);
	ast_mutex_destroy(&member->outgoingq.lock);

	//
	// delete the members frames
	//
	struct ast_frame* fr;

	// incoming frames
	if ((fr = AST_LIST_FIRST(&member->incomingq.frames)))
		ast_frfree(fr);

	// outgoing frames
	if ((fr = AST_LIST_FIRST(&member->outgoingq.frames)))
		ast_frfree(fr);

	// speaker buffer
	if (member->speakerBuffer)
	{
		ast_free(member->speakerBuffer);
	}
	// speaker frames
	if (member->mixAstFrame)
	{
		ast_free(member->mixAstFrame);
	}
	if (member->mixConfFrame)
	{
		ast_free(member->mixConfFrame);
	}
#ifdef  CACHE_AST_FRAMES
	// cache frames
	if (member->outgoingq.cache)
	{
		ast_frfree(member->outgoingq.cache);
	}
	if (member->incomingq.cache)
	{
		ast_frfree(member->incomingq.cache);
	}
#endif
#if	SILDET == 1
	if (member->dsp)
	{
		WebRtcVad_Free(member->dsp);
		ast_translator_free_path(member->to_dsp);
	}
#elif	SILDET == 2
	if (member->dsp)
	{
		speex_preprocess_state_destroy(member->dsp);
		ast_translator_free_path(member->to_dsp);
	}
#endif

	// free the mixing translators
	ast_translator_free_path(member->to_slinear);
	ast_translator_free_path(member->from_slinear);

	// free the member's copy of the spyee channel name
	if (member->spyee_channel_name)
	{
		ast_free(member->spyee_channel_name);
	}

	// clear all sounds
	ast_conf_soundq *sound = member->soundq;
	ast_conf_soundq *next;

	while (sound)
	{
		next = sound->next;
		if (sound->stream)
			ast_stopstream(member->chan);
		ast_free(sound);
		sound = next;
	}

	// decrement listheader count
	if (member->listheaders)
	{
		ao2_ref(member->listheaders, -1);
	}

#ifdef	CACHE_CONTROL_BLOCKS
	// put the member control block on the free list
	ast_mutex_lock(&mbrblocklist_lock);
	AST_LIST_INSERT_HEAD(&mbrBlockList, member, free_list);
	ast_mutex_unlock(&mbrblocklist_lock);
#else
	ast_free(member);
#endif
}

//
// incoming frame functions
//

conf_frame* get_incoming_frame(ast_conf_member *member)
{
	if (!member->incomingq.count)
	{
		return NULL;
	}

	ast_mutex_lock(&member->incomingq.lock);

	// get first frame
	struct ast_frame* fr = AST_LIST_REMOVE_HEAD(&member->incomingq.frames, frame_list);

	// decrement frame count
	member->incomingq.count--;

	ast_mutex_unlock(&member->incomingq.lock);

	return create_conf_frame(member, fr);
}

void queue_incoming_frame(ast_conf_member* member, struct ast_frame* fr)
{
	//
	// create new frame from passed data frame
	//
#ifdef  CACHE_AST_FRAMES
	struct ast_frame *cf = astframe_cache_get(&member->incomingq.cache);
	if (cf)
	{    
		AST_LIST_NEXT(cf, frame_list) = NULL;
#if	ASTERISK_SRC_VERSION == 104
		memcpy(cf->data, fr->data, fr->datalen);
#else
		memcpy(cf->data.ptr, fr->data.ptr, fr->datalen);
#endif
		ast_copy_flags(cf, fr, AST_FRFLAG_HAS_TIMING_INFO);
		cf->ts = fr->ts;
		cf->len = fr->len;
		cf->seqno = fr->seqno;
		fr = cf;
	}    
	else
	{
		if (!(cf = ast_calloc_cache(1, sizeof(struct ast_frame) + AST_FRIENDLY_OFFSET + member->pkt_size)))
		{
			ast_log(LOG_ERROR, "unable to malloc incoming ast_frame\n");
			return;
		}
		cf->mallocd_hdr_len = sizeof(struct ast_frame) + AST_FRIENDLY_OFFSET + member->pkt_size;
		cf->frametype = fr->frametype;
#if	ASTERISK_SRC_VERSION < 1300
		cf->subclass = fr->subclass;
#else
		cf->subclass.format = fr->subclass.format;
#endif
		cf->datalen = member->pkt_size;
		cf->samples = 160;
		cf->delivery = fr->delivery;
		cf->mallocd = AST_MALLOCD_HDR;
		cf->offset = AST_FRIENDLY_OFFSET;
#if	ASTERISK_SRC_VERSION == 104
		cf->data = (char*)cf + sizeof(struct ast_frame) + AST_FRIENDLY_OFFSET;
		memcpy(cf->data, fr->data, fr->datalen);
#else
		cf->data.ptr = (char*)cf + sizeof(struct ast_frame) + AST_FRIENDLY_OFFSET;
		memcpy(cf->data.ptr, fr->data.ptr, fr->datalen);
#endif
		ast_copy_flags(cf, fr, AST_FRFLAG_HAS_TIMING_INFO);
		cf->ts = fr->ts;
		cf->len = fr->len;
		cf->seqno = fr->seqno;
		fr = cf;
	}
#else
	if (!(fr = ast_frdup(fr)))
	{
		ast_log(LOG_ERROR, "unable to malloc incoming ast_frame\n");
		return;
	}
#endif
	//
	// add new frame to members incoming frame queue
	// (i.e. save this frame data, so we can distribute it in conference_exec later)
	//

	ast_mutex_lock(&member->incomingq.lock);

	AST_LIST_INSERT_TAIL(&member->incomingq.frames, fr, frame_list);

	//
	// drop a frame if more than AST_CONF_MAX_QUEUE
	//
	if (++member->incomingq.count > AST_CONF_MAX_QUEUE)
	{
		ast_frfree(AST_LIST_REMOVE_HEAD(&member->incomingq.frames, frame_list));
		member->incomingq.count--;
	}

	ast_mutex_unlock(&member->incomingq.lock);
}

//
// outgoing frame functions
//

struct ast_frame* get_outgoing_frame(ast_conf_member *member)
{
	if (member->outgoingq.count)
	{
		ast_mutex_lock(&member->outgoingq.lock);

		struct ast_frame* fr = AST_LIST_REMOVE_HEAD(&member->outgoingq.frames, frame_list);

		// decrement frame count
		member->outgoingq.count--;
		ast_mutex_unlock(&member->outgoingq.lock);
		return fr;
	}

	return NULL;
}

void queue_outgoing_frame(ast_conf_member* member, struct ast_frame* fr, struct timeval delivery)
{
	//
	// create new frame from passed data frame
	//
#ifdef  CACHE_AST_FRAMES
	struct ast_frame *cf = astframe_cache_get(&member->outgoingq.cache);
        if (cf)
        {    
		AST_LIST_NEXT(cf, frame_list) = NULL;
#if	ASTERISK_SRC_VERSION == 104
                memcpy(cf->data, fr->data, fr->datalen);
#else
                memcpy(cf->data.ptr, fr->data.ptr, fr->datalen);
#endif
		ast_copy_flags(cf, fr, AST_FRFLAG_HAS_TIMING_INFO);
		cf->ts = fr->ts;
		cf->len = fr->len;
		cf->seqno = fr->seqno;
                fr = cf;
        }    
	else 
#endif
	if (!(fr  = ast_frdup(fr)))
	{
		ast_log(LOG_ERROR, "unable to malloc outgoing ast_frame\n");
		return;
	}

	// set delivery timestamp
	fr->delivery = delivery;

	//
	// add new frame to members outgoing frame queue
	//

	ast_mutex_lock(&member->outgoingq.lock);

	AST_LIST_INSERT_TAIL(&member->outgoingq.frames, fr, frame_list);

	//
	// drop a frame if more than AST_CONF_MAX_QUEUE
	//
	if (++member->outgoingq.count > AST_CONF_MAX_QUEUE)
	{
		ast_frfree(AST_LIST_REMOVE_HEAD(&member->outgoingq.frames, frame_list));
		member->outgoingq.count--;
	}

	ast_mutex_unlock(&member->outgoingq.lock);
}

void queue_frame_for_listener(
	ast_conference* conf,
	ast_conf_member* member
)
{
	struct ast_frame* qf;
	conf_frame* frame = conf->listener_frame;

	if (frame)
	{
		// try for a pre-converted frame; otherwise, convert (and store) the frame
		if ((qf = frame->converted[member->write_format]) && !member->listen_volume)
		{
			queue_outgoing_frame(member, qf, conf->delivery_time);
		}
		else
		{
			if (!member->listen_volume)
			{
				// convert using the conference's translation path
				qf = convert_frame(conf->from_slinear_paths[member->write_format], frame->fr, 0);

				frame->converted[member->write_format] = qf;

				queue_outgoing_frame(member, qf, conf->delivery_time);
			}
			else
			{
				ast_frame_adjust_volume(frame->fr, member->listen_volume);

				// convert using the conference's translation path
				qf = convert_frame(conf->from_slinear_paths[member->write_format], frame->fr, 0);

				ast_frame_adjust_volume(frame->fr, -member->listen_volume);

				queue_outgoing_frame(member, qf, conf->delivery_time);

				// free frame (the translator's copy)
				if (conf->from_slinear_paths[member->write_format])
					ast_frfree(qf);
			}
		}

	}
	else
	{
		queue_silent_frame(conf, member);
	}
}

void queue_frame_for_speaker(
	ast_conference* conf,
	ast_conf_member* member
)
{
	struct ast_frame* qf;
	conf_frame* frame = member->speaker_frame;

	if (frame)
	{
		// try for a pre-converted frame; otherwise, convert the frame
		if ((qf = frame->converted[member->write_format]) && !member->listen_volume)
		{
			queue_outgoing_frame(member, qf, conf->delivery_time);
		}
		else
		{
			if (member->listen_volume)
			{
				ast_frame_adjust_volume(frame->fr, member->listen_volume);
			}

			// convert using the member's translation path
			qf = convert_frame(member->from_slinear, frame->fr, 0);

			queue_outgoing_frame(member, qf, conf->delivery_time);

			// free frame (the translator's copy)
			if (member->from_slinear)
				ast_frfree(qf);
		}

	}
	else
	{
		queue_silent_frame(conf, member);
	}
}



void queue_silent_frame(
	ast_conference* conf,
	ast_conf_member* member
)
{
	// get the appropriate silent frame
	struct ast_frame* qf = silent_conf_frame->converted[member->write_format];

	if (!qf)
	{
#if	ASTERISK_SRC_VERSION < 1000
		struct ast_trans_pvt* trans = ast_translator_build_path(member->chan->writeformat, AST_FORMAT_CONFERENCE);
#elif	ASTERISK_SRC_VERSION < 1100
		struct ast_trans_pvt* trans = ast_translator_build_path(&member->chan->writeformat, &ast_format_conference);
#elif	ASTERISK_SRC_VERSION < 1300
		struct ast_trans_pvt* trans = ast_translator_build_path(ast_channel_writeformat(member->chan), &ast_format_conference);
#else
		struct ast_trans_pvt* trans = ast_translator_build_path(ast_channel_writeformat(member->chan), ast_format_conference);
#endif
		if (trans)
		{
			// translate the frame
			if ((qf = ast_translate(trans, silent_conf_frame->fr, 0)))
			{
				// isolate the frame so we can keep it around after trans is free'd
				qf = ast_frisolate(qf);

				// cache the new, isolated frame
				silent_conf_frame->converted[member->write_format] = qf;
			}

			ast_translator_free_path(trans);
		}
	}

	// if it's not null queue the frame
	if (qf)
	{
		queue_outgoing_frame(member, qf, conf->delivery_time);
	}
	else
	{
#if	ASTERISK_SRC_VERSION < 1100
		ast_log(LOG_ERROR, "unable to translate outgoing silent frame, channel => %s\n", member->chan->name);
#else
		ast_log(LOG_ERROR, "unable to translate outgoing silent frame, channel => %s\n", ast_channel_name(member->chan));
#endif
	}
}



void member_process_outgoing_frames(ast_conference* conf,
				  ast_conf_member *member)
{
	// skip members that are not ready
	// skip no receive audio clients
	if (!member->ready_for_outgoing || member->norecv_audio)
	{
		return;
	}

	if (AST_LIST_EMPTY(&member->spy_list.head))
	{
		// neither a spyer nor a spyee
		if (!member->is_speaking) 
		{
			// queue listener frame
			queue_frame_for_listener(conf, member);
		}
		else
		{
			// queue speaker frame
			queue_frame_for_speaker(conf, member);
		}
	}
	else
	{
		// either a spyer or a spyee
		if (member->spyee_channel_name)
		{
			// spyer -- use member translator unless sharing frame with listeners
			if (member->speaker_frame != conf->listener_frame)
			{
				queue_frame_for_speaker(conf, member);
			}
			else
			{
				queue_frame_for_listener(conf, member);
			}
		}
		else
		{
			// spyee -- use member translator if spyee speaking or spyer whispering to spyee
			if (member->is_speaking)
			{
				queue_frame_for_speaker(conf, member);
			}
			else
			{
				ast_conf_member *entry;
				AST_LIST_TRAVERSE(&member->spy_list.head, entry, spy_list.entry)
				{
					if (member == AST_LIST_FIRST(&entry->spy_list.head) && entry->is_speaking)
					{
						queue_frame_for_speaker(conf, member);
						return;
					}
				}
				queue_frame_for_listener(conf, member);
			}
		}
	}
}

void member_process_spoken_frames(ast_conference* conf,
				 ast_conf_member *member,
				 conf_frame **spoken_frames,
				 int *listener_count,
				 int *speaker_count
	)
{
	conf_frame *cfr;

	// handle retrieved frames
	if (!(cfr  = get_incoming_frame(member)))
	{
		// clear speaking state
		member->is_speaking = 0;
	}
	else
	{
		// set speaking state
		member->is_speaking = 1;

		// add the frame to the list of spoken frames
		if (*spoken_frames)
		{
			cfr->next = *spoken_frames;
		}

		// point the list at the new frame
		*spoken_frames = cfr;

		// increment speaker count
		(*speaker_count)++;

		// decrement listener count
		(*listener_count)--;
	}
}

// get the next frame from the soundq
static struct ast_frame *get_next_soundframe(ast_conf_member *member)
{
	struct ast_frame *f;

	while (!(f = member->soundq->stream ? ast_readframe(member->soundq->stream) : NULL))
	{
		ast_conf_soundq *toboot = member->soundq;

		if (!toboot->stream)
		{
#if	ASTERISK_SRC_VERSION < 1100
			if ((toboot->stream = ast_openstream_full(member->chan, toboot->name, member->chan->language, 1)))
#else
			if ((toboot->stream = ast_openstream_full(member->chan, toboot->name, ast_channel_language(member->chan), 1)))
#endif
				continue;
		}

		if (toboot->stream)
		{
			ast_stopstream(member->chan);
		}

		if (!(member->soundq = toboot->next))
		{
			member->muted = 0;
			ast_free(toboot);
			return NULL;
		} else {
			ast_free(toboot);
			continue;
		}

	}
	return f;
}

static void *conf_sound_alloc(struct ast_channel *chan, void *params)
{
	ast_conf_member *member = (ast_conf_member *)params;

	member->ready_for_outgoing = 0;

	return params;
}

static void conf_sound_release(struct ast_channel *chan, void *data)
{
	ast_conf_member *member = (ast_conf_member *)data;

	member->ready_for_outgoing = 1;
}

static int conf_sound_generator(struct ast_channel *chan, void *data, int len, int samples)
{
	ast_conf_member *member = (ast_conf_member *)data;
	struct ast_frame *f = get_next_soundframe(member);

	if (!f)
		return -1;

	ast_write(chan, f);
	ast_frfree(f);

	return 0;
}

struct ast_generator play_sound_generator = {
	.alloc = conf_sound_alloc,
	.release = conf_sound_release,
	.generate = conf_sound_generator,
};
