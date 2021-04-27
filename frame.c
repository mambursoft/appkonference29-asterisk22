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

#ifdef	VECTORS

typedef short v8hi __attribute__ ((vector_size (16))); 

static inline void mix_slinear_frames(char *dst, const char *src, int samples)
{
	int i;

	for (i = 0; i < samples / 8; ++i)
	{
		((v8hi *) dst)[i] = __builtin_ia32_paddsw128(((v8hi *) dst)[i] , ((v8hi *) src)[i]);
	}
}

static inline void unmix_slinear_frame(char *dst, const char *src1, const char *src2, int samples)
{
	int i;

	for (i = 0; i < samples / 8; ++i)
	{
		((v8hi *) dst)[i] = __builtin_ia32_psubsw128(((v8hi *) src1)[i] , ((v8hi *) src2)[i]);
	}
}

#else

static void mix_slinear_frames(char *dst, const char *src, int samples)
{
	int i, val;

	for (i = 0; i < samples; ++i)
	{
		val = ((short*)dst)[i] + ((short*)src)[i];

		if (val > 32767)
		{
			((short*)dst)[i] = 32767;
		}
		else if (val < -32768)
		{
			((short*)dst)[i] = -32768;
		}
		else
		{
			((short*)dst)[i] = val;
		}
	}
}

static void unmix_slinear_frame(char *dst, const char *src1, const char *src2, int samples)
{
	int i, val;

	for (i = 0; i < samples; ++i)
	{
		val = ((short*)src1)[i] - ((short*)src2)[i];

		if (val > 32767)
		{
			((short*)dst)[i] = 32767;
		}
		else if (val < -32768)
		{
			((short*)dst)[i] = -32768;
		}
		else
		{
			((short*)dst)[i] = val;
		}
	}
}

#endif

conf_frame* mix_frames(ast_conference* conf, conf_frame* frames_in, int speaker_count, int listener_count)
{
	if (speaker_count == 1)
	{
		// pass-through frames
		return mix_single_speaker(conf, frames_in);
	}

	if (speaker_count == 2 && listener_count == 0)
	{
		ast_conf_member* mbr = NULL;

		// copy orignal frame to converted array so speaker doesn't need to re-encode it
		frames_in->converted[frames_in->member->read_format] = frames_in->fr;

		// convert frame to slinear and adjust volume; otherwise, drop both frames
		if (!(frames_in->fr = convert_frame(frames_in->member->to_slinear, frames_in->fr, 0)))
		{
			ast_log(LOG_WARNING, "mix_frames: unable to convert frame to slinear\n");
			return NULL;
		} 
		if ((frames_in->talk_volume = conf->volume + frames_in->member->talk_volume))
		{
			ast_frame_adjust_volume(frames_in->fr, frames_in->talk_volume > 0 ? frames_in->talk_volume + 1 : frames_in->talk_volume - 1);
#if	SILDET == 2
			if (frames_in->member->read_format != AC_CONF && !frames_in->member->dsp)
#else
			if (frames_in->member->read_format != AC_CONF)
#endif
				{
					ast_frfree(frames_in->converted[frames_in->member->read_format]);
					frames_in->converted[frames_in->member->read_format] = NULL;
				}
		}

		// copy orignal frame to converted array so speakers doesn't need to re-encode it
		frames_in->next->converted[frames_in->next->member->read_format] = frames_in->next->fr;

		// convert frame to slinear and adjust volume; otherwise, drop both frames
		if (!(frames_in->next->fr = convert_frame(frames_in->next->member->to_slinear, frames_in->next->fr, 0)))
		{
			ast_log(LOG_WARNING, "mix_frames: unable to convert frame to slinear\n");
			return NULL;
		}
		if ((frames_in->next->talk_volume = conf->volume + frames_in->next->member->talk_volume))
		{
			ast_frame_adjust_volume(frames_in->next->fr, frames_in->next->talk_volume > 0 ? frames_in->next->talk_volume + 1 : frames_in->next->talk_volume - 1);
#if	SILDET == 2
			if (frames_in->next->member->read_format != AC_CONF && !frames_in->next->member->dsp)
#else
			if (frames_in->next->member->read_format != AC_CONF)
#endif
				{
					ast_frfree(frames_in->next->converted[frames_in->next->member->read_format]);
					frames_in->next->converted[frames_in->next->member->read_format] = NULL;
				}
		}

		// swap frame member pointers
		mbr = frames_in->member;
		frames_in->member = frames_in->next->member;
		frames_in->next->member = mbr;

		frames_in->member->speaker_frame = frames_in;
		AST_LIST_INSERT_HEAD(&frames_in->speaker_frame_list_head, frames_in->member, speaker_frame_list_entry);

		frames_in->next->member->speaker_frame = frames_in->next;
		AST_LIST_INSERT_HEAD(&frames_in->next->speaker_frame_list_head, frames_in->next->member, speaker_frame_list_entry);

		return frames_in;
	}

	// mix spoken frames for sending
	// (note: this call also releases us from free'ing spoken_frames)
	return mix_multiple_speakers(conf, frames_in, speaker_count, listener_count);

}

