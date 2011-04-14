/*
 * app_konference
 *
 * A channel independent conference application for Asterisk
 *
 * Copyright (C) 2002, 2003 Junghanns.NET GmbH
 * Copyright (C) 2003, 2004 HorizonLive.com, Inc.
 * Copyright (C) 2005, 2006 HorizonWimba, Inc.
 * Copyright (C) 2007 Wimba, Inc.
 *
 * Klaus-Peter Junghanns <kapejod@ns1.jnetdns.de>
 *
 * Video Conferencing support added by
 * Neil Stratford <neils@vipadia.com>
 * Copyright (C) 2005, 2005 Vipadia Limited
 *
 * VAD driven video conferencing, text message support
 * and miscellaneous enhancements added by
 * Mihai Balea <mihai at hates dot ms>
 *
 * This program may be modified and distributed under the
 * terms of the GNU General Public License. You should have received
 * a copy of the GNU General Public License along with this
 * program; if not, write to the Free Software Foundation, Inc.
 * 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "asterisk/autoconfig.h"
#include "frame.h"

#ifdef	VECTORS

typedef short v4si __attribute__ ((vector_size (16))); 

static inline void mix_slinear_frames( char *dst, const char *src, int samples )
{
	int i ;

	for ( i = 0 ; i < samples / 8 ; ++i )
	{
		((v4si *)dst)[i] = ((v4si *)dst)[i] + ((v4si *)src)[i];
	}

	return ;
}

static inline void unmix_slinear_frame( char *dst, const char *src1, const char *src2, int samples )
{
	int i ;

	for ( i = 0 ; i < samples / 8 ; ++i )
	{
		((v4si *)dst)[i] = ((v4si *)src1)[i] - ((v4si *)src2)[i];
	}

	return ;
}

#else

static void mix_slinear_frames( char *dst, const char *src, int samples )
{
	int i, val ;

	for ( i = 0 ; i < samples ; ++i )
	{
		val = ( (short*)dst )[i] + ( (short*)src )[i] ;

		if ( val > 32767 )
		{
			( (short*)dst )[i] = 32767 ;
		}
		else if ( val < -32768 )
		{
			( (short*)dst )[i] = -32768 ;
		}
		else
		{
			( (short*)dst )[i] = val ;
		}
	}

	return ;
}

static void unmix_slinear_frame( char *dst, const char *src1, const char *src2, int samples )
{
	int i, val ;

	for ( i = 0 ; i < samples ; ++i )
	{
		val = ( (short*)src1 )[i] - ( (short*)src2 )[i] ;

		if ( val > 32767 )
		{
			( (short*)dst )[i] = 32767 ;
		}
		else if ( val < -32768 )
		{
			( (short*)dst )[i] = -32768 ;
		}
		else
		{
			( (short*)dst )[i] = val ;
		}
	}

	return ;
}

#endif

conf_frame* mix_frames( struct ast_conference* conf, conf_frame* frames_in, int speaker_count, int listener_count )
{
	if ( speaker_count == 1 )
	{
		// pass-through frames
		return mix_single_speaker( conf, frames_in ) ;
		//printf("mix single speaker\n");
	}

	if ( speaker_count == 2 && listener_count == 0 )
	{
		struct ast_conf_member* mbr = NULL ;

		// copy orignal frame to converted array so speaker doesn't need to re-encode it
		frames_in->converted[ frames_in->member->read_format_index ] = ast_frdup( frames_in->fr ) ;

		// convert frame to slinear and adjust volume; otherwise, drop both frames
		if (!(frames_in->fr = convert_frame( frames_in->member->to_slinear, frames_in->fr)))
		{
			ast_log( LOG_WARNING, "mix_frames: unable to convert frame to slinear\n" ) ;
			return NULL ;
		} 
		if ( (frames_in->talk_volume = conf->volume + frames_in->member->talk_volume) )
		{
			ast_frame_adjust_volume(frames_in->fr, frames_in->talk_volume);
		}

		// copy orignal frame to converted array so speakers doesn't need to re-encode it
		frames_in->next->converted[ frames_in->next->member->read_format_index ] = ast_frdup( frames_in->next->fr ) ;

		// convert frame to slinear and adjust volume; otherwise, drop both frames
		if (!(frames_in->next->fr = convert_frame( frames_in->next->member->to_slinear, frames_in->next->fr)))
		{
			ast_log( LOG_WARNING, "mix_frames: unable to convert frame to slinear\n" ) ;
			return NULL ;
		}
		if ( (frames_in->next->talk_volume = conf->volume + frames_in->next->member->talk_volume) )
		{
			ast_frame_adjust_volume(frames_in->next->fr, frames_in->next->talk_volume);
		}

		// swap frame member pointers
		mbr = frames_in->member ;
		frames_in->member = frames_in->next->member ;
		frames_in->next->member = mbr ;

		frames_in->member->speaker_frame = frames_in ;
		frames_in->next->member->speaker_frame = frames_in->next ;

		return frames_in ;
	}

	// mix spoken frames for sending
	// ( note: this call also releases us from free'ing spoken_frames )
	return mix_multiple_speakers( conf, frames_in, speaker_count, listener_count ) ;

}

conf_frame* mix_single_speaker( struct ast_conference* conf, conf_frame* frames_in )
{
#ifdef APP_KONFERENCE_DEBUG
	//DEBUG("returning single spoken frame\n") ;

	//
	// check input
	//

	if ( frames_in == NULL )
	{
		DEBUG("unable to mix single spoken frame with null frame\n") ;
		return NULL ;
	}

	if ( frames_in->fr == NULL )
	{
		DEBUG("unable to mix single spoken frame with null data\n") ;
		return NULL ;
	}

	if ( frames_in->member == NULL )
	{
		DEBUG("unable to mix single spoken frame with null member\n") ;
		return NULL ;
	}
#endif // APP_KONFERENCE_DEBUG

	//
	// 'mix' the frame
	//

	// copy orignal frame to converted array so listeners don't need to re-encode it
	frames_in->converted[ frames_in->member->read_format_index ] = ast_frdup( frames_in->fr ) ;

	// convert frame to slinear; otherwise, drop the frame
	if (!(frames_in->fr = convert_frame( frames_in->member->to_slinear, frames_in->fr)))
	{
		ast_log( LOG_WARNING, "mix_single_speaker: unable to convert frame to slinear\n" ) ;
		return NULL ;
	}

	if ( (frames_in->talk_volume = frames_in->member->talk_volume + conf->volume) )
	{
		ast_frame_adjust_volume(frames_in->fr, frames_in->talk_volume);
	}

	if (!frames_in->member->spy_partner)
	{
		// speaker is neither a spyee nor a spyer
		// set the frame's member to null ( i.e. all listeners )
		frames_in->member = NULL ;

		conf->listener_frame = frames_in ;
	}
	else
	{
		// speaker is either a spyee or a spyer
		if ( frames_in->member->spyer == 0
			&& conf->membercount > 2 )
		{
			conf_frame *spy_frame = copy_conf_frame(frames_in);

			if ( spy_frame != 0 )
			{
				frames_in->next = spy_frame;
				spy_frame->prev = frames_in;

				spy_frame->member = frames_in->member->spy_partner;
				spy_frame->talk_volume = frames_in->talk_volume;

				spy_frame->converted[ frames_in->member->read_format_index ]
					= ast_frdup( frames_in->converted[ frames_in->member->read_format_index ] ) ;

				spy_frame->member->speaker_frame = spy_frame ;
			}

			frames_in->member = NULL ;

			conf->listener_frame = frames_in ;
		}
		else
		{
			frames_in->member = frames_in->member->spy_partner ;

			frames_in->member->speaker_frame = frames_in ;
		}
	}

	return frames_in ;
}

conf_frame* mix_multiple_speakers(
	struct ast_conference* conf,	
	conf_frame* frames_in,
	int speakers,
	int listeners
)
{
#ifdef APP_KONFERENCE_DEBUG
	//
	// check input
	//

	// no frames to mix
	if ( ( frames_in == NULL ) || ( frames_in->fr == NULL ) )
	{
		ast_log( LOG_ERROR, "passed spoken frame list was NULL\n") ;
		return NULL ;
	}

	// if less than two speakers, then no frames to mix
	if ( speakers < 2 )
	{
		ast_log( LOG_ERROR, "mix_multiple_speakers() called with less than two speakers\n") ;
		return NULL ;
	}
#endif // APP_KONFERENCE_DEBUG

	//
	// mix the audio
	//

	// pointer to the spoken frames list
	conf_frame* cf_spoken = frames_in ;

	// allocate a mix buffer large enough to hold a frame
	char* listenerBuffer = calloc( AST_CONF_BUFFER_SIZE, sizeof(char) ) ;

	while ( cf_spoken != NULL )
	{

		if ( !(cf_spoken->fr = convert_frame( cf_spoken->member->to_slinear, cf_spoken->fr)) )
		{
			ast_log( LOG_ERROR, "mix_multiple_speakers: unable to convert frame to slinear\n" ) ;
			return NULL;
		}

		if (( cf_spoken->member->talk_volume != 0 ) || (conf->volume != 0))
		{
			ast_frame_adjust_volume(cf_spoken->fr, cf_spoken->member->talk_volume + conf->volume);
		}

		if ( cf_spoken->member->spyer == 0 )
		{
			// add the speaker's voice
#if	ASTERISK == 14
			mix_slinear_frames( listenerBuffer + AST_FRIENDLY_OFFSET, cf_spoken->fr->data, AST_CONF_BLOCK_SAMPLES);
#else
			mix_slinear_frames( listenerBuffer + AST_FRIENDLY_OFFSET, cf_spoken->fr->data.ptr, AST_CONF_BLOCK_SAMPLES);
#endif
		} 
		else
		{
			cf_spoken->member->spy_partner->whisper_frame = cf_spoken;
		}

		cf_spoken = cf_spoken->next;
	}

	//
	// create the send frame list
	//

	// reset the send list pointer
	cf_spoken = frames_in ;

	// pointer to the new list of mixed frames
	conf_frame* cf_sendFrames = NULL ;

	while ( cf_spoken != NULL )
	{
		if ( cf_spoken->member->spyer == 0 )
		{
			// allocate a mix buffer large enough to hold a frame
			char* speakerBuffer = calloc( AST_CONF_BUFFER_SIZE, sizeof(char) ) ;

			cf_sendFrames = create_conf_frame(cf_spoken->member, cf_sendFrames, NULL);

			cf_sendFrames->mixed_buffer = speakerBuffer + AST_FRIENDLY_OFFSET ;

			// subtract the speaker's voice
#if	ASTERISK == 14
			unmix_slinear_frame(cf_sendFrames->mixed_buffer, listenerBuffer + AST_FRIENDLY_OFFSET, cf_spoken->fr->data, AST_CONF_BLOCK_SAMPLES);
#else
			unmix_slinear_frame(cf_sendFrames->mixed_buffer, listenerBuffer + AST_FRIENDLY_OFFSET, cf_spoken->fr->data.ptr, AST_CONF_BLOCK_SAMPLES);
#endif

			if ( cf_spoken->member->spy_partner && cf_spoken->member->spy_partner->local_speaking_state != 0 )
			{
				// add whisper voice
#if	ASTERISK == 14
				mix_slinear_frames( cf_sendFrames->mixed_buffer, cf_spoken->member->whisper_frame->fr->data, AST_CONF_BLOCK_SAMPLES);
#else
				mix_slinear_frames( cf_sendFrames->mixed_buffer, cf_spoken->member->whisper_frame->fr->data.ptr, AST_CONF_BLOCK_SAMPLES);
#endif
			}

			cf_sendFrames->fr = create_slinear_frame( cf_sendFrames->mixed_buffer ) ;

			cf_sendFrames->member->speaker_frame = cf_sendFrames ;
		}
		else if ( cf_spoken->member->spy_partner->local_speaking_state == 0 )
		{
			// allocate a mix buffer large enough to hold a frame
			char* whisperBuffer = malloc( AST_CONF_BUFFER_SIZE ) ;
			memcpy(whisperBuffer,listenerBuffer,AST_CONF_BUFFER_SIZE);

			cf_sendFrames = create_conf_frame( cf_spoken->member->spy_partner, cf_sendFrames, NULL ) ;

			cf_sendFrames->mixed_buffer = whisperBuffer + AST_FRIENDLY_OFFSET ;

			// add the whisper voice
#if	ASTERISK == 14
			mix_slinear_frames( whisperBuffer + AST_FRIENDLY_OFFSET, cf_spoken->fr->data, AST_CONF_BLOCK_SAMPLES);
#else
			mix_slinear_frames( whisperBuffer + AST_FRIENDLY_OFFSET, cf_spoken->fr->data.ptr, AST_CONF_BLOCK_SAMPLES);
#endif

			cf_sendFrames->fr = create_slinear_frame( cf_sendFrames->mixed_buffer ) ;

			cf_sendFrames->member->speaker_frame = cf_sendFrames ;
		}

		cf_spoken = cf_spoken->next;
	}

	//
	// if necessary, add a frame for listeners
	//

	if ( listeners > 0 )
	{
		cf_sendFrames = create_conf_frame( NULL, cf_sendFrames, NULL ) ;
		cf_sendFrames->mixed_buffer = listenerBuffer + AST_FRIENDLY_OFFSET ;
		cf_sendFrames->fr = create_slinear_frame( cf_sendFrames->mixed_buffer ) ;

		conf->listener_frame = cf_sendFrames ;
	}
	else
	{
		free(listenerBuffer);
	}

	//
	// move any spyee frames to sendFrame list and delete the remaining frames
	// ( caller will only be responsible for free'ing returns frames )
	//

	// reset the spoken list pointer
	cf_spoken = frames_in ;

	while ( cf_spoken != NULL )
	{
		struct ast_conf_member *spy_partner = cf_spoken->member->spy_partner ;

		if ( spy_partner == NULL || cf_spoken->member->spyer != 0 )
		{
			// delete the frame
			cf_spoken = delete_conf_frame( cf_spoken ) ;
		}
		else
		{
			// move the unmixed frame to sendFrames
			//  and indicate which spyer it's for
			conf_frame *spy_frame = cf_spoken ;

			cf_spoken = cf_spoken->next;
			if ( cf_spoken != NULL )
				cf_spoken->prev = NULL;

			spy_frame->next = cf_sendFrames;
			cf_sendFrames->prev = spy_frame;
			spy_frame->prev = NULL;

			spy_frame->member = spy_partner;

			cf_sendFrames = spy_frame;

			cf_sendFrames->member->speaker_frame = cf_sendFrames ;
		}
	}

	// return the list of frames for sending
	return cf_sendFrames ;
}

struct ast_frame* convert_frame( struct ast_trans_pvt* trans, struct ast_frame* fr )
{
	if ( trans == NULL )
	{
		return fr ;
	}

#ifdef APP_KONFERENCE_DEBUG
	if ( fr == NULL )
	{
		ast_log( LOG_WARNING, "unable to convert null frame\n" ) ;
		return NULL ;
	}
#endif

	// convert the frame
	struct ast_frame* translated_frame = ast_translate( trans, fr, 1 ) ;

#ifdef APP_KONFERENCE_DEBUG
	// check for errors
	if ( translated_frame == NULL )
	{
		ast_log( LOG_ERROR, "unable to translate frame\n" ) ;
		return NULL ;
	}
#endif

	// return the translated frame
	return translated_frame ;
}

conf_frame* delete_conf_frame( conf_frame* cf )
{
  int c;
#ifdef APP_KONFERENCE_DEBUG
	// check for null frames
	if ( cf == NULL )
	{
		ast_log( LOG_ERROR, "unable to delete null conf frame\n") ;
		return NULL ;
	}
#endif
	if ( cf->fr != NULL )
	{
		ast_frfree( cf->fr ) ;
		cf->fr = NULL ;
	}

	// make sure converted frames are set to null
	for ( c = 0 ; c < AC_SUPPORTED_FORMATS ; ++c )
	{
		if ( cf->converted[ c ] != NULL )
		{
			ast_frfree( cf->converted[ c ] ) ;
			cf->converted[ c ] = NULL ;
		}
	}

	// get a pointer to the next frame
	// in the list so we can return it
	conf_frame* nf = cf->next ;

	free( cf ) ;
	cf = NULL ;

	return nf ;
}

conf_frame* create_conf_frame( struct ast_conf_member* member, conf_frame* next, const struct ast_frame* fr )
{
	// pointer to list of mixed frames
	conf_frame* cf = malloc( sizeof( struct conf_frame ) ) ;

	if ( cf == NULL )
	{
		ast_log( LOG_ERROR, "unable to allocate memory for conf frame\n" ) ;
		return NULL ;
	}

	//
	// init with some defaults
	//

	// make sure converted frames are set to null
//	for ( int c = 0 ; c < AC_SUPPORTED_FORMATS ; ++c )
//	{
//		cf->converted[ c ] = NULL ;
//	}

	memset( (struct ast_frame*)( cf->converted ), 0x0, ( sizeof( struct ast_frame* ) * AC_SUPPORTED_FORMATS ) ) ;

	cf->member = member ;
	// cf->priority = 0 ;

	cf->prev = NULL ;
	cf->next = next ;

	// establish relationship to 'next'
	if ( next != NULL ) next->prev = cf ;

	// this holds the ast_frame pointer
	cf->fr = ( fr == NULL ) ? NULL : ast_frdup( ( struct ast_frame* )( fr ) ) ;

	// this holds the temporu mix buffer
	cf->mixed_buffer = NULL ;

	cf->talk_volume = 0 ;

	return cf ;
}

conf_frame* copy_conf_frame( conf_frame* src )
{
#ifdef	APP_KONFERENCE_DEBUG
	//
	// check inputs
	//

	if ( src == NULL )
	{
		ast_log( LOG_ERROR, "unable to copy null conf frame\n") ;
		return NULL ;
	}
#endif
	//
	// copy the frame
	//

	struct conf_frame *cfr = NULL ;

	// create a new conf frame
	cfr = create_conf_frame( src->member, NULL, src->fr ) ;

	if ( cfr == NULL )
	{
		DEBUG("unable to create new conf frame for copy\n") ;
		return NULL ;
	}

	return cfr ;
}

#ifdef	TEXT
//
// Create a TEXT frame based on a given string
//
struct ast_frame* create_text_frame(const char *text, int copy)
{
	struct ast_frame *f;
	char             *t;

	f = calloc(1, sizeof(struct ast_frame));
	if ( f == NULL )
	{
		ast_log( LOG_ERROR, "unable to allocate memory for text frame\n" ) ;
		return NULL ;
	}
	if ( copy )
	{
		t = calloc(strlen(text) + 1, 1);
		if ( t == NULL )
		{
			ast_log( LOG_ERROR, "unable to allocate memory for text data\n" ) ;
			free(f);
			return NULL ;
		}
		strncpy(t, text, strlen(text));
	} else
	{
		t = (char *)text;
	}

	f->frametype = AST_FRAME_TEXT;
	f->offset = 0;
	f->mallocd = AST_MALLOCD_HDR;
	if ( copy ) f->mallocd |= AST_MALLOCD_DATA;
	f->datalen = strlen(t) + 1;
#if	ASTERISK == 14
	f->data = t;
#else
	f->data.ptr = t;
#endif
	f->src = NULL;

	return f;
}
#endif

//
// slinear frame functions
//

struct ast_frame* create_slinear_frame( char* data )
{
	struct ast_frame* f ;

	f = calloc( 1, sizeof( struct ast_frame ) ) ;
	if ( f == NULL )
	{
		ast_log( LOG_ERROR, "unable to allocate memory for slinear frame\n" ) ;
		return NULL ;
	}

	f->frametype = AST_FRAME_VOICE ;
#ifndef	AC_USE_G722
#if	ASTERISK == 14 || ASTERISK == 16
	f->subclass = AST_FORMAT_SLINEAR ;
#else
	f->subclass.integer = AST_FORMAT_SLINEAR ;
#endif
#else
#if	ASTERISK == 14 || ASTERISK == 16
	f->subclass = AST_FORMAT_SLINEAR16 ;
#else
	f->subclass.integer = AST_FORMAT_SLINEAR16 ;
#endif
#endif
	f->samples = AST_CONF_BLOCK_SAMPLES ;
	f->offset = AST_FRIENDLY_OFFSET ;
	f->mallocd = AST_MALLOCD_HDR | AST_MALLOCD_DATA ;

	f->datalen = AST_CONF_FRAME_DATA_SIZE ;
#if	ASTERISK == 14
	f->data = data;
#else
	f->data.ptr = data;
#endif
	f->src = NULL ;

	return f ;
}

//
// silent frame functions
//

conf_frame* get_silent_frame( void )
{
	static conf_frame* static_silent_frame = NULL ;

	// we'll let this leak until the application terminates
	if ( static_silent_frame == NULL )
	{
		//DEBUG("creating cached silent frame\n") ;
		struct ast_frame* fr = get_silent_slinear_frame() ;

		static_silent_frame = create_conf_frame( NULL, NULL, fr ) ;

		if ( static_silent_frame == NULL )
		{
			ast_log( LOG_WARNING, "unable to create cached silent frame\n" ) ;
			return NULL ;
		}

		// init the 'converted' slinear silent frame
		static_silent_frame->converted[ AC_SLINEAR_INDEX ] = get_silent_slinear_frame() ;
	}

	return static_silent_frame ;
}

struct ast_frame* get_silent_slinear_frame( void )
{
	static struct ast_frame* f = NULL ;

	// we'll let this leak until the application terminates
	if ( f == NULL )
	{
		char* data = malloc( AST_CONF_BUFFER_SIZE ) ;
		memset( data, 0x0, AST_CONF_BUFFER_SIZE ) ;
		f = create_slinear_frame( data + AST_FRIENDLY_OFFSET ) ;
	}

	return f;
}













