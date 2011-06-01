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
struct ast_conf_member *mbrblocklist = NULL;
AST_MUTEX_DEFINE_STATIC(mbrblocklist_lock);
#endif

// process an incoming frame.  Returns 0 normally, 1 if hangup was received.
static int process_incoming(struct ast_conf_member *member, struct ast_conference *conf, struct ast_frame *f)
{
#if ( SILDET == 2 )
	int silent_frame = 0;
#endif

	// In Asterisk 1.4 AST_FRAME_DTMF is equivalent to AST_FRAME_DTMF_END
	if (f->frametype == AST_FRAME_DTMF)
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
				member->chan->uniqueid,
				member->chan->name,
#if	ASTERISK == 14 || ASTERISK == 16
				member->chan->cid.cid_num ? member->chan->cid.cid_num : "unknown",
				member->chan->cid.cid_name ? member->chan->cid.cid_name : "unknown",
				f->subclass,
#else
				member->chan->caller.id.number.str ? member->chan->caller.id.number.str : "unknown",
				member->chan->caller.id.name.str ? member->chan->caller.id.name.str: "unknown",
				f->subclass.integer,
#endif
				conf->membercount,
				member->flags,
				member->mute_audio
				) ;

		}
#ifdef	DTMF
		if (!member->mute_audio &&
			!member->dtmf_relay)
		{
			// relay this to the listening channels
			queue_incoming_dtmf_frame( member, f );
		}
#endif
	}
#ifdef	DTMF
	else if (f->frametype == AST_FRAME_DTMF_BEGIN)
	{
		if (!member->mute_audio &&
			!member->dtmf_relay)
		{
			// relay this to the listening channels
			queue_incoming_dtmf_frame( member, f );
		}
	}
#endif
	if ((f->frametype == AST_FRAME_VOICE
			&& (member->mute_audio == 1
				|| member->muted == 1
				|| conf->membercount == 1)
			)
		)
	{
		// this is a listen-only user, ignore the frame
		ast_frfree( f ) ;
	}
	else if ( f->frametype == AST_FRAME_VOICE )
	{
		// accounting: count the incoming frame
		member->frames_in++ ;

#if ( SILDET == 2 )
		// reset silence detection flag
		silent_frame = 0 ;
		//
		// make sure we have a valid dsp and frame type
		//
		if (
			member->dsp
#ifndef	AC_USE_G722
#if	ASTERISK == 14 || ASTERISK == 16
			&& f->subclass == AST_FORMAT_SLINEAR
#else
			&& f->subclass.integer == AST_FORMAT_SLINEAR
#endif
#else
#if	ASTERISK == 14 || ASTERISK == 16
			&& f->subclass == AST_FORMAT_SLINEAR16
#else
			&& f->subclass.integer == AST_FORMAT_SLINEAR16
#endif
#endif
			&& f->datalen == AST_CONF_FRAME_DATA_SIZE
			)
		{
			// send the frame to the preprocessor
			int spx_ret;
#if	ASTERISK == 14
			spx_ret = speex_preprocess( member->dsp, f->data, NULL );
#else
			spx_ret = speex_preprocess( member->dsp, f->data.ptr, NULL );
#endif

			if ( !spx_ret )
			{
				//
				// we ignore the preprocessor's outcome if we've seen voice frames
				// in within the last AST_CONF_SKIP_SPEEX_PREPROCESS frames
				//
				if ( member->ignore_speex_count > 0 )
				{
					// skip speex_preprocess(), and decrement counter
					if (!--member->ignore_speex_count ) {
						manager_event(
							EVENT_FLAG_CONF,
							"ConferenceState",
							"Channel: %s\r\n"
							"Flags: %s\r\n"
							"State: %s\r\n",
							member->chan->name,
							member->flags,
							"silent"
						) ;
					}
				}
				else
				{
					// set silent_frame flag
					silent_frame = 1 ;
				}
			}
			else
			{
				if (!member->ignore_speex_count) {
					manager_event(
						EVENT_FLAG_CONF,
						"ConferenceState",
						"Channel: %s\r\n"
						"Flags: %s\r\n"
						"State: %s\r\n",
						member->chan->name,
						member->flags,
						"speaking"
					) ;
				}
				// voice detected, reset skip count
				member->ignore_speex_count = AST_CONF_SKIP_SPEEX_PREPROCESS ;
			}
		}
		if ( !silent_frame )
#endif
			queue_incoming_frame( member, f );

		// free the original frame
		ast_frfree( f ) ;
	}
	else if (
		f->frametype == AST_FRAME_CONTROL
#if	ASTERISK == 14 || ASTERISK == 16
		&& f->subclass == AST_CONTROL_HANGUP
#else
		&& f->subclass.integer == AST_CONTROL_HANGUP
#endif
		)
	{
		// hangup received

		// free the frame
		ast_frfree( f ) ;

		// break out of the while ( 42 == 42 )
		return 1;
	}
	else {
		// undesirables
		ast_frfree( f ) ;
	}

	return 0;
}

// get the next frame from the soundq;  must be called with member locked.
static struct ast_frame *get_next_soundframe(struct ast_conf_member *member, struct ast_frame
    *exampleframe) {
    struct ast_frame *f;

again:
	ast_mutex_unlock(&member->lock);
again2:
    f=(member->soundq->stream && !member->soundq->stopped ? ast_readframe(member->soundq->stream) : NULL);

    if(!f) { // we're done with this sound; remove it from the queue, and try again
	struct ast_conf_soundq *toboot = member->soundq;

	if (!toboot->stopped && !toboot->stream)
	{
		toboot->stream = ast_openstream(member->chan, toboot->name, member->chan->language);
		//ast_log( LOG_WARNING, "trying to play sound: name = %s, stream = %p\n", toboot->name, toboot->stream);
		if (toboot->stream)
		{
			member->chan->stream = NULL;
			goto again2;
		}
		//ast_log( LOG_WARNING, "trying to play sound, %s not found!?", toboot->name);
	}

	if (toboot->stream) {
		ast_closestream(toboot->stream);
		//ast_log( LOG_WARNING, "finished playing a sound: name = %s, stream = %p\n", toboot->name, toboot->stream);
		// notify applications via mgr interface that this sound has been played
		manager_event(
			EVENT_FLAG_CONF,
			"ConferenceSoundComplete",
			"Channel: %s\r\n"
			"Sound: %s\r\n",
			member->chan->name,
			toboot->name
		);
	}

	ast_mutex_lock( &member->lock ) ;
	member->soundq = toboot->next;

	free(toboot);
	if(member->soundq) goto again;

	member->muted = 0;

	ast_mutex_unlock(&member->lock);

	// if we get here, we've gotten to the end of the queue; reset write format
	if ( ast_set_write_format( member->chan, member->write_format ) < 0 )
	{
		ast_log( LOG_ERROR, "unable to set write format to %d\n",
		    member->write_format ) ;
	}
    } else {
	// copy delivery from exampleframe
	f->delivery = exampleframe->delivery;
    }

    return f;
}


