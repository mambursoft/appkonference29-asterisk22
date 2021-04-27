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

#ifdef	VIDEO
#include "asterisk/stream.h"
#include "pj/config_site.h"
#include "asterisk/message.h"
#include "asterisk/json.h"
#endif

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
#ifdef	VIDEO
char vsrc_data[160+AST_FRIENDLY_OFFSET] = { [AST_FRIENDLY_OFFSET] = VSRC };
#endif

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
#ifdef	VIDEO
struct ast_frame vsrc_frame = {.frametype = AST_FRAME_TEXT, .data.ptr = &vsrc_data[AST_FRIENDLY_OFFSET], .datalen = 160};
#endif
#endif

#ifdef	VIDEO
#define VIDEO_DESTINATIONS_MAX PJMEDIA_MAX_SDP_MEDIA
#define VIDEO_DESTINATION_PREFIX "conference_destination"
#define VIDEO_DESTINATION_LEN strlen(VIDEO_DESTINATION_PREFIX)
#define VIDEO_DESTINATION_SEPARATOR '_'

#define JOIN_NOTIFICATION "{\"jsonrpc\": \"2.0\", \"method\": \"joinSFU\"}"
#define JOIN_NOTIFICATION_LEN sizeof(JOIN_NOTIFICATION)

struct ast_frame joinSFU_frame = {.frametype = AST_FRAME_TEXT, .data.ptr = JOIN_NOTIFICATION, .datalen = JOIN_NOTIFICATION_LEN};

static int is_video_source(const struct ast_stream *stream)
{
	if (ast_stream_get_state(stream) != AST_STREAM_STATE_REMOVED
		&& ast_stream_get_type(stream) == AST_MEDIA_TYPE_VIDEO
		&& strncmp(ast_stream_get_name(stream), VIDEO_DESTINATION_PREFIX,
			VIDEO_DESTINATION_LEN)) {
		return 1;
	}

	return 0;
}

static int is_video_destination(const struct ast_stream *stream, const char *source_channel_name, const char *source_stream_name)
{
	char *destination_video_name;
	size_t destination_video_name_len;

	if (ast_stream_get_state(stream) == AST_STREAM_STATE_REMOVED
		|| ast_stream_get_type(stream) != AST_MEDIA_TYPE_VIDEO) {
		return 0;
	}

	destination_video_name_len = VIDEO_DESTINATION_LEN + 1;
	if (!ast_strlen_zero(source_channel_name)) {
		destination_video_name_len += strlen(source_channel_name) + 1;
		if (!ast_strlen_zero(source_stream_name)) {
			destination_video_name_len += strlen(source_stream_name) + 1;
		}

		destination_video_name = ast_alloca(destination_video_name_len);
		if (!ast_strlen_zero(source_stream_name)) {
			snprintf(destination_video_name, destination_video_name_len, "%s%c%s%c%s",
				VIDEO_DESTINATION_PREFIX, VIDEO_DESTINATION_SEPARATOR,
				source_channel_name, VIDEO_DESTINATION_SEPARATOR,
				source_stream_name);
			return !strcmp(ast_stream_get_name(stream), destination_video_name);
		}
		snprintf(destination_video_name, destination_video_name_len, "%s%c%s",
			VIDEO_DESTINATION_PREFIX, VIDEO_DESTINATION_SEPARATOR,
			source_channel_name);
	} else {
		destination_video_name = VIDEO_DESTINATION_PREFIX;
	}

	return !strncmp(ast_stream_get_name(stream), destination_video_name, destination_video_name_len - 1);
}