conf_frame* mix_single_speaker(ast_conference* conf, conf_frame* frames_in)
{
	//
	// 'mix' the frame
	//

	// copy orignal frame to converted array so listeners don't need to re-encode it
	frames_in->converted[frames_in->member->read_format] = frames_in->fr;

	// convert frame to slinear; otherwise, drop the frame
	if (!(frames_in->fr = convert_frame(frames_in->member->to_slinear, frames_in->fr, 0)))
	{
		ast_log(LOG_WARNING, "mix_single_speaker: unable to convert frame to slinear\n");
		return NULL;
	}

	if ((frames_in->talk_volume = frames_in->member->talk_volume + conf->volume))
	{
		ast_frame_adjust_volume(frames_in->fr, frames_in->talk_volume > 0 ? frames_in->talk_volume + 1 : frames_in->talk_volume - 1);
#if	SILDET == 2
		if (frames_in->member->read_format != AC_CONF && !frames_in->member->dsp)
#else
		if (frames_in->member->read_format != AC_CONF)
#endif
		{
			ast_frfree(frames_in->converted[frames_in->member->read_format]);
			frames_in->converted[frames_in->member->read_format] = NULL;
		}
	}

	if (AST_LIST_EMPTY(&frames_in->member->spy_list.head))
	{
		// speaker is neither a spyee nor a spyer

		// set the conference listener frame
		conf->listener_frame = frames_in;
		frames_in->member = NULL;
	}
	else
	{
		// speaker is either a spyee or a spyer
		if (!frames_in->member->spyee_channel_name)
		{
			ast_conf_member *entry;
			AST_LIST_TRAVERSE(&frames_in->member->spy_list.head, entry, spy_list.entry)
			{
				entry->speaker_frame = frames_in;
				AST_LIST_INSERT_HEAD(&frames_in->speaker_frame_list_head, entry, speaker_frame_list_entry);
			}

			conf->listener_frame = frames_in;
		}
		else
		{
			frames_in->member = AST_LIST_FIRST(&frames_in->member->spy_list.head);

			frames_in->member->speaker_frame = frames_in;

			AST_LIST_INSERT_HEAD(&frames_in->speaker_frame_list_head, frames_in->member, speaker_frame_list_entry);
		}
	}

	return frames_in;
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
	conf_frame* cf_spoken = frames_in;

	// clear listener mix buffer
	memset(conf->listenerBuffer,0,AST_CONF_BUFFER_SIZE);

	while (cf_spoken)
	{
		// copy orignal frame to converted array so spyers don't need to re-encode it
		cf_spoken->converted[cf_spoken->member->read_format] = cf_spoken->fr;

		if (!(cf_spoken->fr = convert_frame(cf_spoken->member->to_slinear, cf_spoken->fr, 0)))
		{
			ast_log(LOG_ERROR, "mix_multiple_speakers: unable to convert frame to slinear\n");
			return NULL;
		}

		if (cf_spoken->member->talk_volume || conf->volume)
		{
			ast_frame_adjust_volume(cf_spoken->fr, cf_spoken->member->talk_volume + conf->volume > 0 ? cf_spoken->member->talk_volume + conf->volume + 1 : cf_spoken->member->talk_volume + conf->volume - 1);
#if	SILDET == 2
			if (cf_spoken->member->read_format != AC_CONF && !cf_spoken->member->dsp)
#else
			if (cf_spoken->member->read_format != AC_CONF)
#endif
			{
				ast_frfree(cf_spoken->converted[cf_spoken->member->read_format]);
				cf_spoken->converted[cf_spoken->member->read_format] = NULL;
			}
		}

		if (!cf_spoken->member->spyee_channel_name)
		{
			// add the speaker's voice
#if	ASTERISK_SRC_VERSION == 104
			mix_slinear_frames(conf->listenerBuffer + AST_FRIENDLY_OFFSET, cf_spoken->fr->data, AST_CONF_BLOCK_SAMPLES);
#else
			mix_slinear_frames(conf->listenerBuffer + AST_FRIENDLY_OFFSET, cf_spoken->fr->data.ptr, AST_CONF_BLOCK_SAMPLES);
#endif
		} 
		else
		{
			ast_conf_member *spyee = AST_LIST_FIRST(&cf_spoken->member->spy_list.head);
			if (!spyee->whisper_frame)
			{
				spyee->whisper_frame = cf_spoken;
			}
			else
			{
#if     ASTERISK_SRC_VERSION == 104
				mix_slinear_frames(spyee->whisper_frame->fr->data, cf_spoken->fr->data, AST_CONF_BLOCK_SAMPLES);
#else
				mix_slinear_frames(spyee->whisper_frame->fr->data.ptr, cf_spoken->fr->data.ptr, AST_CONF_BLOCK_SAMPLES);
#endif
			}
		}

		cf_spoken = cf_spoken->next;
	}

	//
	// create the send frame list
	//

	// reset the send list pointer
	cf_spoken = frames_in;

	// pointer to the new list of mixed frames
	conf_frame* cf_sendFrames = NULL;

	while (cf_spoken)
	{
		if (!cf_spoken->member->spyee_channel_name)
		{
			// allocate/reuse mix buffer for speaker
			if (!cf_spoken->member->speakerBuffer)
				cf_spoken->member->speakerBuffer = ast_malloc(AST_CONF_BUFFER_SIZE);

			// clear speaker buffer
			memset(cf_spoken->member->speakerBuffer,0,AST_CONF_BUFFER_SIZE);

			if (!(cf_sendFrames = create_mix_frame(cf_spoken->member, cf_sendFrames, &cf_spoken->member->mixConfFrame)))
				return NULL;

			cf_sendFrames->mixed_buffer = cf_spoken->member->speakerBuffer + AST_FRIENDLY_OFFSET;

			// subtract the speaker's voice
#if	ASTERISK_SRC_VERSION == 104
			unmix_slinear_frame(cf_sendFrames->mixed_buffer, conf->listenerBuffer + AST_FRIENDLY_OFFSET, cf_spoken->fr->data, AST_CONF_BLOCK_SAMPLES);
#else
			unmix_slinear_frame(cf_sendFrames->mixed_buffer, conf->listenerBuffer + AST_FRIENDLY_OFFSET, cf_spoken->fr->data.ptr, AST_CONF_BLOCK_SAMPLES);
#endif
			// add whisper voice
			if (cf_spoken->member->whisper_frame)
			{
#if	ASTERISK_SRC_VERSION == 104
				mix_slinear_frames(cf_sendFrames->mixed_buffer, cf_spoken->member->whisper_frame->fr->data, AST_CONF_BLOCK_SAMPLES);
#else
				mix_slinear_frames(cf_sendFrames->mixed_buffer, cf_spoken->member->whisper_frame->fr->data.ptr, AST_CONF_BLOCK_SAMPLES);
#endif
				cf_spoken->member->whisper_frame = NULL; // reset whisper frame
			}

			if (!(cf_sendFrames->fr = create_slinear_frame(&cf_sendFrames->member->mixAstFrame, cf_sendFrames->mixed_buffer)))
				return NULL;

			cf_sendFrames->member->speaker_frame = cf_sendFrames;

			AST_LIST_INSERT_HEAD(&cf_sendFrames->speaker_frame_list_head, cf_sendFrames->member, speaker_frame_list_entry);
		}
		else if (!AST_LIST_FIRST(&cf_spoken->member->spy_list.head)->is_speaking)
		{
			ast_conf_member *spyee = AST_LIST_FIRST(&cf_spoken->member->spy_list.head);
			conf_frame *whisper_frame = spyee->whisper_frame;

			// add whisper voice
			if (whisper_frame)
			{
				// reset whisper frame
				spyee->whisper_frame = NULL;

				// allocate/reuse a mix buffer for whisper
				if (!cf_spoken->member->speakerBuffer)
					cf_spoken->member->speakerBuffer = ast_malloc(AST_CONF_BUFFER_SIZE);

				// copy listener buffer for whisper
				memcpy(cf_spoken->member->speakerBuffer,conf->listenerBuffer,AST_CONF_BUFFER_SIZE);

				if (!(cf_sendFrames = create_mix_frame(AST_LIST_FIRST(&cf_spoken->member->spy_list.head), cf_sendFrames, &cf_spoken->member->mixConfFrame)))
					return NULL;

				cf_sendFrames->mixed_buffer = cf_spoken->member->speakerBuffer + AST_FRIENDLY_OFFSET;

				// add the whisper voice
#if	ASTERISK_SRC_VERSION == 104
				mix_slinear_frames(cf_spoken->member->speakerBuffer + AST_FRIENDLY_OFFSET, whisper_frame->fr->data, AST_CONF_BLOCK_SAMPLES);
#else
				mix_slinear_frames(cf_spoken->member->speakerBuffer + AST_FRIENDLY_OFFSET, whisper_frame->fr->data.ptr, AST_CONF_BLOCK_SAMPLES);
#endif

				if (!(cf_sendFrames->fr = create_slinear_frame(&cf_sendFrames->member->mixAstFrame, cf_sendFrames->mixed_buffer)))
					return NULL;

				cf_sendFrames->member->speaker_frame = cf_sendFrames;

				AST_LIST_INSERT_HEAD(&cf_sendFrames->speaker_frame_list_head, cf_sendFrames->member, speaker_frame_list_entry);
			}
		}

		cf_spoken = cf_spoken->next;
	}

	//
	// if necessary, add a frame for listeners
	//

	if (listeners > 0)
	{
		if (!(cf_sendFrames = create_mix_frame(NULL, cf_sendFrames, &conf->mixConfFrame)))
			return NULL;
		cf_sendFrames->mixed_buffer = conf->listenerBuffer + AST_FRIENDLY_OFFSET;
		if (!(cf_sendFrames->fr = create_slinear_frame(&conf->mixAstFrame, cf_sendFrames->mixed_buffer)))
			return NULL;

		// set the conference listener frame
		conf->listener_frame = cf_sendFrames;
	}

	//
	// move any spyee frames to sendFrame list and delete the remaining frames
	// (caller will only be responsible for free'ing returns frames)
	//

	// reset the spoken list pointer
	cf_spoken = frames_in;

	while (cf_spoken)
	{
		if (AST_LIST_EMPTY(&cf_spoken->member->spy_list.head) || cf_spoken->member->spyee_channel_name)
		{
			// delete the frame
			cf_spoken = delete_conf_frame(cf_spoken);
		}
		else
		{
			// move the unmixed frame to sendFrames
			//  and indicate which spyer it's for
			conf_frame *spy_frame = cf_spoken;

			cf_spoken = cf_spoken->next;

			ast_conf_member *entry;
			AST_LIST_TRAVERSE(&spy_frame->member->spy_list.head, entry, spy_list.entry)
			{
				entry->speaker_frame = spy_frame;
				AST_LIST_INSERT_HEAD(&spy_frame->speaker_frame_list_head, entry, speaker_frame_list_entry);
			}
			spy_frame->member = NULL;

			spy_frame->next = cf_sendFrames;

			cf_sendFrames = spy_frame;
		}
	}

	// return the list of frames for sending
	return cf_sendFrames;
}