// process outgoing frames for the channel, playing either normal conference audio,
// or requested sounds
static int process_outgoing(struct ast_conf_member *member)
{
	conf_frame* cf ; // frame read from the output queue
	struct ast_frame *f;

	for(;;)
	{
		// acquire member mutex and grab a frame.
		cf = get_outgoing_frame( member ) ;

                // if there's no frames exit the loop.
		if ( !cf )
		{
			break;
		}


		struct ast_frame *realframe = f = cf->fr;

		// if we're playing sounds, we can just replace the frame with the
		// next sound frame, and send it instead
		ast_mutex_lock( &member->lock ) ;
		if ( member->soundq )
		{
			f = get_next_soundframe(member, f);
			if ( !f )
			{
				// if we didn't get anything, just revert to "normal"
				f = realframe;
			}
		} else {
			if (member->moh_flag) {
				member->ready_for_outgoing = 0;
				delete_conf_frame( cf ) ;
				ast_moh_start(member->chan, NULL, NULL);
				ast_mutex_unlock(&member->lock);
				return 0;
			}
			ast_mutex_unlock(&member->lock);
		}

		// send the voice frame
		if ( ast_write( member->chan, f ) )
		{
			// accounting: count dropped outgoing frames
			member->frames_out_dropped++ ;
		}

		// clean up frame
		delete_conf_frame( cf ) ;
		
		// free sound frame
		if ( f != realframe )
			ast_frfree(f) ;

		if ( member->chan->_softhangup )
			return 1;

	}
#ifdef	DTMF
        // Do the same for dtmf, suck it dry
	for(;;)
	{
		// acquire member mutex and grab a frame.
		cf = get_outgoing_dtmf_frame( member ) ;

		// if there's no frames exit the loop.
		if(!cf) break;

		// send the dtmf frame
		if ( ast_write( member->chan, cf->fr ) )
		{
			// accounting: count dropped outgoing frames
			member->dtmf_frames_out_dropped++ ;
		}

		// clean up frame
		delete_conf_frame( cf ) ;

		if ( member->chan->_softhangup )
			return 1;

	}
#endif

	return 0;
}

//
// main member thread function
//

#if	ASTERISK == 14
int member_exec( struct ast_channel* chan, void* data )
#else
int member_exec( struct ast_channel* chan, const char* data )
#endif
{
//	struct timeval start, end ;
//	start = ast_tvnow();

	struct ast_conference *conf ;
	char conf_name[CONF_NAME_LEN + 1]  = { 0 };
	struct ast_conf_member *member ;

	struct ast_frame *f ; // frame received from ast_read()

	int left = 0 ;
	int res;

	//
	// If the call has not yet been answered, answer the call
	// Note: asterisk apps seem to check _state, but it seems like it's safe
	// to just call ast_answer.  It will just do nothing if it is up.
	// it will also return -1 if the channel is a zombie, or has hung up.
	//

	res = ast_answer( chan ) ;
	if ( res )
	{
		ast_log( LOG_ERROR, "unable to answer call\n" ) ;
		return -1 ;
	}

	//
	// create a new member for the conference
 	//

	member = create_member( chan, (const char*)( data ), conf_name ) ; // flags, name

	// unable to create member, return an error
	if ( member == NULL )
	{
		ast_log( LOG_ERROR, "unable to create member\n" ) ;
		return -1 ;
	}

	//
	// setup asterisk read/write formats
	//

	if ( ast_set_read_format( chan, member->read_format ) < 0 )
	{
		ast_log( LOG_ERROR, "unable to set read format to signed linear\n" ) ;
		delete_member( member ) ;
		return -1 ;
	}

	if ( ast_set_write_format( chan, member->write_format ) < 0 ) // AST_FORMAT_SLINEAR, chan->nativeformats
	{
		ast_log( LOG_ERROR, "unable to set write format to signed linear\n" ) ;
		delete_member( member ) ;
		return -1 ;
	}

	//
	// setup a conference for the new member
	//

	char max_users_flag = 0 ;
	conf = join_conference( member, conf_name, &max_users_flag ) ;

	if ( !conf )
	{
		ast_log( LOG_NOTICE, "unable to setup member conference %s: max_users_flag is %d\n", conf_name, max_users_flag ) ;
		delete_member( member) ;
		return (max_users_flag ? 0 : -1 ) ;
	}

	// add member to channel table
	member->bucket = &(channel_table[hash(member->chan->name) % CHANNEL_TABLE_SIZE]);

	AST_LIST_LOCK (member->bucket ) ;
	AST_LIST_INSERT_HEAD (member->bucket, member, hash_entry) ;
	AST_LIST_UNLOCK (member->bucket ) ;

	manager_event(
		EVENT_FLAG_CONF,
		"ConferenceJoin",
		"ConferenceName: %s\r\n"
		"Type: %s\r\n"
		"UniqueID: %s\r\n"
		"Member: %d\r\n"
		"Flags: %s\r\n"
		"Channel: %s\r\n"
		"CallerID: %s\r\n"
		"CallerIDName: %s\r\n"
		"Moderators: %d\r\n"
		"Count: %d\r\n",
		conf->name,
		member->type,
		member->chan->uniqueid,
		member->id,
		member->flags,
		member->chan->name,
#if	ASTERISK == 14 || ASTERISK == 16
		member->chan->cid.cid_num ? member->chan->cid.cid_num : "unknown",
		member->chan->cid.cid_name ? member->chan->cid.cid_name : "unknown",
#else
		member->chan->caller.id.number.str ? member->chan->caller.id.number.str : "unknown",
		member->chan->caller.id.name.str ? member->chan->caller.id.name.str: "unknown",
#endif
		conf->stats.moderators,
		conf->membercount
	) ;

	// if spyer setup failed, set variable and exit conference
	if ( member->spyee_channel_name && !member->spyer )
	{
		remove_member( member, conf, conf_name ) ;
		pbx_builtin_setvar_helper(member->chan, "KONFERENCE", "SPYFAILED" );
		return 0 ;
	}

	//
	// process loop for new member ( this runs in it's own thread )
	//

	// tell conference_exec we're ready for frames
	member->ready_for_outgoing = 1 ;
	while ( 42 == 42 )
	{
		// make sure we have a channel to process
		if ( !chan )
		{
			ast_log( LOG_NOTICE, "member channel has closed\n" ) ;
			break ;
		}

		//-----------------//
		// INCOMING FRAMES //
		//-----------------//

		// wait for an event on this channel
		left = ast_waitfor( chan, AST_CONF_WAITFOR_LATENCY ) ;

		if ( left < 0 )
		{
			// an error occured
			ast_log(
				LOG_NOTICE,
				"an error occured waiting for a frame, channel => %s, error => %d\n",
				chan->name, left
			) ;
			break; // out of the 42==42
		}
		else if ( left == 0 )
		{
			// no frame has arrived yet
			// ast_log( LOG_NOTICE, "no frame available from channel, channel => %s\n", chan->name ) ;
		}
		else if ( left > 0 )
		{
			// a frame has come in before the latency timeout
			// was reached, so we process the frame

			f = ast_read( chan ) ;

			if ( !f )
			{
				// They probably want to hangup...
				break ;
			}

			// actually process the frame: break if we got hangup.
			if(process_incoming(member, conf, f)) break;

		}

		if (conf->kick_flag || member->kick_flag) {
			pbx_builtin_setvar_helper(member->chan, "KONFERENCE", "KICKED" );
			break;
		}
		
		if ( member->moh_stop ) {
			ast_moh_stop(member->chan);
			member->moh_stop = 0;
		}

		//-----------------//
		// OUTGOING FRAMES //
		//-----------------//

		if ( !process_outgoing(member) )
			// back to process incoming frames
			continue ;
		else
			// they probably hungup...
			break ;
	}

	//
	// clean up
	//

	remove_member( member, conf, conf_name ) ;
	return 0 ;
}