static void map_source_to_destinations(const char *source_stream_name, const char *source_channel_name, size_t conference_stream_position, ast_conference *conf)
{
	ast_conf_member *participant;

	AST_RWDLLIST_TRAVERSE(&conf->sfu_list, participant, sfu_entry)
	{
		int i;
		struct ast_stream_topology *topology;

		if (!strcmp(source_channel_name, ast_channel_name(participant->chan))) {
			continue;
		}

		ast_channel_lock(participant->chan);
		topology = ast_channel_get_stream_topology(participant->chan);

		for (i = 0; i < ast_stream_topology_get_count(topology); ++i) {
			struct ast_stream *stream;

			stream = ast_stream_topology_get_stream(topology, i);

			if (is_video_destination(stream, source_channel_name, source_stream_name)) {
				AST_VECTOR_REPLACE(&participant->stream_map.to_channel, conference_stream_position, i);
				break;
			}
		}
		ast_channel_unlock(participant->chan);
	}
}

static void conference_stream_topology_changed(ast_conference *conf)
{
	ast_conf_member *participant;
	int video_source_count = 0;
	
	AST_RWDLLIST_TRAVERSE(&conf->sfu_list, participant, sfu_entry)
	{
		AST_VECTOR_DEFAULT(&participant->stream_map.to_channel, VIDEO_DESTINATIONS_MAX, -1);
		AST_VECTOR_DEFAULT(&participant->stream_map.to_conference, VIDEO_DESTINATIONS_MAX, -1);
	}

	AST_RWDLLIST_TRAVERSE(&conf->sfu_list, participant, sfu_entry)
	{
		int i;
		struct ast_stream_topology *topology;

		ast_channel_lock(participant->chan);
		topology = ao2_bump(ast_channel_get_stream_topology(participant->chan));
		if (!topology) {
			ast_channel_unlock(participant->chan);
			continue;
		}

		for (i = 0; i < ast_stream_topology_get_count(topology); ++i) {
			struct ast_stream *stream = ast_stream_topology_get_stream(topology, i);

			if (is_video_source(stream)) {
				AST_VECTOR_REPLACE(&participant->stream_map.to_conference, i, video_source_count);
				map_source_to_destinations(ast_stream_get_name(stream), ast_channel_name(participant->chan),
					video_source_count, conf);
				video_source_count++;
			}
		}

		ast_stream_topology_free(topology);
		ast_channel_unlock(participant->chan);
	}
}

static int append_source_streams(struct ast_stream_topology *destination, const char *channel_name, const struct ast_stream_topology *source)
{
	int i;

	for (i = 0; i < ast_stream_topology_get_count(source); ++i) {
		struct ast_stream *stream;
		struct ast_stream *stream_clone;
		char *stream_clone_name = NULL;

		stream = ast_stream_topology_get_stream(source, i);
		if (!is_video_source(stream)) {
			continue;
		}

		if (ast_asprintf(&stream_clone_name, "%s_%s_%s", VIDEO_DESTINATION_PREFIX,
			channel_name, ast_stream_get_name(stream)) < 0) {
			return -1;
		}

		stream_clone = ast_stream_clone(stream, stream_clone_name);
		ast_free(stream_clone_name);
		if (!stream_clone) {
			return -1;
		}
		if (ast_stream_topology_append_stream(destination, stream_clone) < 0) {
			ast_stream_free(stream_clone);
			return -1;
		}
	}

	return 0;
}

static int append_all_streams(struct ast_stream_topology *destination, const struct ast_stream_topology *source)
{
	int i;
	int destination_index = 0;

	for (i = 0; i < ast_stream_topology_get_count(source); ++i) {
		struct ast_stream *clone;
		int added = 0;

		clone = ast_stream_clone(ast_stream_topology_get_stream(source, i), NULL);
		if (!clone) {
			return -1;
		}

		while (destination_index < ast_stream_topology_get_count(destination)) {
			struct ast_stream *stream = ast_stream_topology_get_stream(destination, destination_index);

			destination_index++;

			if (ast_stream_get_state(stream) == AST_STREAM_STATE_REMOVED) {
				ast_stream_topology_set_stream(destination, destination_index - 1, clone);
				added = 1;
				break;
			}
		}

		if (!added && ast_stream_topology_append_stream(destination, clone) < 0) {
			ast_stream_free(clone);
			return -1;
		}
	}

	return 0;
}