struct ast_frame* convert_frame(struct ast_trans_pvt* trans, struct ast_frame* fr, int consume)
{
	// return translated frame
	return !trans ? fr : ast_translate(trans, fr, consume);
}

conf_frame* delete_conf_frame(conf_frame* cf)
{
  int c;
	ast_frfree(cf->fr);
	for (c = 1; c < AC_SUPPORTED_FORMATS; ++c)
	{
		if (cf->converted[c])
		{
			ast_frfree(cf->converted[c]);
		}
	}

	conf_frame* nf = cf->next;

	if (!cf->mixed_buffer)
	{
#ifdef	CACHE_CONF_FRAMES
		memset(cf,0,sizeof(conf_frame));
		AST_LIST_INSERT_HEAD(&confFrameList, cf, free_list);
#else
		ast_free(cf);
#endif
	}

	return nf;
}

conf_frame* create_conf_frame(ast_conf_member* member, struct ast_frame* fr)
{
	conf_frame* cf;

#ifdef	CACHE_CONF_FRAMES
	cf  = AST_LIST_REMOVE_HEAD(&confFrameList, free_list);
	if (!cf && !(cf = ast_calloc(1, sizeof(conf_frame))))
#else
	if (!(cf  = ast_calloc(1, sizeof(conf_frame))))
#endif
	{
		ast_log(LOG_ERROR, "unable to allocate memory for conf frame\n");
		return NULL;
	}
	cf->member = member;
	cf->fr = fr;

	return cf;
}