//
// manange member functions
//

struct ast_conf_member* create_member( struct ast_channel *chan, const char* data, char* conf_name )
{
	struct ast_conf_member *member;
#ifdef	CACHE_CONTROL_BLOCKS
	if ( mbrblocklist )
	{
		// get member control block from the free list
		ast_mutex_lock ( &mbrblocklist_lock ) ;
		member = mbrblocklist;
		mbrblocklist = mbrblocklist->next;
		ast_mutex_unlock ( &mbrblocklist_lock ) ;
		memset(member,0,sizeof(struct ast_conf_member));
	}
	else
	{
#endif
		// allocate new member control block
		if ( !(member = ast_calloc( 1,  sizeof( struct ast_conf_member ) )) )
		{
			ast_log( LOG_ERROR, "unable to calloc ast_conf_member\n" ) ;
			return NULL ;
		}
#ifdef	CACHE_CONTROL_BLOCKS
	}
#endif

	// initialize mutex
	ast_mutex_init( &member->lock ) ;

	// initialize cv
	ast_cond_init( &member->delete_var, NULL ) ;

	// Default values for parameters that can get overwritten by dialplan arguments
#if ( SILDET == 2 )
	member->vad_prob_start = AST_CONF_PROB_START;
	member->vad_prob_continue = AST_CONF_PROB_CONTINUE;
#endif
	member->max_users = AST_CONF_MAX_USERS;

	//
	// initialize member with passed data values
	//
	char argstr[256] = { 0 };

	// copy the passed data
	strncpy( argstr, data, sizeof(argstr) - 1 ) ;

	// point to the copied data
	char *stringp = argstr;

	// parse the id
	char *token;
	if ( ( token = strsep( &stringp, argument_delimiter ) ) )
	{
		strncpy( conf_name, token, CONF_NAME_LEN ) ;
	}
	else
	{
		ast_log( LOG_ERROR, "create_member unable to parse member data: channel name = %s, data = %s\n", chan->name, data ) ;
		free( member ) ;
		return NULL ;
	}

	// parse the flags
	if ( ( token = strsep( &stringp, argument_delimiter ) ) )
	{
		strncpy( member->flags, token, MEMBER_FLAGS_LEN ) ;
	}

	while ( (token = strsep(&stringp, argument_delimiter )) )
	{
#if ( SILDET == 2 )
		static const char arg_vad_prob_start[] = "vad_prob_start";
		static const char arg_vad_prob_continue[] = "vad_prob_continue";
#endif
		static const char arg_max_users[] = "max_users";
		static const char arg_conf_type[] = "type";
		static const char arg_chanspy[] = "spy";

		char *value = token;
		const char *key = strsep(&value, "=");
		
		if ( !key || !value )
		{
			ast_log(LOG_WARNING, "Incorrect argument %s\n", token);
			continue;
		}

		if ( !strncasecmp(key, arg_max_users, sizeof(arg_max_users) - 1) )
		{
			member->max_users = strtol(value, (char **)NULL, 10);
		} else if ( !strncasecmp(key, arg_conf_type, sizeof(arg_conf_type) - 1) )
		{
			strncpy( member->type, value, MEMBER_TYPE_LEN ) ;
		} else if ( !strncasecmp(key, arg_chanspy, sizeof(arg_chanspy) - 1) )
		{
			member->spyee_channel_name = malloc( strlen( value ) + 1 ) ;
			strcpy( member->spyee_channel_name, value ) ;
#if ( SILDET == 2 )
		} else if ( !strncasecmp(key, arg_vad_prob_start, sizeof(arg_vad_prob_start) - 1) )
		{
			member->vad_prob_start = strtof(value, (char **)NULL);
		} else if ( !strncasecmp(key, arg_vad_prob_continue, sizeof(arg_vad_prob_continue) - 1) )
		{
			member->vad_prob_continue = strtof(value, (char **)NULL);
#endif
		} else
		{
			ast_log(LOG_WARNING, "unknown parameter %s with value %s\n", key, value) ;
		}

	}

