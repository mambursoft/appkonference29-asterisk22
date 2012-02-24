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
#include "frame.h"

static char data[AST_CONF_BUFFER_SIZE] ;

static struct ast_frame fr = { .frametype = AST_FRAME_VOICE, 
#if     ASTERISK == 14
				.subclass = AST_FORMAT_SLINEAR,
				.data = &(data[AST_FRIENDLY_OFFSET]),
#elif	ASTERISK == 16
				.subclass = AST_FORMAT_SLINEAR,
				.data.ptr = &(data[AST_FRIENDLY_OFFSET]),
#else
				.subclass.integer = AST_FORMAT_SLINEAR,
				.data.ptr = &(data[AST_FRIENDLY_OFFSET]),
#endif
				.samples = AST_CONF_BLOCK_SAMPLES,
				.offset = AST_FRIENDLY_OFFSET,
				.datalen = AST_CONF_FRAME_DATA_SIZE } ;

static conf_frame cfr =  { .fr = &fr, .converted[AC_SLINEAR_INDEX] = &fr } ;

conf_frame *silent_conf_frame = &cfr ;

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

conf_frame* mix_frames( ast_conference* conf, conf_frame* frames_in, int speaker_count, int listener_count )
{
	if ( speaker_count == 1 )
	{
		// pass-through frames
		return mix_single_speaker( conf, frames_in ) ;
	}

	if ( speaker_count == 2 && listener_count == 0 )
	{
		ast_conf_member* mbr = NULL ;

		// copy orignal frame to converted array so speaker doesn't need to re-encode it
		frames_in->converted[ frames_in->member->read_format_index ] = frames_in->fr ;

		// convert frame to slinear and adjust volume; otherwise, drop both frames
		if (!(frames_in->fr = convert_frame( frames_in->member->to_slinear, frames_in->fr, 0)))
		{
			ast_log( LOG_WARNING, "mix_frames: unable to convert frame to slinear\n" ) ;
			return NULL ;
		} 
		if ( (frames_in->talk_volume = conf->volume + frames_in->member->talk_volume) )
		{
			ast_frame_adjust_volume(frames_in->fr, frames_in->talk_volume);
		}

		// copy orignal frame to converted array so speakers doesn't need to re-encode it
		frames_in->next->converted[ frames_in->next->member->read_format_index ] = frames_in->next->fr ;

		// convert frame to slinear and adjust volume; otherwise, drop both frames
		if (!(frames_in->next->fr = convert_frame( frames_in->next->member->to_slinear, frames_in->next->fr, 0)))
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

conf_frame* mix_single_speaker( ast_conference* conf, conf_frame* frames_in )
{
	//
	// 'mix' the frame
	//

	// copy orignal frame to converted array so listeners don't need to re-encode it
	frames_in->converted[ frames_in->member->read_format_index ] = frames_in->fr ;

	// convert frame to slinear; otherwise, drop the frame
	if (!(frames_in->fr = convert_frame( frames_in->member->to_slinear, frames_in->fr, 0)))
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

		// set the conference listener frame
		conf->listener_frame = frames_in ;
	}
	else
	{
		// speaker is either a spyee or a spyer
		if ( !frames_in->member->spyer
			&& conf->membercount > 2 )
		{
			conf_frame *spy_frame = create_conf_frame(frames_in->member, frames_in->fr);

			if ( spy_frame )
			{
				frames_in->next = spy_frame;
				spy_frame->prev = frames_in;

				spy_frame->member = frames_in->member->spy_partner;
				spy_frame->talk_volume = frames_in->talk_volume;

				spy_frame->converted[ frames_in->member->read_format_index ]
					= !frames_in->member->to_slinear ? spy_frame->fr :
						ast_frdup( frames_in->converted[ frames_in->member->read_format_index ] ) ; 

				spy_frame->member->speaker_frame = spy_frame ;
			}

			// set the conference listener frame
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
	ast_conference* conf,	
	conf_frame* frames_in,
	int speakers,
	int listeners
)
{
	//
	// mix the audio
	//

	// pointer to the spoken frames list
	conf_frame* cf_spoken = frames_in ;

	// clear listener mix buffer
	memset(conf->listenerBuffer,0,AST_CONF_BUFFER_SIZE) ;

	while ( cf_spoken )
	{
		// copy orignal frame to converted array so spyers don't need to re-encode it
		cf_spoken->converted[ cf_spoken->member->read_format_index ] = cf_spoken->fr ;

		if ( !(cf_spoken->fr = convert_frame( cf_spoken->member->to_slinear, cf_spoken->fr, 0)) )
		{
			ast_log( LOG_ERROR, "mix_multiple_speakers: unable to convert frame to slinear\n" ) ;
			return NULL;
		}

		if ( cf_spoken->member->talk_volume || conf->volume )
		{
			ast_frame_adjust_volume(cf_spoken->fr, cf_spoken->member->talk_volume + conf->volume);
		}

		if ( !cf_spoken->member->spyer )
		{
			// add the speaker's voice
#if	ASTERISK == 14
			mix_slinear_frames( conf->listenerBuffer + AST_FRIENDLY_OFFSET, cf_spoken->fr->data, AST_CONF_BLOCK_SAMPLES);
#else
			mix_slinear_frames( conf->listenerBuffer + AST_FRIENDLY_OFFSET, cf_spoken->fr->data.ptr, AST_CONF_BLOCK_SAMPLES);
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

	while ( cf_spoken )
	{
		if ( !cf_spoken->member->spyer )
		{
			// allocate/reuse mix buffer for speaker
			if ( !cf_spoken->member->speakerBuffer )
				cf_spoken->member->speakerBuffer = ast_malloc( AST_CONF_BUFFER_SIZE ) ;

			// clear speaker buffer
			memset(cf_spoken->member->speakerBuffer,0,AST_CONF_BUFFER_SIZE);

			if (!(cf_sendFrames = create_mix_frame(cf_spoken->member, cf_sendFrames, &cf_spoken->member->mixConfFrame)))
				return NULL ;

			cf_sendFrames->mixed_buffer = cf_spoken->member->speakerBuffer + AST_FRIENDLY_OFFSET ;

			// subtract the speaker's voice
#if	ASTERISK == 14
			unmix_slinear_frame(cf_sendFrames->mixed_buffer, conf->listenerBuffer + AST_FRIENDLY_OFFSET, cf_spoken->fr->data, AST_CONF_BLOCK_SAMPLES);
#else
			unmix_slinear_frame(cf_sendFrames->mixed_buffer, conf->listenerBuffer + AST_FRIENDLY_OFFSET, cf_spoken->fr->data.ptr, AST_CONF_BLOCK_SAMPLES);
#endif

			if ( cf_spoken->member->spy_partner && cf_spoken->member->spy_partner->local_speaking_state )
			{
				// add whisper voice
#if	ASTERISK == 14
				mix_slinear_frames(cf_sendFrames->mixed_buffer, cf_spoken->member->whisper_frame->fr->data, AST_CONF_BLOCK_SAMPLES);
#else
				mix_slinear_frames(cf_sendFrames->mixed_buffer, cf_spoken->member->whisper_frame->fr->data.ptr, AST_CONF_BLOCK_SAMPLES);
#endif
			}

			if (!(cf_sendFrames->fr = create_slinear_frame( &cf_sendFrames->member->mixAstFrame, cf_sendFrames->mixed_buffer )))
				return NULL ;

			cf_sendFrames->member->speaker_frame = cf_sendFrames ;
		}
		else if ( !cf_spoken->member->spy_partner->local_speaking_state )
		{
			// allocate/reuse a mix buffer for whisper
			if ( !cf_spoken->member->speakerBuffer )
				cf_spoken->member->speakerBuffer = ast_malloc( AST_CONF_BUFFER_SIZE ) ;

			// copy listener buffer for whisper
			memcpy(cf_spoken->member->speakerBuffer,conf->listenerBuffer,AST_CONF_BUFFER_SIZE);

			if (!(cf_sendFrames = create_mix_frame( cf_spoken->member->spy_partner, cf_sendFrames, &cf_spoken->member->mixConfFrame )))
				return NULL ;

			cf_sendFrames->mixed_buffer = cf_spoken->member->speakerBuffer + AST_FRIENDLY_OFFSET ;

			// add the whisper voice
#if	ASTERISK == 14
			mix_slinear_frames(cf_spoken->member->speakerBuffer + AST_FRIENDLY_OFFSET, cf_spoken->fr->data, AST_CONF_BLOCK_SAMPLES);
#else
			mix_slinear_frames(cf_spoken->member->speakerBuffer + AST_FRIENDLY_OFFSET, cf_spoken->fr->data.ptr, AST_CONF_BLOCK_SAMPLES);
#endif

			if (!(cf_sendFrames->fr = create_slinear_frame( &cf_sendFrames->member->mixAstFrame, cf_sendFrames->mixed_buffer )))
				return NULL ;

			cf_sendFrames->member->speaker_frame = cf_sendFrames ;
		}

		cf_spoken = cf_spoken->next;
	}

	//
	// if necessary, add a frame for listeners
	//

	if ( listeners > 0 )
	{
		if (!(cf_sendFrames = create_mix_frame( NULL, cf_sendFrames, &conf->mixConfFrame )))
			return NULL ;
		cf_sendFrames->mixed_buffer = conf->listenerBuffer + AST_FRIENDLY_OFFSET ;
		if (!(cf_sendFrames->fr = create_slinear_frame( &conf->mixAstFrame, cf_sendFrames->mixed_buffer )))
			return NULL ;

		// set the conference listener frame
		conf->listener_frame = cf_sendFrames ;
	}

	//
	// move any spyee frames to sendFrame list and delete the remaining frames
	// ( caller will only be responsible for free'ing returns frames )
	//

	// reset the spoken list pointer
	cf_spoken = frames_in ;

	while ( cf_spoken )
	{
		ast_conf_member *spy_partner = cf_spoken->member->spy_partner ;

		if ( !spy_partner || cf_spoken->member->spyer )
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
			if ( cf_spoken )
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

struct ast_frame* convert_frame( struct ast_trans_pvt* trans, struct ast_frame* fr, int consume )
{
	if ( !trans )
	{
		return fr ;
	}

	// convert the frame
	struct ast_frame* translated_frame = ast_translate( trans, fr, consume ) ;

	// return the translated frame
	return translated_frame ;
}

conf_frame* delete_conf_frame( conf_frame* cf )
{
  int c;

	if ( cf->fr )
	{
		ast_frfree( cf->fr ) ;
	}

	for ( c = 1 ; c < AC_SUPPORTED_FORMATS ; ++c )
	{
		if ( cf->converted[ c ] )
		{
			ast_frfree( cf->converted[ c ] ) ;
		}
	}

	conf_frame* nf = cf->next ;

	if ( !cf->mixed_buffer )
	{
#if	defined(CACHE_CONF_FRAMES) && defined(ONEMIXTHREAD)
		memset(cf,0,sizeof(conf_frame));
		AST_LIST_INSERT_HEAD(&confFrameList, cf, frame_list) ;
#else
		ast_free( cf ) ;
#endif
	}

	return nf ;
}

conf_frame* create_conf_frame( ast_conf_member* member, const struct ast_frame* fr )
{
	conf_frame* cf ;

#if	defined(CACHE_CONF_FRAMES) & defined(ONEMIXTHREAD)
	cf  = AST_LIST_REMOVE_HEAD(&confFrameList, frame_list) ;
	if ( !cf && !(cf = ast_calloc( 1, sizeof( conf_frame ) )) )
#else
	if ( !(cf  = ast_calloc( 1, sizeof( conf_frame ))) )
#endif
	{
		ast_log( LOG_ERROR, "unable to allocate memory for conf frame\n" ) ;
		return NULL ;
	}

	cf->member = member ;

	if ( fr )
	{
		if (!(cf->fr = ast_frdup(( struct ast_frame* )( fr ))))
		{
			ast_free(cf) ;
			ast_log( LOG_ERROR, "unable to allocate memory for conf frame\n" ) ;
			return NULL ;
		}
	}

	return cf ;
}

conf_frame* create_mix_frame( ast_conf_member* member, conf_frame* next, conf_frame** cf )
{
	if (!*cf)
	{
		if (!(*cf = ast_calloc( 1, sizeof( conf_frame ))))
		{
			ast_log( LOG_ERROR, "unable to allocate memory for conf frame\n" ) ;
			return NULL ;
		}
	}
	else
	{
		memset(*cf,0,sizeof( conf_frame )) ;
	}

	(*cf)->member = member ;

	if (next)
	{
		(*cf)->next = next ;
		next->prev = *cf ;
	}

	return *cf ;
}

//
// slinear frame function
//

struct ast_frame* create_slinear_frame(struct ast_frame **f, char* data )
{
	if (!*f)
	{
		if ( !(*f = ast_calloc( 1, sizeof( struct ast_frame ))) )
		{
			ast_log( LOG_ERROR, "unable to allocate memory for slinear frame\n" ) ;
			return NULL ;
		}
		(*f)->frametype = AST_FRAME_VOICE ;
#ifndef	AC_USE_G722
#if	ASTERISK == 14 || ASTERISK == 16
		(*f)->subclass = AST_FORMAT_SLINEAR ;
#else
		(*f)->subclass.integer = AST_FORMAT_SLINEAR ;
#endif
#else
#if	ASTERISK == 14 || ASTERISK == 16
		(*f)->subclass = AST_FORMAT_SLINEAR16 ;
#else
		(*f)->subclass.integer = AST_FORMAT_SLINEAR16 ;
#endif
#endif
		(*f)->samples = AST_CONF_BLOCK_SAMPLES ;
		(*f)->offset = AST_FRIENDLY_OFFSET ;
		(*f)->datalen = AST_CONF_FRAME_DATA_SIZE ;
		(*f)->src = NULL ;
	}
#if	ASTERISK == 14
	(*f)->data = data;
#else
	(*f)->data.ptr = data;
#endif
	return *f ;
}