conf_frame* create_mix_frame(ast_conf_member* member, conf_frame* next, conf_frame** cf)
{
	if (!*cf)
	{
		if (!(*cf = ast_calloc(1, sizeof(conf_frame))))
		{
			ast_log(LOG_ERROR, "unable to allocate memory for conf frame\n");
			return NULL;
		}
	}
	else
	{
		memset(*cf,0,sizeof(conf_frame));
	}

	(*cf)->member = member;

	if (next)
	{
		(*cf)->next = next;
	}

	return *cf;
}

//
// slinear frame function
//

struct ast_frame* create_slinear_frame(struct ast_frame **f, char* data)
{
	if (!*f)
	{
		if (!(*f = ast_calloc(1, sizeof(struct ast_frame))))
		{
			ast_log(LOG_ERROR, "unable to allocate memory for slinear frame\n");
			return NULL;
		}
		(*f)->frametype = AST_FRAME_VOICE;
#if	ASTERISK_SRC_VERSION == 104 || ASTERISK_SRC_VERSION == 106
		(*f)->subclass = AST_FORMAT_CONFERENCE;
#elif	ASTERISK_SRC_VERSION < 1300
		(*f)->subclass.integer = AST_FORMAT_CONFERENCE;
#else
		(*f)->subclass.format = ast_format_conference;
#endif
		(*f)->samples = AST_CONF_BLOCK_SAMPLES;
		(*f)->offset = AST_FRIENDLY_OFFSET;
		(*f)->datalen = AST_CONF_FRAME_DATA_SIZE;
		(*f)->src = NULL;
	}
#if	ASTERISK_SRC_VERSION == 104
	(*f)->data = data;
#else
	(*f)->data.ptr = data;
#endif
	return *f;
}

conf_frame *create_silent_frame(void)
{
	static char data[AST_CONF_BUFFER_SIZE];

	static conf_frame silent_conf_frame;

	silent_conf_frame.converted[AC_CONF] = create_slinear_frame(&silent_conf_frame.fr, data);

	return &silent_conf_frame;
}

void delete_silent_frame(conf_frame *silent_conf_frame)
{
	//free silent frames
	int i;
	for (i = 1; i < AC_SUPPORTED_FORMATS; ++i)
	{
		if (silent_conf_frame->converted[i])
		{
			ast_frfree(silent_conf_frame->converted[i]);
		}
	}

	ast_free(silent_conf_frame->fr);
}