	//
	// initialize member with default values
	//

	// keep pointer to member's channel
	member->chan = chan ;

	// set default if no type parameter
	if ( !(*(member->type)) ) {
		strcpy( member->type, AST_CONF_TYPE_DEFAULT ) ;
	}

	// start of day video ids
	member->id = -1;

	// ( not currently used )
	// member->samplesperframe = AST_CONF_BLOCK_SAMPLES ;

	// record start time
	// init dropped frame timestamps
	// init state change timestamp
	member->time_entered =
		member->last_in_dropped =
		member->last_out_dropped =
		ast_tvnow();
	//
	// parse passed flags
	//

	// temp pointer to flags string
	char* flags = member->flags ;

	int i;

	for ( i = 0 ; i < strlen( flags ) ; ++i )
	{
		{
			// allowed flags are C, c, L, l, V, D, A, C, X, R, T, t, M, S, z, o, F, H
			// mute/no_recv options
			switch ( flags[i] )
			{
			case 'L':
				member->mute_audio = 1;
				break ;
			case 'l':
				member->norecv_audio = 1;
				break;
#if ( SILDET == 2 )
				// speex preprocessing options
			case 'V':
				member->vad_flag = 1 ;
				break ;
			case 'D':
				member->denoise_flag = 1 ;
				break ;
			case 'A':
				member->agc_flag = 1 ;
				break ;
#endif
				// dtmf/moderator/video switching options
			case 'R':
				member->dtmf_relay = 1;
				break;
			case 'M':
				member->ismoderator = 1;
				break;
			case 'x':
				member->kick_conferees = 1;
				break;
#if ( SILDET == 2 )
				//Telephone connection
			case 'a':
				member->vad_flag = 1 ;
#endif
			case 'T':
				member->via_telephone = 1;
				break;
			case 'H':
				member->hold_flag = 1;
				break;

			default:
				break ;
			}
		}
	}

#if ( SILDET == 2 )
	//
	// configure silence detection and preprocessing
	// if the user is coming in via the telephone,
	// and is not listen-only
	//
	if ( member->via_telephone )
	{
		// create a speex preprocessor
		member->dsp = speex_preprocess_state_init( AST_CONF_BLOCK_SAMPLES, AST_CONF_SAMPLE_RATE ) ;

		if ( !member->dsp )
		{
			ast_log( LOG_WARNING, "unable to initialize member dsp, channel => %s\n", chan->name ) ;
		}
		else
		{
			// set speex preprocessor options
			speex_preprocess_ctl( member->dsp, SPEEX_PREPROCESS_SET_VAD, &(member->vad_flag) ) ;
			speex_preprocess_ctl( member->dsp, SPEEX_PREPROCESS_SET_DENOISE, &(member->denoise_flag) ) ;
			speex_preprocess_ctl( member->dsp, SPEEX_PREPROCESS_SET_AGC, &(member->agc_flag) ) ;

			speex_preprocess_ctl( member->dsp, SPEEX_PREPROCESS_SET_PROB_START, &member->vad_prob_start ) ;
			speex_preprocess_ctl( member->dsp, SPEEX_PREPROCESS_SET_PROB_CONTINUE, &member->vad_prob_continue ) ;
		}
	}
#endif
	//
	// read, write, and translation options
	//

	// set member's audio formats, taking dsp preprocessing into account
	// ( chan->nativeformats, AST_FORMAT_SLINEAR, AST_FORMAT_ULAW, AST_FORMAT_GSM )
#if ( SILDET == 2 )
#ifndef	AC_USE_G722
	member->read_format = ( !member->dsp ? chan->nativeformats : AST_FORMAT_SLINEAR ) ;
#else
	member->read_format = ( !member->dsp ? chan->nativeformats : AST_FORMAT_SLINEAR16 ) ;
#endif
#else
	member->read_format = chan->nativeformats ;
#endif
	member->write_format = chan->nativeformats;
	// 1.2 or 1.3+
#ifdef AST_FORMAT_AUDIO_MASK

	member->read_format &= AST_FORMAT_AUDIO_MASK;
	member->write_format &= AST_FORMAT_AUDIO_MASK;
#endif

	//translation paths ( ast_translator_build_path() returns null if formats match )
#ifndef	AC_USE_G722
	member->to_slinear = ast_translator_build_path( AST_FORMAT_SLINEAR, member->read_format ) ;
	member->from_slinear = ast_translator_build_path( member->write_format, AST_FORMAT_SLINEAR ) ;
#else
	member->to_slinear = ast_translator_build_path( AST_FORMAT_SLINEAR16, member->read_format ) ;
	member->from_slinear = ast_translator_build_path( member->write_format, AST_FORMAT_SLINEAR16 ) ;
#endif

	// index for converted_frames array
	switch ( member->write_format )
	{
#ifndef	AC_USE_G722
		case AST_FORMAT_SLINEAR:
#else
		case AST_FORMAT_SLINEAR16:
#endif
			member->write_format_index = AC_SLINEAR_INDEX ;
			break ;

		case AST_FORMAT_ULAW:
			member->write_format_index = AC_ULAW_INDEX ;
			break ;

	        case AST_FORMAT_ALAW:
			member->write_format_index = AC_ALAW_INDEX ;
			break ;

		case AST_FORMAT_GSM:
			member->write_format_index = AC_GSM_INDEX ;
			break ;
#ifdef	AC_USE_SPEEX
		case AST_FORMAT_SPEEX:
			member->write_format_index = AC_SPEEX_INDEX;
			break;
#endif
#ifdef AC_USE_G729A
		case AST_FORMAT_G729A:
			member->write_format_index = AC_G729A_INDEX;
			break;
#endif
#ifdef AC_USE_G722
		case AST_FORMAT_G722:
			member->write_format_index = AC_G722_INDEX;
			break;
#endif
		default:
			break;
	}