static void sfu_topologies_on_join(ast_conf_member *joiner)
{
	ast_conference *conf = joiner->conf;

	struct ast_stream_topology *joiner_video = ast_stream_topology_alloc();
	if (!joiner_video) {
		return;
	}

	ast_channel_lock(joiner->chan);
	int res = append_source_streams(joiner_video, ast_channel_name(joiner->chan), ast_channel_get_stream_topology(joiner->chan));
	joiner->topology = ast_stream_topology_clone(ast_channel_get_stream_topology(joiner->chan));
	ast_channel_unlock(joiner->chan);

	if (res || !joiner->topology) {
		goto cleanup;
	}

	ast_conf_member *participant;

	AST_RWDLLIST_TRAVERSE(&conf->sfu_list, participant, sfu_entry)
	{
		if (participant == joiner) {
			continue;
		}

		ast_channel_lock(participant->chan);
		res = append_source_streams(joiner->topology, ast_channel_name(participant->chan), ast_channel_get_stream_topology(participant->chan));
		ast_channel_unlock(participant->chan);
		if (res) {
			goto cleanup;
		}
	}

	ast_channel_request_stream_topology_change(joiner->chan, joiner->topology, NULL);

	AST_RWDLLIST_TRAVERSE(&conf->sfu_list, participant, sfu_entry)
	{
		if (participant == joiner) {
			continue;
		}

		if (append_all_streams(participant->topology, joiner_video)) {
			goto cleanup;
		}

		ast_channel_request_stream_topology_change(participant->chan, participant->topology, NULL);
	}

cleanup:
	ast_stream_topology_free(joiner_video);
}

static int remove_destination_streams(struct ast_stream_topology *topology, const char *channel_name)
{
	int i;
	int stream_removed = 0;

	for (i = 0; i < ast_stream_topology_get_count(topology); ++i) {
		struct ast_stream *stream;

		stream = ast_stream_topology_get_stream(topology, i);

		if (is_video_destination(stream, channel_name, NULL)) {
			ast_stream_set_state(stream, AST_STREAM_STATE_REMOVED);
			stream_removed = 1;
		}
	}
	return stream_removed;
}

static void sfu_topologies_on_leave(ast_conf_member *leaver)
{
	ast_conference *conf = leaver->conf;
	ast_conf_member *participant;

	AST_RWDLLIST_TRAVERSE(&conf->sfu_list, participant, sfu_entry)
	{
		if (!remove_destination_streams(participant->topology, ast_channel_name(leaver->chan))) {
			continue;
		}
		ast_channel_request_stream_topology_change(participant->chan, participant->topology, NULL);
	}

	if (remove_destination_streams(leaver->topology, "")) {
		ast_channel_request_stream_topology_change(leaver->chan, leaver->topology, NULL);
	}
}
#endif