	// index for converted_frames array
	switch ( member->read_format )
	{
#ifndef	AC_USE_G722
		case AST_FORMAT_SLINEAR:
#else
		case AST_FORMAT_SLINEAR16:
#endif
			member->read_format_index = AC_SLINEAR_INDEX ;
			break ;

		case AST_FORMAT_ULAW:
			member->read_format_index = AC_ULAW_INDEX ;
			break ;

		case AST_FORMAT_ALAW:
			member->read_format_index = AC_ALAW_INDEX ;
			break ;

		case AST_FORMAT_GSM:
			member->read_format_index = AC_GSM_INDEX ;
			break ;
#ifdef	AC_USE_SPEEX
		case AST_FORMAT_SPEEX:
			member->read_format_index = AC_SPEEX_INDEX;
			break;
#endif
#ifdef AC_USE_G729A
		case AST_FORMAT_G729A:
			member->read_format_index = AC_G729A_INDEX;
			break;
#endif
#ifdef AC_USE_G722
		case AST_FORMAT_G722:
			member->read_format_index = AC_G722_INDEX;
			break;
#endif
		default:
			break;
	}
	//
	// finish up
	//

	return member ;
}

struct ast_conf_member* delete_member( struct ast_conf_member* member )
{
	ast_mutex_lock ( &member->lock ) ;

	member->delete_flag = 1 ;
	if ( member->use_count )
		ast_cond_wait ( &member->delete_var, &member->lock ) ;

	ast_mutex_unlock ( &member->lock ) ;

	// destroy member mutex and condition variable
	ast_mutex_destroy( &member->lock ) ;
	ast_cond_destroy( &member->delete_var ) ;

	//
	// delete the members frames
	//

	conf_frame* cf ;

	// incoming frames
	cf = member->inFrames ;

	while ( cf )
	{
		cf = delete_conf_frame( cf ) ;
	}
	// outgoing frames
	cf = member->outFrames ;

	while ( cf )
	{
		cf = delete_conf_frame( cf ) ;
	}
#ifdef AST_CONF_CACHE_LAST_FRAME
	if ( member->inFramesLast )
	{
		delete_conf_frame( member->inFramesLast ) ;
	}
#endif
#if ( SILDET == 2 )
	if ( member->dsp )
	{
		speex_preprocess_state_destroy( member->dsp ) ;
	}
#endif

	// free the mixing translators
	ast_translator_free_path( member->to_slinear ) ;
	ast_translator_free_path( member->from_slinear ) ;

	// get a pointer to the next
	// member so we can return it
	struct ast_conf_member* nm = member->next ;

	// free the member's copy of the spyee channel name
	free(member->spyee_channel_name);

	// clear all sounds
	struct ast_conf_soundq *sound = member->soundq;
	struct ast_conf_soundq *next;

	while ( sound )
	{
		next = sound->next;
		if ( sound->stream )
			ast_closestream( sound->stream );
		free( sound );
		sound = next;
	}

#ifdef	CACHE_CONTROL_BLOCKS
	// put the member control block on the free list
	ast_mutex_lock ( &mbrblocklist_lock ) ;
	member->next = mbrblocklist;
	mbrblocklist = member;
	ast_mutex_unlock ( &mbrblocklist_lock ) ;
#else
	free( member ) ;
#endif
	return nm ;
}

//
// incoming frame functions
//
#ifdef	DTMF
conf_frame* get_incoming_dtmf_frame( struct ast_conf_member *member )
{
  	if ( !member )
	{
		ast_log( LOG_WARNING, "unable to get frame from null member\n" ) ;
		return NULL ;
	}

	ast_mutex_lock(&member->lock);

	if ( !member->inDTMFFramesCount )
	{
		ast_mutex_unlock(&member->lock);
		return NULL ;
	}

	//
	// return the next frame in the queue
	//

	conf_frame* cfr = NULL ;

	// get first frame in line
	cfr = member->inDTMFFramesTail ;

	// if it's the only frame, reset the queue,
	// else, move the second frame to the front
	if ( member->inDTMFFramesTail == member->inDTMFFrames )
	{
		member->inDTMFFramesTail = NULL ;
		member->inDTMFFrames = NULL ;
	}
	else
	{
		// move the pointer to the next frame
		member->inDTMFFramesTail = member->inDTMFFramesTail->prev ;

		// reset it's 'next' pointer
		if ( member->inDTMFFramesTail )
			member->inDTMFFramesTail->next = NULL ;
	}

	// separate the conf frame from the list
	cfr->next = NULL ;
	cfr->prev = NULL ;

	// decriment frame count
	member->inDTMFFramesCount-- ;

	ast_mutex_unlock(&member->lock);
	return cfr ;

}
#endif

conf_frame* get_incoming_frame( struct ast_conf_member *member )
{
#ifdef AST_CONF_CACHE_LAST_FRAME
	conf_frame *cf_result;
#endif
	ast_mutex_lock(&member->lock);

 	//
 	// repeat last frame a couple times to smooth transition
 	//

#ifdef AST_CONF_CACHE_LAST_FRAME
	if ( !member->inFramesCount )
	{
		// nothing to do if there's no cached frame
		if ( !member->inFramesLast ) {
			ast_mutex_unlock(&member->lock);
			return NULL ;
		}

		// turn off 'okay to cache' flag
		member->okayToCacheLast = 0 ;

		if ( member->inFramesRepeatLast >= AST_CONF_CACHE_LAST_FRAME )
		{
			// already used this frame AST_CONF_CACHE_LAST_FRAME times

			// reset repeat count
			member->inFramesRepeatLast = 0 ;

			// clear the cached frame
			delete_conf_frame( member->inFramesLast ) ;
			member->inFramesLast = NULL ;

			// return null
			ast_mutex_unlock(&member->lock);
			return NULL ;
		}
		else
		{
			// increment counter
			member->inFramesRepeatLast++ ;

			// return a copy of the cached frame
			cf_result = copy_conf_frame( member->inFramesLast ) ;
			ast_mutex_unlock(&member->lock);
			return cf_result;
		}
	}
	else if ( !member->okayToCacheLast && member->inFramesCount >= 3 )
	{
		// turn on 'okay to cache' flag
		member->okayToCacheLast = 1 ;
	}
#else
	if ( !member->inFramesCount ) {
		ast_mutex_unlock(&member->lock);
		return NULL ;
	}
#endif // AST_CONF_CACHE_LAST_FRAME

	//
	// return the next frame in the queue
	//

	conf_frame* cfr = NULL ;

	// get first frame in line
	cfr = member->inFramesTail ;

	// if it's the only frame, reset the queue,
	// else, move the second frame to the front
	if ( member->inFramesTail == member->inFrames )
	{
		member->inFramesTail = NULL ;
		member->inFrames = NULL ;
	}
	else
	{
		// move the pointer to the next frame
		member->inFramesTail = member->inFramesTail->prev ;

		// reset it's 'next' pointer
		if ( member->inFramesTail )
			member->inFramesTail->next = NULL ;
	}

	// separate the conf frame from the list
	cfr->next = NULL ;
	cfr->prev = NULL ;

	// decriment frame count
	member->inFramesCount-- ;

#ifdef AST_CONF_CACHE_LAST_FRAME
	// copy frame if queue is now empty
	if (
		!member->inFramesCount
		&& member->okayToCacheLast == 1
	)
	{
		// reset repeat count
		member->inFramesRepeatLast = 0 ;

		// clear cached frame
		if ( member->inFramesLast )
		{
			delete_conf_frame( member->inFramesLast ) ;
			member->inFramesLast = NULL ;
		}

		// cache new frame
		member->inFramesLast = copy_conf_frame( cfr ) ;
	}
#endif // AST_CONF_CACHE_LAST_FRAME

	ast_mutex_unlock(&member->lock);
	return cfr ;
}
#ifdef	DTMF
void queue_incoming_dtmf_frame( struct ast_conf_member* member, const struct ast_frame* fr )
{
	ast_mutex_lock(&member->lock);

	// We have to drop if the queue is full!
	if ( member->inDTMFFramesCount >= AST_CONF_MAX_DTMF_QUEUE )
	{
		ast_mutex_unlock(&member->lock);
		return ;
	}

	//
	// create new conf frame from passed data frame
	//

	// ( member->inFrames may be null at this point )
	conf_frame* cfr = create_conf_frame( member, member->inDTMFFrames, fr ) ;

	if ( !cfr )
	{
		ast_log( LOG_ERROR, "unable to malloc conf_frame\n" ) ;
		ast_mutex_unlock(&member->lock);
		return ;
	}

	// copy frame data pointer to conf frame
	// cfr->fr = fr ;

	//
	// add new frame to speaking members incoming frame queue
	// ( i.e. save this frame data, so we can distribute it in conference_exec later )
	//

	if ( !member->inDTMFFrames )
	{
		// this is the first frame in the buffer
		member->inDTMFFramesTail = cfr ;
		member->inDTMFFrames = cfr ;
	}
	else
	{
		// put the new frame at the head of the list
		member->inDTMFFrames = cfr ;
	}

	// increment member frame count
	member->inDTMFFramesCount++ ;

	ast_mutex_unlock(&member->lock);
}

#endif
void queue_incoming_frame( struct ast_conf_member* member, struct ast_frame* fr )
{
	ast_mutex_lock(&member->lock);

	if ( member->inFramesCount > member->inFramesNeeded )
	{
		if ( member->inFramesCount > AST_CONF_QUEUE_DROP_THRESHOLD )
		{
			struct timeval curr = ast_tvnow();

			// time since last dropped frame
			long diff = ast_tvdiff_ms(curr, member->last_in_dropped);

			// number of milliseconds which must pass between frame drops
			// ( 15 frames => -100ms, 10 frames => 400ms, 5 frames => 900ms, 0 frames => 1400ms, etc. )
			long time_limit = 1000 - ( ( member->inFramesCount - AST_CONF_QUEUE_DROP_THRESHOLD ) * 100 ) ;

			if ( diff >= time_limit )
			{
				// count sequential drops
				member->sequential_drops++ ;

				// accounting: count dropped incoming frames
				member->frames_in_dropped++ ;

				// reset frames since dropped
				member->since_dropped = 0 ;

				// delete the frame
				delete_conf_frame( get_incoming_frame( member ) ) ;

				member->last_in_dropped = ast_tvnow();
			}
		}
	}

	//
	// if we have to drop frames, we'll drop new frames
	// because it's easier ( and doesn't matter much anyway ).
	//

	if ( member->inFramesCount >= AST_CONF_MAX_QUEUE )
	{
		// count sequential drops
		member->sequential_drops++ ;

		// accounting: count dropped incoming frames
		member->frames_in_dropped++ ;

		// reset frames since dropped
		member->since_dropped = 0 ;

		ast_mutex_unlock(&member->lock);
		return ;
	}

	// reset sequential drops
	member->sequential_drops = 0 ;

	// increment frames since dropped
	member->since_dropped++ ;

	//
	// create new conf frame from passed data frame
	//
		conf_frame* cfr = create_conf_frame( member, member->inFrames, fr ) ;
		if ( !cfr)
		{
			ast_log( LOG_ERROR, "unable to malloc conf_frame\n" ) ;
			ast_mutex_unlock(&member->lock);
			return ;
		}

		//
		// add new frame to speaking members incoming frame queue
		// ( i.e. save this frame data, so we can distribute it in conference_exec later )
		//

		if ( !member->inFrames) {
			member->inFramesTail = cfr ;
		}
		member->inFrames = cfr ;
		member->inFramesCount++ ;
	ast_mutex_unlock(&member->lock);
}

//
// outgoing frame functions
//

conf_frame* get_outgoing_frame( struct ast_conf_member *member )
{
	conf_frame* cfr ;

	ast_mutex_lock(&member->lock);

	if ( member->outFramesCount > AST_CONF_MIN_QUEUE )
	{
		cfr = member->outFramesTail ;

		// if it's the only frame, reset the queu,
		// else, move the second frame to the front
		if ( member->outFramesTail == member->outFrames )
		{
			member->outFrames = NULL ;
			member->outFramesTail = NULL ;
		}
		else
		{
			// move the pointer to the next frame
			member->outFramesTail = member->outFramesTail->prev ;

			// reset it's 'next' pointer
			if ( member->outFramesTail )
				member->outFramesTail->next = NULL ;
		}

		// separate the conf frame from the list
		cfr->next = NULL ;
		cfr->prev = NULL ;

		// decriment frame count
		member->outFramesCount-- ;
		ast_mutex_unlock(&member->lock);
		return cfr ;
	}
	ast_mutex_unlock(&member->lock);
	return NULL ;
}