// process an incoming frame.  Returns 0 normally, 1 if hangup was received.
static int process_incoming(ast_conf_member *member, ast_conference *conf, struct ast_frame *f)
{
	int hangup = 0;

	switch (f->frametype)
	{
		case AST_FRAME_VOICE:
		{
			if (member->mute_audio
				|| member->muted
				||  conf->membercount == 1)
			{
				break;
			}
#if	 SILDET == 2
			// reset silence detection flag
			int is_silent_frame = 0;
			//
			// make sure we have a valid dsp and frame type
			//
			if (member->dsp)
			{
				// send the frame to the preprocessor
				f = convert_frame(member->to_dsp, f, 1);
#if	ASTERISK_SRC_VERSION == 104
				if (!speex_preprocess(member->dsp, f->data, NULL))
#else
				if (!speex_preprocess(member->dsp, f->data.ptr, NULL))
#endif
				{
					//
					// we ignore the preprocessor's outcome if we've seen voice frames
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
#ifdef	VIDEO
		case AST_FRAME_VIDEO:
                {
			if (conf->video_mode == SFU)
			{
				ast_conf_member *participant;

				AST_RWDLLIST_RDLOCK(&conf->sfu_list);

				int conf_stream_num = AST_VECTOR_GET(&member->stream_map.to_conference, f->stream_num);

				if (conf_stream_num < 0)
				{
					AST_RWDLLIST_UNLOCK(&conf->sfu_list);
					break;
				}

				AST_RWDLLIST_TRAVERSE(&conf->sfu_list, participant, sfu_entry)
				{
					if (participant == member) {
						continue;
					}

					int member_stream_num = AST_VECTOR_GET(&participant->stream_map.to_channel, conf_stream_num);

					if (member_stream_num < 0)
					{
						continue;
					}

					queue_video_frame(participant, f, member_stream_num);
				}

				AST_RWDLLIST_UNLOCK(&conf->sfu_list);
			} else if (conf->video_mode == SRC)
			{
				ast_conf_member *participant;

				AST_RWDLLIST_RDLOCK(&conf->sfu_list);

				if (conf->video_source == member)
				{
					ast_write(member->chan, f);

					ast_clear_flag(f, AST_FRFLAG_HAS_TIMING_INFO);

					AST_RWDLLIST_TRAVERSE(&conf->sfu_list, participant, sfu_entry)
					{
						if (participant != member)
							queue_video_frame(participant, f, 1);
					}
				}

				AST_RWDLLIST_UNLOCK(&conf->sfu_list);
			}

			break;
                }
#endif
		case AST_FRAME_DTMF_END:
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
			switch (f->subclass)
#else
			switch (f->subclass.integer)
#endif
			{
				case AST_CONTROL_HANGUP:
				{
					hangup = 1;
					break;
				}
#ifdef	VIDEO
				case AST_CONTROL_STREAM_TOPOLOGY_CHANGED:
				{
					//ast_log(LOG_NOTICE, "%s ast_control_stream_topology_changed\n", ast_channel_name(member->chan));
					AST_RWDLLIST_WRLOCK(&conf->sfu_list);
					conference_stream_topology_changed(conf);
					AST_RWDLLIST_UNLOCK(&conf->sfu_list);
					break;
				}
				case AST_CONTROL_STREAM_TOPOLOGY_SOURCE_CHANGED:
				{
					//ast_log(LOG_NOTICE, "%s ast_control_stream_topology_source_changed\n", ast_channel_name(member->chan));
					break;
				}
				case AST_CONTROL_VIDUPDATE:
				{
					//ast_log(LOG_NOTICE, "%s ast_control_vidupdate %d\n", ast_channel_name(member->chan), conf->video_mode);
					if (conf->video_mode == SFU)
					{
						ast_conf_member *participant;

						AST_RWDLLIST_RDLOCK(&conf->sfu_list);

						AST_RWDLLIST_TRAVERSE(&conf->sfu_list, participant, sfu_entry)
						{
							if (participant == member) {
								continue;
							}

							ast_indicate(participant->chan, AST_CONTROL_VIDUPDATE);
						
						}

						AST_RWDLLIST_UNLOCK(&conf->sfu_list);
					} else if (conf->video_mode == SRC)
					{
						AST_RWDLLIST_RDLOCK(&conf->sfu_list);
						if (conf->video_source)
						{
							ast_indicate(conf->video_source->chan, AST_CONTROL_VIDUPDATE);
						}
						AST_RWDLLIST_UNLOCK(&conf->sfu_list);
					}

					break;
				}
#if	0
				case AST_CONTROL_ANSWER:
				{
					ast_log(LOG_NOTICE, "%s ast_control_answer status: %d\n", ast_channel_name(member->chan), ast_channel_state(member->chan));
					break;
				}
				case AST_CONTROL_PVT_CAUSE_CODE:
				{
					ast_log(LOG_NOTICE, "%s ast_control_pvt_cause_code status: %d data: %s\n", ast_channel_name(member->chan), ast_channel_state(member->chan), ((struct ast_control_pvt_cause_code *)f->data.ptr)->code);
					break;
				}
				case AST_CONTROL_SRCCHANGE:
				{
					ast_log(LOG_NOTICE, "%s ast_control_srcchange\n", ast_channel_name(member->chan));
					break;
				}
#endif
#endif
				default:
				{
					//ast_log(LOG_NOTICE, "%s ast_control %d ??\n", ast_channel_name(member->chan), f->subclass.integer);
					break;
				}
			}
			break;
		}
#ifdef	VIDEO
		case AST_FRAME_TEXT_DATA:
		{

			struct ast_msg_data *msg = f->data.ptr;

			struct ast_json *text = ast_json_load_buf(ast_msg_data_get_attribute(msg, AST_MSG_DATA_ATTR_BODY), strlen(ast_msg_data_get_attribute(msg, AST_MSG_DATA_ATTR_BODY)), NULL);

			if (!text)
				break;

			const char *method = ast_json_string_get(ast_json_object_get(text, "method"));
			int id = ast_json_integer_get(ast_json_object_get(text, "id"));

			if (!method || !id)
				break;

			char response[1024] = {0};
			struct ast_frame response_frame = {.frametype = AST_FRAME_TEXT, .data.ptr = response}; 

			if (!strcmp(method, "getWebSocketChannel"))
			{
				// {"jsonrpc": "2.0", "method": "getWebChannel", "params": {}, "id": 1}
				// {"jsonrpc": "2.0", "result": "PJSIP/webrtc_client-xxxxxxxx", "id": 1}

				response_frame.datalen = sprintf(response, "{\"jsonrpc\": \"2.0\", \"result\": \"%s\", \"id\": %d}", ast_channel_name(member->chan), id);

				ast_write(member->chan, &response_frame);
			}
			else if (!strcmp(method, "getSourceChannel"))
			{
				// {"jsonrpc": "2.0", "method": "getSourceChannel", "params": {"mid":"video-x"}, "id": 1}
				// {"jsonrpc": "2.0", "result": "PJSIP/webrtc_client-xxxxxxxx", "id": 1}

				struct ast_json *params = ast_json_object_get(text, "params");

				if (!params)
					break;

				const char *mid = ast_json_string_get(ast_json_object_get(params, "mid"));

				if (!mid)
					break;

				int stream_num = atoi(strchr(mid, '-')+1);

				struct ast_stream *stream = ast_stream_topology_get_stream(member->topology, stream_num);

				if (!stream)
					break;

				char source[32] = {0};
				strncpy(source, ast_stream_get_name(stream)+VIDEO_DESTINATION_LEN+1, 28);

				response_frame.datalen = sprintf(response, "{\"jsonrpc\": \"2.0\", \"result\": \"%s\", \"id\": %d}", source,  id);

				ast_write(member->chan, &response_frame);
			}

			break;
		}
#endif
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
				case PLAY:
				{
#if	ASTERISK_SRC_VERSION < 1100
					if (!ast_test_flag(member->chan, AST_FLAG_MOH))
#else
					if (!ast_test_flag(ast_channel_flags(member->chan), AST_FLAG_MOH))
#endif
					{
						member->muted = 1;

						ast_conf_soundq *newsound;
						ast_conf_soundq **q;
#if     ASTERISK_SRC_VERSION == 104
						char *sound = (char*)f->data;
#else
						char *sound = (char*)f->data.ptr;
#endif
						newsound = ast_calloc(1, sizeof(ast_conf_soundq));
						ast_copy_string(newsound->name, &sound[1], 160-2);
						for (q=&member->soundq; *q; q = &((*q)->next));
						*q = newsound;
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
						member->muted = 0;

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

					}
					break;
				}
				case HOLD:
				{
#if     ASTERISK_SRC_VERSION < 1100
					if (!ast_test_flag(member->chan, AST_FLAG_MOH))
#else
					if (!ast_test_flag(ast_channel_flags(member->chan), AST_FLAG_MOH))
#endif
					{
						member->muted = 1;
						member->ready_for_outgoing = 0;

						if (!member->norecv_audio)
						{
							// clear all sounds
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
						}

						ast_moh_start(member->chan, NULL, NULL);
					}
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
#ifdef	VIDEO
				case VSRC:
				{
					AST_RWDLLIST_WRLOCK(&conf->sfu_list);

					if (member->video_mode == SRC)
					{
						member->conf->video_source = member;
						ast_indicate(member->chan, AST_CONTROL_VIDUPDATE);
					}

					AST_RWDLLIST_UNLOCK(&conf->sfu_list);

					break;
				}
#endif
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
			break;
		}
		default:
		{
			//ast_log(LOG_NOTICE, "%s ast_frame %d ??\n", ast_channel_name(member->chan), f->frametype);
			break;
		}
	}

	// free the input frame
	ast_frfree(f);

	return hangup;
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
			if (member->sound_event)
			{
				member->sound_event = 0;
				manager_event(
					EVENT_FLAG_CONF,
					"ConferenceSoundComplete",
					"ConferenceName: %s\r\n"
					"Channel: %s\r\n"
					"Sound: %s\r\n",
					member->conf->name,
#if	ASTERISK_SRC_VERSION < 1100
					member->chan->name,
#else
					ast_channel_name(member->chan),
#endif
					toboot->name
				);
			}
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

// process outgoing frames for the channel, playing either normal conference audio,
// or requested sounds
static void process_outgoing(ast_conf_member *member)
{
	struct ast_frame *cf, *sf;
//
//	process audio
//
	while ((cf = get_outgoing_frame(member)))
	{
		// if we're playing sounds, we can just replace the frame with the
		// next sound frame, and send it instead
		if (member->soundq)
		{
			if ((sf = get_next_soundframe(member)))
			{
				// use dequeued frame delivery time
				sf->delivery = cf->delivery;

				// free voice frame
				ast_frfree(cf);

				// send sound frame
				ast_write(member->chan, sf);

				// free sound frame
				ast_frfree(sf);

				continue;
			}
		}

		// send the frame
		ast_write(member->chan, cf);

                // free voice frame
		ast_frfree(cf);
	}
#ifdef	VIDEO
//
//	process video
//
	while ((cf = get_video_frame(member)))
	{
		ast_write_stream(member->chan, cf->stream_num, cf);

		ast_frfree(cf);
	}
#endif
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
#ifdef	VIDEO
		"Video: %d\r\n"
#endif
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
#ifdef	VIDEO
		!!ast_channel_get_default_stream(member->chan, AST_MEDIA_TYPE_VIDEO),
#endif
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
#ifdef	VIDEO
	// handle video mode--join
	if (member->video_mode == SFU)
	{
		//ast_log(LOG_NOTICE, "%s sfu join\n", ast_channel_name(member->chan));
		AST_VECTOR_DEFAULT(&member->stream_map.to_channel, VIDEO_DESTINATIONS_MAX, -1);
		AST_VECTOR_DEFAULT(&member->stream_map.to_conference, VIDEO_DESTINATIONS_MAX, -1);
		AST_RWDLLIST_WRLOCK(&conf->sfu_list);
		AST_RWDLLIST_INSERT_HEAD(&conf->sfu_list, member, sfu_entry);
		sfu_topologies_on_join(member);
		AST_RWDLLIST_UNLOCK(&conf->sfu_list);
		ast_write(member->chan, &joinSFU_frame);
	} else if (member->video_mode == SRC)
	{
		//ast_log(LOG_NOTICE, "%s src join\n", ast_channel_name(member->chan));
		AST_RWDLLIST_WRLOCK(&conf->sfu_list);
		AST_RWDLLIST_INSERT_HEAD(&conf->sfu_list, member, sfu_entry);
		AST_RWDLLIST_UNLOCK(&conf->sfu_list);
	}
#endif
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

#ifdef	VIDEO
			if (!(f = ast_read_stream(chan)) || process_incoming(member, conf, f))
#else
			if (!(f = ast_read(chan)) || process_incoming(member, conf, f))
#endif
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
#ifdef	VIDEO
	// handle video mode--leave
	if (member->video_mode == SFU)
	{
		//ast_log(LOG_NOTICE, "%s sfu leave\n", ast_channel_name(member->chan));
		AST_RWDLLIST_WRLOCK(&conf->sfu_list);
		sfu_topologies_on_leave(member);
		AST_RWDLLIST_REMOVE(&conf->sfu_list, member, sfu_entry);
		AST_RWDLLIST_UNLOCK(&conf->sfu_list);
		AST_VECTOR_FREE(&member->stream_map.to_channel);
		AST_VECTOR_FREE(&member->stream_map.to_conference);
		ast_stream_topology_free(member->topology);
	} else if (member->video_mode == SRC)
	{
		//ast_log(LOG_NOTICE, "%s src leave\n", ast_channel_name(member->chan));
		AST_RWDLLIST_WRLOCK(&conf->sfu_list);
		AST_RWDLLIST_REMOVE(&conf->sfu_list, member, sfu_entry);
		if (conf->video_source == member)
			conf->video_source = 0;
		AST_RWDLLIST_UNLOCK(&conf->sfu_list);
	}
#endif
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
		score_id = last_score_id < SPEAKER_SCOREBOARD_SIZE - 1 ? ++last_score_id : 0;
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
#ifdef VIDEO
	ast_mutex_init(&member->videoq.lock);
#endif
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
#ifdef	VIDEO
		static const char arg_video[] = "video";
#endif

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
#ifdef	VIDEO
		} else if (!strncasecmp(key, arg_video, sizeof(arg_video) - 1))
		{
			if (!strncasecmp(value, "sfu", sizeof("sfu")))
				member->video_mode = SFU;
			else if (!strncasecmp(value, "src", sizeof("src")))
				member->video_mode = SRC;
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
#if	SILDET == 2
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
#if	SILDET == 2
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
#if	SILDET == 2

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
		member->to_slinear = ast_translator_build_path(AST_FORMAT_CONFERENCE, chan->readformat);
#elif	ASTERISK_SRC_VERSION < 1100
		member->to_slinear = ast_translator_build_path(&ast_format_conference, &chan->readformat);
#elif	ASTERISK_SRC_VERSION < 1300
		member->to_slinear = ast_translator_build_path(&ast_format_conference, ast_channel_readformat(chan));
#else
		member->to_slinear = ast_translator_build_path(ast_format_conference, ast_channel_readformat(chan));
#endif
	}
#else	// SILDET

#if	ASTERISK_SRC_VERSION < 1000
	member->to_slinear = ast_translator_build_path(AST_FORMAT_CONFERENCE, chan->readformat);
#elif	ASTERISK_SRC_VERSION < 1100
	member->to_slinear = ast_translator_build_path(&ast_format_conference, &chan->readformat);
#elif	ASTERISK_SRC_VERSION < 1300
	member->to_slinear = ast_translator_build_path(&ast_format_conference, ast_channel_readformat(chan));
#else
	member->to_slinear = ast_translator_build_path(ast_format_conference, ast_channel_readformat(chan));
#endif

#endif	// SILDET

#if	ASTERISK_SRC_VERSION < 1000
	member->from_slinear = ast_translator_build_path(chan->writeformat, AST_FORMAT_CONFERENCE);
#elif	ASTERISK_SRC_VERSION < 1100
	member->from_slinear = ast_translator_build_path(&chan->writeformat, &ast_format_conference);
#elif	ASTERISK_SRC_VERSION < 1300
	member->from_slinear = ast_translator_build_path(ast_channel_writeformat(chan), &ast_format_conference);
#else
	member->from_slinear = ast_translator_build_path(ast_channel_writeformat(chan), ast_format_conference);
#endif

	// index for converted_frames array
#if	ASTERISK_SRC_VERSION < 1000
	switch (chan->writeformat)
#elif	ASTERISK_SRC_VERSION < 1100
	switch (chan->writeformat.id)
#elif	ASTERISK_SRC_VERSION < 1300
	switch (ast_channel_writeformat(chan)->id)
#else
	switch (ast_format_compatibility_format2bitfield(ast_channel_writeformat(chan)))
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
#if	ASTERISK_SRC_VERSION < 1300
		case AST_FORMAT_G729A:
#else
		case AST_FORMAT_G729:
#endif
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
#if	SILDET == 2

#if	ASTERISK_SRC_VERSION < 1000
	switch (member->dsp ? AST_FORMAT_CONFERENCE : chan->readformat)
#elif	ASTERISK_SRC_VERSION < 1100
	switch (member->dsp ? AST_FORMAT_CONFERENCE : chan->readformat.id)
#elif	ASTERISK_SRC_VERSION < 1300
	switch (member->dsp ? AST_FORMAT_CONFERENCE : ast_channel_readformat(chan)->id)
#else
	switch (member->dsp ? AST_FORMAT_CONFERENCE : ast_format_compatibility_format2bitfield(ast_channel_readformat(chan)))
#endif

#else	// SILDET

#if	ASTERISK_SRC_VERSION < 1000
	switch (chan->readformat)
#elif	ASTERISK_SRC_VERSION < 1100
	switch (chan->readformat.id)
#elif	ASTERISK_SRC_VERSION < 1300
	switch (ast_channel_readformat(chan)->id)
#else
	switch (ast_format_compatibility_format2bitfield(ast_channel_readformat(chan)))
#endif

#endif	// SILDET
	{
		case AST_FORMAT_CONFERENCE:
			member->read_format = AC_CONF;
			break;

		case AST_FORMAT_ULAW:
			member->read_format = AC_ULAW;
			break;
#ifdef  AC_USE_ALAW
		case AST_FORMAT_ALAW:
			member->read_format = AC_ALAW;
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
#if	ASTERISK_SRC_VERSION < 1300
		case AST_FORMAT_G729A:
#else
		case AST_FORMAT_G729:
#endif
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
#ifdef VIDEO
	// video frames
	if ((fr = AST_LIST_FIRST(&member->videoq.frames)))
		ast_frfree(fr);
#endif
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
#if	SILDET == 2
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
	if (!(fr = ast_frdup(fr)))
	{
		ast_log(LOG_ERROR, "unable to malloc incoming ast_frame\n");
		return;
	}

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

#ifdef	VIDEO
//
// video frame functions
//

struct ast_frame* get_video_frame(ast_conf_member* member)
{
	if (member->videoq.count)
	{
		ast_mutex_lock(&member->videoq.lock);

		struct ast_frame* fr = AST_LIST_REMOVE_HEAD(&member->videoq.frames, frame_list);

		// decrement frame count
		member->videoq.count--;
		ast_mutex_unlock(&member->videoq.lock);
		return fr;
	}

	return NULL;
}

void queue_video_frame(ast_conf_member* member, struct ast_frame* fr, int stream_num)
{
	//
	// create new frame from passed data frame
	//
	if (!(fr  = ast_frdup(fr)))
	{
		ast_log(LOG_ERROR, "unable to malloc outgoing ast_frame\n");
		return;
	}

	fr->stream_num = stream_num;

	//
	// add new frame to members outgoing frame queue
	//

	ast_mutex_lock(&member->videoq.lock);

	AST_LIST_INSERT_TAIL(&member->videoq.frames, fr, frame_list);

	//
	// drop a frame if more than AST_CONF_MAX_QUEUE
	//
	if (++member->videoq.count > AST_CONF_MAX_QUEUE)
	{
		ast_frfree(AST_LIST_REMOVE_HEAD(&member->videoq.frames, frame_list));
		member->videoq.count--;
	}

	ast_mutex_unlock(&member->videoq.lock);
}
#endif

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
				qf = ast_frdup(frame->fr);

				ast_frame_adjust_volume(qf, member->listen_volume > 0 ? member->listen_volume + 1 : member->listen_volume - 1);

				// convert using the conference's translation path
				qf = convert_frame(conf->from_slinear_paths[member->write_format], qf, 1);

				queue_outgoing_frame(member, qf, conf->delivery_time);

				// free frame (the translator's copy)
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
				ast_frame_adjust_volume(frame->fr, member->listen_volume > 0 ? member->listen_volume + 1 : member->listen_volume - 1);
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