void queue_outgoing_frame( struct ast_conf_member* member, const struct ast_frame* fr, struct timeval delivery )
{
	// accounting: count the number of outgoing frames for this member
	member->frames_out++ ;

	//
	// we have to drop frames, so we'll drop new frames
	// because it's easier ( and doesn't matter much anyway ).
	//
	if ( member->outFramesCount >= AST_CONF_MAX_QUEUE )
	{
		// accounting: count dropped outgoing frames
		member->frames_out_dropped++ ;
		return ;
	}

	//
	// create new conf frame from passed data frame
	//

	conf_frame* cfr = create_conf_frame( member, member->outFrames, fr ) ;

	if ( !cfr )
	{
		ast_log( LOG_ERROR, "unable to create new conf frame\n" ) ;

		// accounting: count dropped outgoing frames
		member->frames_out_dropped++ ;
		return ;
	}

	// set delivery timestamp
	cfr->fr->delivery = delivery ;

	//
	// add new frame to speaking members incoming frame queue
	// ( i.e. save this frame data, so we can distribute it in conference_exec later )
	//

	if ( !member->outFrames ) {
		member->outFramesTail = cfr ;
	}
	member->outFrames = cfr ;
	member->outFramesCount++ ;
}

//
// outgoing frame functions
//
#ifdef	DTMF
conf_frame* get_outgoing_dtmf_frame( struct ast_conf_member *member )
{
	conf_frame* cfr ;

	ast_mutex_lock(&member->lock);

	if ( member->outDTMFFramesCount > AST_CONF_MIN_QUEUE )
	{
		cfr = member->outDTMFFramesTail ;

		// if it's the only frame, reset the queu,
		// else, move the second frame to the front
		if ( member->outDTMFFramesTail == member->outDTMFFrames )
		{
			member->outDTMFFrames = NULL ;
			member->outDTMFFramesTail = NULL ;
		}
		else
		{
			// move the pointer to the next frame
			member->outDTMFFramesTail = member->outDTMFFramesTail->prev ;

			// reset it's 'next' pointer
			if ( member->outDTMFFramesTail )
				member->outDTMFFramesTail->next = NULL ;
		}

		// separate the conf frame from the list
		cfr->next = NULL ;
		cfr->prev = NULL ;

		// decriment frame count
		member->outDTMFFramesCount-- ;
		ast_mutex_unlock(&member->lock);
		return cfr ;
	}
	ast_mutex_unlock(&member->lock);
	return NULL ;
}
#endif

#ifdef	DTMF
void queue_outgoing_dtmf_frame( struct ast_conf_member* member, const struct ast_frame* fr )
{
	ast_mutex_lock(&member->lock);

	// accounting: count the number of outgoing frames for this member
	member->dtmf_frames_out++ ;

	//
	// we have to drop frames, so we'll drop new frames
	// because it's easier ( and doesn't matter much anyway ).
	//
	if ( member->outDTMFFramesCount >= AST_CONF_MAX_DTMF_QUEUE)
	{
		// accounting: count dropped outgoing frames
		member->dtmf_frames_out_dropped++ ;
		ast_mutex_unlock(&member->lock);
		return ;
	}

	//
	// create new conf frame from passed data frame
	//

	conf_frame* cfr = create_conf_frame( member, member->outDTMFFrames, fr ) ;

	if ( !cfr )
	{
		ast_log( LOG_ERROR, "unable to create new conf frame\n" ) ;

		// accounting: count dropped outgoing frames
		member->dtmf_frames_out_dropped++ ;
		ast_mutex_unlock(&member->lock);
		return ;
	}

	if ( !member->outDTMFFrames )
	{
		// this is the first frame in the buffer
		member->outDTMFFramesTail = cfr ;
		member->outDTMFFrames = cfr ;
	}
	else
	{
		// put the new frame at the head of the list
		member->outDTMFFrames = cfr ;
	}

	// increment member frame count
	member->outDTMFFramesCount++ ;

	ast_mutex_unlock(&member->lock);
}
#endif

void queue_frame_for_listener(
	struct ast_conference* conf,
	struct ast_conf_member* member
)
{
	struct ast_frame* qf ;
	struct conf_frame* frame = conf->listener_frame ;

	if ( frame )
	{
		// first, try for a pre-converted frame
		qf = (!member->listen_volume && !frame->talk_volume ? frame->converted[member->write_format_index] : 0);

		// convert ( and store ) the frame
		if ( !qf )
		{
			// make a copy of the slinear version of the frame
			qf = ast_frdup( frame->fr ) ;

			if (member->listen_volume )
			{
				ast_frame_adjust_volume(qf, member->listen_volume);
			}

			if ( !qf  )
			{
				ast_log( LOG_WARNING, "unable to duplicate frame\n" ) ;
				queue_silent_frame( conf, member ) ;
				return ;
			}

			// convert using the conference's translation path
			qf = convert_frame( conf->from_slinear_paths[ member->write_format_index ], qf ) ;

			// store the converted frame
			// ( the frame will be free'd next time through the loop )
			if (!member->listen_volume)
			{
				if (frame->converted[ member->write_format_index ])
					ast_frfree (frame->converted[ member->write_format_index ]);
				frame->converted[ member->write_format_index ] = qf ;
				frame->talk_volume = 0;
			}
		}

		if ( qf )
		{
			queue_outgoing_frame( member, qf, conf->delivery_time ) ;

			if ( member->listen_volume )
			{
				// free frame ( the translator's copy )
				ast_frfree( qf ) ;
			}
		}
		else
		{
			ast_log( LOG_WARNING, "unable to translate outgoing listener frame, channel => %s\n", member->chan->name ) ;
		}

	}
	else
	{
		queue_silent_frame( conf, member ) ;
	}
}

void queue_frame_for_speaker(
	struct ast_conference* conf,
	struct ast_conf_member* member
)
{
	struct ast_frame* qf ;
	conf_frame* frame = member->speaker_frame ;

	if ( frame )
	{
		//
		// convert and queue frame
		//

		if ( (qf = frame->converted[member->write_format_index]) && !member->listen_volume && !frame->talk_volume )
		{
			// frame is already in correct format, so just queue it

			queue_outgoing_frame( member, qf, conf->delivery_time ) ;
		}
		else
		{
			// make a copy of the slinear version of the frame
			qf = ast_frdup( frame->fr ) ;

			if (member->listen_volume )
			{
				ast_frame_adjust_volume(qf, member->listen_volume);
			}

			//
			// convert frame to member's write format
			// ( calling ast_frdup() to make sure the translator's copy sticks around )
			//
			qf = convert_frame( member->from_slinear, qf ) ;

			if ( qf )
			{
				// queue frame
				queue_outgoing_frame( member, qf, conf->delivery_time ) ;

				// free frame ( the translator's copy )
				ast_frfree( qf ) ;
			}
			else
			{
				ast_log( LOG_WARNING, "unable to translate outgoing speaker frame, channel => %s\n", member->chan->name ) ;
			}
		}
	}
	else
	{
		queue_silent_frame( conf, member ) ;
	}
}


void queue_silent_frame(
	struct ast_conference* conf,
	struct ast_conf_member* member
)
{
  int c;

	//
	// initialize static variables
	//

	static conf_frame* silent_frame = NULL ;
	static struct ast_frame* qf = NULL ;

	if ( !silent_frame )
	{
		if ( !( silent_frame = get_silent_frame() ) )
		{
			ast_log( LOG_WARNING, "unable to initialize static silent frame\n" ) ;
			return ;
		}
	}


	// get the appropriate silent frame
	qf = silent_frame->converted[ member->write_format_index ] ;

	if ( !qf  )
	{
		//
		// we need to do this to avoid echo on the speaker's line.
		// translators seem to be single-purpose, i.e. they
		// can't be used simultaneously for multiple audio streams
		//
#ifndef AC_USE_G722
		struct ast_trans_pvt* trans = ast_translator_build_path( member->write_format, AST_FORMAT_SLINEAR ) ;
#else
		struct ast_trans_pvt* trans = ast_translator_build_path( member->write_format, AST_FORMAT_SLINEAR16 ) ;
#endif
		if ( trans )
		{
			// attempt ( five times ) to get a silent frame
			// to make sure we provice the translator with enough data
			for ( c = 0 ; c < 5 ; ++c )
			{
				// translate the frame
				qf = ast_translate( trans, silent_frame->fr, 0 ) ;

				// break if we get a frame
				if ( qf ) break ;
			}

			if ( qf )
			{
				// isolate the frame so we can keep it around after trans is free'd
				qf = ast_frisolate( qf ) ;

				// cache the new, isolated frame
				silent_frame->converted[ member->write_format_index ] = qf ;
			}

			ast_translator_free_path( trans ) ;
		}
	}

	//
	// queue the frame, if it's not null,
	// otherwise there was an error
	//
	if ( qf )
	{
		queue_outgoing_frame( member, qf, conf->delivery_time ) ;
	}
	else
	{
		ast_log( LOG_ERROR, "unable to translate outgoing silent frame, channel => %s\n", member->chan->name ) ;
	}
}



void member_process_outgoing_frames(struct ast_conference* conf,
				  struct ast_conf_member *member)
{
	ast_mutex_lock(&member->lock);

	// skip members that are not ready
	// skip no receive audio clients
	if ( !member->ready_for_outgoing || member->norecv_audio == 1 )
	{
		ast_mutex_unlock(&member->lock);
		return ;
	}

	if ( !member->spy_partner )
	{
		// neither a spyer nor a spyee
		if ( !member->local_speaking_state ) 
		{
			// queue listener frame
			queue_frame_for_listener( conf, member ) ;
		}
		else
		{
			// queue speaker frame
			queue_frame_for_speaker( conf, member ) ;
		}
	}
	else
	{
		// either a spyer or a spyee
		if ( member->spyer != 0 )
		{
			// spyer -- always use member translator
			queue_frame_for_speaker( conf, member ) ;
		}
		else
		{
			// spyee -- use member translator if spyee speaking or spyer whispering to spyee
			if ( member->local_speaking_state == 1 || member->spy_partner->local_speaking_state == 1 )
			{
				queue_frame_for_speaker( conf, member ) ;
			}
			else
			{
				queue_frame_for_listener( conf, member ) ;
			}
		}
	}

	ast_mutex_unlock(&member->lock);
}

void member_process_spoken_frames(struct ast_conference* conf,
				 struct ast_conf_member *member,
				 struct conf_frame **spoken_frames,
				 long time_diff,
				 int *listener_count,
				 int *speaker_count
	)
{
	struct conf_frame *cfr;

	// acquire member mutex
	ast_mutex_lock( &member->lock ) ;

	// reset speaker frame
	member->speaker_frame = NULL ;

	// tell member the number of frames we're going to need ( used to help dropping algorithm )
	member->inFramesNeeded = ( time_diff / AST_CONF_FRAME_INTERVAL ) - 1 ;

	// non-listener member should have frames,
	// unless silence detection dropped them
	cfr = get_incoming_frame( member ) ;

	// handle retrieved frames
	if ( !cfr || !cfr->fr  )
	{
		// Decrement speaker count for us and for driven members
		// This happens only for the first missed frame, since we want to
		// decrement only on state transitions
		if ( member->local_speaking_state == 1 )
		{
			member->local_speaking_state = 0;
		}
		// count the listeners
		(*listener_count)++ ;
	}
	else
	{
		// append the frame to the list of spoken frames
		if ( *spoken_frames )
		{
			// add new frame to end of list
			cfr->next = *spoken_frames ;
			(*spoken_frames)->prev = cfr ;
		}

		// point the list at the new frame
		*spoken_frames = cfr ;
		// Increment speaker count for us and for driven members
		// This happens only on the first received frame, since we want to
		// increment only on state transitions
		if ( !member->local_speaking_state )
		{
			member->local_speaking_state = 1;
		}
		// count the speakers
		(*speaker_count)++ ;
	}

	// release member mutex
	ast_mutex_unlock( &member->lock ) ;

	return;
}

#ifdef	CACHE_CONTROL_BLOCKS
void freembrblocks( void )
{
	struct ast_conf_member *mbrblock;
	while ( mbrblocklist )
	{
		mbrblock = mbrblocklist;
		mbrblocklist = mbrblocklist->next;
		free( mbrblock );
	}
}
#endif
