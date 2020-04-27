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
#include "conference.h"
#include "frame.h"
#include "asterisk/utils.h"

#include "asterisk/app.h"
#include "asterisk/say.h"

#include "asterisk/musiconhold.h"

#ifdef	TIMERFD
#include <sys/timerfd.h>
#include <errno.h>
#elif	KQUEUE
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#endif

#if	ASTERISK_SRC_VERSION > 108 && ASTERISK_SRC_VERSION < 1300
struct ast_format ast_format_conference = { .id = AST_FORMAT_CONFERENCE };
struct ast_format ast_format_ulaw = { .id = AST_FORMAT_ULAW };
#ifdef  AC_USE_ALAW
struct ast_format ast_format_alaw = { .id = AST_FORMAT_ALAW };
#endif
#ifdef  AC_USE_GSM
struct ast_format ast_format_gsm = { .id = AST_FORMAT_GSM };
#endif
#ifdef  AC_USE_SPEEX
struct ast_format ast_format_speex = { .id = AST_FORMAT_SPEEX };
#endif
#ifdef  AC_USE_G729A
struct ast_format ast_format_g729a = { .id = AST_FORMAT_G729A };
#endif
#ifdef  AC_USE_G722
struct ast_format ast_format_slinear = { .id = AST_FORMAT_SLINEAR };
struct ast_format ast_format_g722 = { .id = AST_FORMAT_G722 };
#endif
#endif

//
// static variables
//

// list of current conferences
static ast_conference *conflist;

// base timestamp
static struct timeval base;

// tf variables
#ifdef	CHECK_THREAD_FREQUENCY
static int tf_count;
static int tf_expirations;
static int tf_max_expirations;
static struct timeval tf_base;
#endif

#ifdef	TIMERFD
// timer file descriptor
static int timerfd = -1;
#elif	KQUEUE
// kqueue file descriptor
static int kqueuefd = -1;
static struct kevent inqueue;
static struct kevent outqueue;
#endif

// mutex for synchronizing access to conflist
AST_MUTEX_DEFINE_STATIC(conflist_lock);

static int conference_count;

// Forward function declarations
static ast_conference* find_conf(const char* name);
static ast_conference* create_conf(char* name, ast_conf_member* member);
static void remove_conf(ast_conference **conf);
static void add_member(ast_conf_member* member, ast_conference* conf);
static void process_conference(ast_conference *conf);
#ifdef	CHECK_THREAD_FREQUENCY
static void check_frequency(void);
#endif
static int get_expirations(void);

static void destroy_conf_listheaders(void *obj)
{
	ast_conf_listheaders *listheaders = obj;
	ast_rwlock_destroy(&listheaders->lock);
}

//
// main conference function
//

static void conference_exec()
{
	// conference epoch - 20 milliseconds
	struct timeval epoch = ast_tv(0, AST_CONF_FRAME_INTERVAL * 1000);

	// set base timestamps
#ifdef	CHECK_THREAD_FREQUENCY
	base = tf_base = ast_tvnow();
	// reset tf variables
	tf_count = tf_expirations = tf_max_expirations = 0;
#else
	base = ast_tvnow();
#endif

	//
	// conference thread loop
	//

	while (42)
	{
		// update expirations
		uint64_t expirations = get_expirations();
#ifdef	CHECK_THREAD_FREQUENCY
		// update tf variables
		tf_expirations += expirations;
		if (expirations > tf_max_expirations) tf_max_expirations = expirations;
#endif
		// loop for expirations
		for (; expirations; expirations--)
		{

			//
			// process the conference list
			//

			// increment the timer base (it will be used later to timestamp outgoing frames)
			base = ast_tvadd(base, epoch);

			// get the first entry
			ast_conference *conflisthead = conflist;
			ast_conference **conf  = &conflisthead;

			while (*conf)
			{
				// acquire the conference lock
				ast_rwlock_rdlock(&(*conf)->listheaders->lock);

				//
				// check if the conference is empty and if so
				// remove it and continue to the next conference
				//

				if (!(*conf)->membercount)
				{
					int res;
					if ((res = ast_mutex_trylock(&conflist_lock)) || conflisthead != conflist)
					{
						if (!res) ast_mutex_unlock(&conflist_lock);

						ast_rwlock_unlock(&(*conf)->listheaders->lock);

						// get the next conference
						conf = &(*conf)->next;

						continue;
					}

					remove_conf(conf);

					if (!conference_count)
					{
						// release the conference list lock
						ast_mutex_unlock(&conflist_lock);
#ifdef	TIMERFD
						// close timer file
						close(timerfd);
#elif	KQUEUE
						// close kqueue file
						close(kqueuefd);
#endif
						// exit the conference thread
						pthread_exit(NULL);
					}

					// release the conference list lock
					ast_mutex_unlock(&conflist_lock);

					continue; // next conference
				}

				// update the current delivery time
				(*conf)->delivery_time = base;

				// process conference frames
				process_conference(*conf);

				// release conference lock
				ast_rwlock_unlock(&(*conf)->listheaders->lock);

				// get the next conference
				conf = &(*conf)->next;
			}
		}
#ifdef	CHECK_THREAD_FREQUENCY
		// check thread frequency
		check_frequency();
#endif
	}
}

void process_conference(ast_conference *conf)
{

	// list entry
	ast_conf_listentry *listentry;

	// reset speaker and listener count
	int speaker_count = 0;
	int listener_count = conf->membercount;

	// reset listener frame
	conf->listener_frame = NULL;

	// reset pointer lists
	conf_frame *spoken_frames = NULL;

	// loop over member list and retrieve incoming frames
	for (listentry = conf->listheaders->speakerlistheader.next; listentry != &conf->listheaders->speakerlistheader; listentry = listentry->next)
	{
		ast_conf_member *member = (ast_conf_member *)((char*)listentry - offsetof(ast_conf_member, speakerlistentry));
		member_process_spoken_frames(conf,member,&spoken_frames,
					     &listener_count, &speaker_count);
	}

	// mix incoming frames and get batch of outgoing frames
	conf_frame *send_frames = spoken_frames ? mix_frames(conf, spoken_frames, speaker_count, listener_count) : NULL;

	// loop over member list and send outgoing frames
	for (listentry = conf->listheaders->memberlistheader.next; listentry != &conf->listheaders->memberlistheader; listentry = listentry->next)
	{
		ast_conf_member *member = (ast_conf_member *)((char*)listentry - offsetof(ast_conf_member, memberlistentry));
		member_process_outgoing_frames(conf, member);
	}

	// delete send frames
	while (send_frames)
	{
		ast_conf_member *entry;
		AST_LIST_TRAVERSE(&send_frames->speaker_frame_list_head, entry, speaker_frame_list_entry)
		{
			entry->speaker_frame = NULL; // reset speaker frame
		}

		send_frames = delete_conf_frame(send_frames);
	}
}

#ifdef	CHECK_THREAD_FREQUENCY
static void check_frequency(void)
{
	// calculate tf
	if (++tf_count == AST_CONF_FRAMES_PER_SECOND)
	{
		// update current timestamp
		struct timeval tf_curr = ast_tvnow();

		// compute timestamp difference
		long tf_diff = ast_tvdiff_ms(tf_curr, tf_base);

		if (tf_diff > TF_MAX_VARIATION)
		{
			ast_log(LOG_WARNING,
				"processed frame frequency variation, tf_diff = %ld, tf_expirations = %d tf_max_expirations = %d\n",
				tf_diff, tf_expirations, tf_max_expirations);
		}

		// update base
		tf_base = tf_curr;

		// reset tf values
		tf_count = tf_expirations = tf_max_expirations = 0;
	}
}
#endif

static int get_expirations(void)
{
#ifdef	TIMERFD
		uint64_t expirations;
		// wait for start of epoch
		if (read(timerfd, &expirations, sizeof(expirations)) == -1)
		{
			ast_log(LOG_ERROR, "unable to read timer!? %s\n", strerror(errno));
		}
		return expirations;
#elif	KQUEUE
		// wait for start of epoch
		if (kevent(kqueuefd, &inqueue, 1, &outqueue, 1, NULL) == -1)
		{
			ast_log(LOG_NOTICE, "unable to read timer!? %s\n", strerror(errno));
		}
		return outqueue.data;
#else
		// update the current timestamp
		struct timeval curr = ast_tvnow();

		// calculate difference in timestamps
		long time_diff = ast_tvdiff_ms(curr, base);

		// calculate time we should sleep
		long time_sleep = AST_CONF_FRAME_INTERVAL - time_diff;

		if (time_sleep > 0)
		{
			// sleep for time_sleep (as microseconds)
			usleep(time_sleep * 1000);
			return 1;
		}
		else
		{
			return time_diff / AST_CONF_FRAME_INTERVAL;
		}
#endif
}

//
// conference functions
//

// called by app_conference.c:load_module()
int init_conference(void)
{
	// init silent frame
	silent_conf_frame = create_silent_frame();

	int i;
	//init channel entries
	for (i = 0; i < CHANNEL_TABLE_SIZE; i++)
		AST_LIST_HEAD_INIT (&channel_table[i]);

	//init conference entries
	for (i = 0; i < CONFERENCE_TABLE_SIZE; i++)
		AST_LIST_HEAD_INIT (&conference_table[i]);

	//set delimiter
	argument_delimiter = !strcmp(PACKAGE_VERSION,"1.4") ? "|" : ",";

#if	defined(SPEAKER_SCOREBOARD) && defined(CACHE_CONTROL_BLOCKS)
	//init speaker scoreboard
	int fd;
	if ((fd = open(SPEAKER_SCOREBOARD_FILE,O_CREAT|O_TRUNC|O_RDWR,0644)) > -1)
	{
		if ((ftruncate(fd, SPEAKER_SCOREBOARD_SIZE)) == -1)
		{
			ast_log(LOG_ERROR, "unable to truncate scoreboard file!?\n");
			close(fd);
			return -1;
		}

		if ((speaker_scoreboard = (char*)mmap(NULL, SPEAKER_SCOREBOARD_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED)
		{
			ast_log(LOG_ERROR,"unable to mmap speaker scoreboard!?\n");
			close(fd);
			return -1;
		}

		close(fd);
	}
	else
	{
		ast_log(LOG_ERROR, "unable to open scoreboard file!?\n");
		return -1;
	}
#endif
	return 0;
}


// called by app_conference.c:unload_module()
void dealloc_conference(void)
{
	// delete silent frame
	delete_silent_frame(silent_conf_frame);

	int i;
	//destroy channel entires
	for (i = 0; i < CHANNEL_TABLE_SIZE; i++)
		AST_LIST_HEAD_DESTROY(&channel_table[i]);

	//destroy conference entries
	for (i = 0; i < CONFERENCE_TABLE_SIZE; i++)
		AST_LIST_HEAD_DESTROY(&conference_table[i]);

#ifdef	CACHE_CONTROL_BLOCKS
	//free conference blocks
	ast_conference *confblock;
	while (!AST_LIST_EMPTY(&confBlockList))
	{
		confblock = AST_LIST_REMOVE_HEAD(&confBlockList, free_list);
		ast_free(confblock);
	}
	
	//free member blocks
	ast_conf_member *mbrblock;
	while (!AST_LIST_EMPTY(&mbrBlockList))
	{
		mbrblock = AST_LIST_REMOVE_HEAD(&mbrBlockList, free_list);
		ast_free(mbrblock);
	}
#endif

#ifdef	CACHE_CONF_FRAMES
	//free conf frames
	conf_frame *cfr;
	while ((cfr = AST_LIST_REMOVE_HEAD(&confFrameList, free_list)))
	{
		ast_free(cfr);
	}
#endif

#if	defined(SPEAKER_SCOREBOARD) && defined(CACHE_CONTROL_BLOCKS)
	if (speaker_scoreboard > 0)
		munmap(speaker_scoreboard, SPEAKER_SCOREBOARD_SIZE);
#endif
}

ast_conference* join_conference(ast_conf_member* member, char* conf_name, int max_users)
{
	ast_conference* conf = NULL;

	// acquire the conference list lock
	ast_mutex_lock(&conflist_lock);



	// look for an existing conference
	conf = find_conf(conf_name);

	// unable to find an existing conference, try to create one
	if (!conf)
	{
		// create the new conference with one member
		conf = create_conf(conf_name, member);

		// return an error if create_conf() failed
		// otherwise set the member's pointer to its conference
		if (!conf)
			ast_log(LOG_ERROR, "unable to find or create requested conference\n");
	}
	else
	{
		//
		// existing conference found, add new member to the conference
		//
		// once we call add_member(), this thread
		// is responsible for calling delete_member()
		//
		if (!max_users || (max_users > conf->membercount)) {
			add_member(member, conf);
		} else {
			pbx_builtin_setvar_helper(member->chan, "KONFERENCE", "MAXUSERS");
			conf = NULL;
		}
	}

	// release the conference list lock
	ast_mutex_unlock(&conflist_lock);

	return conf;
}

// This function should be called with conflist_lock mutex being held
static ast_conference* find_conf(const char* name)
{
	ast_conference *conf;
	struct conference_bucket *bucket = &(conference_table[hash(name) % CONFERENCE_TABLE_SIZE]);

	AST_LIST_LOCK(bucket);

	AST_LIST_TRAVERSE(bucket, conf, hash_entry)
		if (!strcmp(conf->name, name)) {
			break;
		}

	AST_LIST_UNLOCK(bucket);

	return conf;
}

// This function should be called with conflist_lock held
static ast_conference* create_conf(char* name, ast_conf_member* member)
{
	//
	// allocate memory for conference
	//

	ast_conference *conf;

#ifdef	CACHE_CONTROL_BLOCKS
	if (!AST_LIST_EMPTY(&confBlockList))
	{
		// get conference control block from the free list
		conf  = AST_LIST_REMOVE_HEAD(&confBlockList, free_list);
		memset(conf,0,sizeof(ast_conference));
	}
	else
	{
#endif
		// allocate new conference control block
		if (!(conf = ast_calloc(1, sizeof(ast_conference))))
		{
			ast_log(LOG_ERROR, "unable to malloc ast_conference\n");
			return NULL;
		}
#ifdef	CACHE_CONTROL_BLOCKS
	}
#endif

	//
	// initialize conference
	//

	// record start time
	conf->time_entered = ast_tvnow();

	// copy name to conference
	strncpy((char*)&(conf->name), name, sizeof(conf->name) - 1);

	// initialize the conference data
	conf->listheaders = ao2_alloc(sizeof(ast_conf_listheaders), destroy_conf_listheaders);
	ast_rwlock_init(&conf->listheaders->lock);
	init_listheader(&conf->listheaders->speakerlistheader);
	init_listheader(&conf->listheaders->memberlistheader);

	// build translation paths
	conf->from_slinear_paths[AC_CONF] = NULL;

#ifdef	AC_USE_ULAW
#if	ASTERISK_SRC_VERSION < 1000
	conf->from_slinear_paths[AC_ULAW] = ast_translator_build_path(AST_FORMAT_ULAW, AST_FORMAT_CONFERENCE);
#elif	ASTERISK_SRC_VERSION < 1300
	conf->from_slinear_paths[AC_ULAW] = ast_translator_build_path(&ast_format_ulaw, &ast_format_conference);
#else
	conf->from_slinear_paths[AC_ULAW] = ast_translator_build_path(ast_format_ulaw, ast_format_conference);
#endif
#endif


#ifdef	AC_USE_ALAW
#if	ASTERISK_SRC_VERSION < 1000
	conf->from_slinear_paths[AC_ALAW] = ast_translator_build_path(AST_FORMAT_ALAW, AST_FORMAT_CONFERENCE);
#elif	ASTERISK_SRC_VERSION < 1300
	conf->from_slinear_paths[AC_ALAW] = ast_translator_build_path(&ast_format_alaw, &ast_format_conference);
#else
	conf->from_slinear_paths[AC_ALAW] = ast_translator_build_path(ast_format_alaw, ast_format_conference);
#endif
#endif

#ifdef	AC_USE_GSM
#if	ASTERISK_SRC_VERSION < 1000
	conf->from_slinear_paths[AC_GSM] = ast_translator_build_path(AST_FORMAT_GSM, AST_FORMAT_CONFERENCE);
#elif	ASTERISK_SRC_VERSION < 1300
	conf->from_slinear_paths[AC_GSM] = ast_translator_build_path(&ast_format_gsm, &ast_format_conference);
#else
	conf->from_slinear_paths[AC_GSM] = ast_translator_build_path(ast_format_gsm, ast_format_conference);
#endif
#endif

#ifdef	AC_USE_SPEEX
#if	ASTERISK_SRC_VERSION < 1000
	conf->from_slinear_paths[AC_SPEEX] = ast_translator_build_path(AST_FORMAT_SPEEX, AST_FORMAT_CONFERENCE);
#elif	ASTERISK_SRC_VERSION < 1300
	conf->from_slinear_paths[AC_SPEEX] = ast_translator_build_path(&ast_format_speex, &ast_format_conference);
#else
	conf->from_slinear_paths[AC_SPEEX] = ast_translator_build_path(ast_format_speex, ast_format_conference);
#endif
#endif

#ifdef AC_USE_G729A
#if	ASTERISK_SRC_VERSION < 1000
	conf->from_slinear_paths[AC_G729A] = ast_translator_build_path(AST_FORMAT_G729A, AST_FORMAT_CONFERENCE);
#elif	ASTERISK_SRC_VERSION < 1300
	conf->from_slinear_paths[AC_G729A] = ast_translator_build_path(&ast_format_g729a, &ast_format_conference);
#else
	conf->from_slinear_paths[AC_G729A] = ast_translator_build_path(ast_format_g729, ast_format_conference);
#endif
#endif

#ifdef AC_USE_G722
#if	ASTERISK_SRC_VERSION < 1000
	conf->from_slinear_paths[AC_SLINEAR] = ast_translator_build_path(AST_FORMAT_SLINEAR, AST_FORMAT_CONFERENCE);
	conf->from_slinear_paths[AC_G722] = ast_translator_build_path(AST_FORMAT_G722, AST_FORMAT_CONFERENCE);
#elif	ASTERISK_SRC_VERSION < 1300
	conf->from_slinear_paths[AC_SLINEAR] = ast_translator_build_path(&ast_format_slinear, &ast_format_conference);
	conf->from_slinear_paths[AC_G722] = ast_translator_build_path(&ast_format_g722, &ast_format_conference);
#else
	conf->from_slinear_paths[AC_SLINEAR] = ast_translator_build_path(ast_format_slin, ast_format_conference);
	conf->from_slinear_paths[AC_G722] = ast_translator_build_path(ast_format_g722, ast_format_conference);
#endif
#endif

	//
	// spawn thread for new conference, using conference_exec(conf)
	//
	if (!conflist)
	{

#ifdef	TIMERFD
		// create timer
		if ((timerfd = timerfd_create(CLOCK_MONOTONIC,TFD_CLOEXEC)) == -1)
		{
			ast_log(LOG_ERROR, "unable to create timer!? %s\n", strerror(errno));

			// clean up conference
			ast_free(conf);
			return NULL;
		}

		// set interval to epoch
		struct itimerspec timerspec = { .it_interval.tv_sec = 0,
						.it_interval.tv_nsec = AST_CONF_FRAME_INTERVAL * 1000000,
						.it_value.tv_sec = 0,
						.it_value.tv_nsec = 1 };

		// set timer
		if (timerfd_settime(timerfd, 0, &timerspec, 0) == -1)
		{
			ast_log(LOG_NOTICE, "unable to set timer!? %s\n", strerror(errno));

			close(timerfd);

			// clean up conference
			ast_free(conf);
			return NULL;
		}
#elif	KQUEUE
		// create timer
		if ((kqueuefd = kqueue()) == -1)
		{
			ast_log(LOG_ERROR, "unable to create timer!? %s\n", strerror(errno));

			// clean up conference
			ast_free(conf);
			return NULL;
		}

		// set interval to epoch
		EV_SET(&inqueue, 1, EVFILT_TIMER, EV_ADD | EV_ENABLE, 0, AST_CONF_FRAME_INTERVAL, 0);
#endif

		pthread_t conference_thread; // conference thread id
		if (!(ast_pthread_create(&conference_thread, NULL, (void*)conference_exec, NULL)))
		{
			// detach the thread so it doesn't leak
			pthread_detach(conference_thread);

			// if realtime set fifo scheduling and bump priority
			if (ast_opt_high_priority)
			{
				int policy;
				struct sched_param param;

				pthread_getschedparam(conference_thread, &policy, &param);
				
				++param.sched_priority;
				policy = SCHED_FIFO;
				pthread_setschedparam(conference_thread, policy, &param);
			}
		}
		else
		{
			ast_log(LOG_ERROR, "unable to start conference thread for conference %s\n", conf->name);

			// clean up conference
			ast_free(conf);
			return NULL;
		}
	}

	// add the initial member
	add_member(member, conf);

	// add member to channel table
	conf->bucket = &(conference_table[hash(conf->name) % CONFERENCE_TABLE_SIZE]);

	AST_LIST_LOCK(conf->bucket);
	AST_LIST_INSERT_HEAD(conf->bucket, conf, hash_entry);
	AST_LIST_UNLOCK(conf->bucket);

	// count new conference
	++conference_count;

	// add conference to conflist
	conf->next = conflist;
	conflist = conf;
#ifdef	VIDEO
	// init sfu list
	AST_RWDLLIST_HEAD_INIT(&conf->sfu_list);
#endif
	return conf;
}

//This function should be called with conflist_lock and conf->lock held
void remove_conf(ast_conference **conf)
{
	//
	// do some frame clean up
	//
	int c;
	for (c = 0; c < AC_SUPPORTED_FORMATS; ++c)
	{
		// free the translation paths
		if ((*conf)->from_slinear_paths[c])
		{
			ast_translator_free_path((*conf)->from_slinear_paths[c]);
		}
	}

	// speaker frames
	if ((*conf)->mixAstFrame)
	{
		ast_free((*conf)->mixAstFrame);
	}
	if ((*conf)->mixConfFrame)
	{
		ast_free((*conf)->mixConfFrame);
	}

	AST_LIST_LOCK((*conf)->bucket);
	AST_LIST_REMOVE((*conf)->bucket, *conf, hash_entry);
	AST_LIST_UNLOCK((*conf)->bucket);

	// unlock and destroy read/write lock
	ast_rwlock_unlock(&(*conf)->listheaders->lock);
	
	ao2_ref((*conf)->listheaders, -1);

	ast_conference *conf_temp = *conf;
	*conf = conf_temp->next;

	if (conf_temp == conflist)
		conflist = *conf;

	// update conference count
	--conference_count;

#ifdef	CACHE_CONTROL_BLOCKS
	// put the conference control block on the free list
	AST_LIST_INSERT_HEAD(&confBlockList, conf_temp, free_list);
#else
	ast_free(conf_temp);	
#endif
}

void end_conference(const char *name)
{
	ast_conference *conf;

	// acquire the conference list lock
	ast_mutex_lock(&conflist_lock);

	if ((conf = find_conf(name)))
	{
		// acquire the conference lock
		ast_rwlock_rdlock(&conf->listheaders->lock);

		// loop over member list and request hangup
		ast_conf_listentry *listentry;
		for (listentry = conf->listheaders->memberlistheader.next; listentry != &conf->listheaders->memberlistheader; listentry = listentry->next)
		{
			ast_conf_member *member = (ast_conf_member *)((char*)listentry - offsetof(ast_conf_member, memberlistentry));

			ast_queue_frame(member->chan, &kick_frame);
		}

		// release the conference lock
		ast_rwlock_unlock(&conf->listheaders->lock);
	}

	// release the conference list lock
	ast_mutex_unlock(&conflist_lock);
}

//
// member-related functions
//

// This function should be called with conflist_lock held
static void add_member(ast_conf_member *member, ast_conference *conf)
{
	// acquire the conference lock
	ast_rwlock_wrlock(&conf->listheaders->lock);

	// if spying, setup spyer/spyee
	if (member->spyee_channel_name)
	{
		ast_conf_member *spyee = find_member(member->spyee_channel_name);

		if (spyee)
		{
			if (!spyee->spyee_channel_name && spyee != member && spyee->conf == conf)
			{
				AST_LIST_INSERT_HEAD(&spyee->spy_list.head, member, spy_list.entry);
				AST_LIST_INSERT_HEAD(&member->spy_list.head, spyee, spy_list.entry);
			}

			if (!--spyee->use_count && spyee->delete_flag)
				ast_cond_signal(&spyee->delete_var);
			ast_mutex_unlock(&spyee->lock);
		}
	}

	// update moderator count
	if (member->ismoderator)
		conf->moderators++;

	// update conference count
	// calculate member identifier
	if (!conf->membercount++)
	{
		member->conf_id = 1;
	}
	else
	{
		ast_conf_member *memberlast = (ast_conf_member *)((char *)conf->listheaders->memberlistheader.next - offsetof(ast_conf_member, memberlistentry));
		member->conf_id = memberlast->conf_id + 1;
	}
	
	// add to member list
	add_listentry(&conf->listheaders->memberlistheader, &member->memberlistentry);

	// add member to speaker list
	if (!member->mute_audio)
		add_listentry(&conf->listheaders->speakerlistheader, &member->speakerlistentry);

	// set pointer to conference
	member->conf = conf;

#ifdef	VIDEO
	// validate video mode
	if (!conf->video_mode && member->video_mode)
	{
		conf->video_mode = member->video_mode;
	} else if (member->video_mode && conf->video_mode != member->video_mode)
	{
		member->video_mode = 0;
	}
#endif
	// release the conference lock
	ast_rwlock_unlock(&conf->listheaders->lock);
}

void remove_member(ast_conf_member* member, ast_conference* conf, char* conf_name)
{
	int membercount;
	int moderators;

	ast_rwlock_wrlock(&conf->listheaders->lock);

	// remove from member list
	remove_listentry(&member->memberlistentry);

	// update member count
	membercount = --conf->membercount;

	// remove from speaker list
	if (!member->mute_audio)
		remove_listentry(&member->speakerlistentry);

	// update moderator count
	moderators = !member->ismoderator ? conf->moderators : --conf->moderators;

	// if this the last moderator and the flag is set then kick the rest
	if (member->ismoderator && member->kick_conferees && !conf->moderators)
	{
		ast_conf_listentry *listentry;
		for (listentry = conf->listheaders->memberlistheader.next; listentry != &conf->listheaders->memberlistheader; listentry = listentry->next)
		{
			ast_conf_member *member = (ast_conf_member *)((char*)listentry - offsetof(ast_conf_member, memberlistentry));
			ast_queue_frame(member->chan, &kick_frame);
		}
	}

	// if spying sever connection to spyee
	if (!AST_LIST_EMPTY(&member->spy_list.head))
	{
		ast_conf_member *entry;
		if (member->spyee_channel_name)
		{
			ast_conf_member *spyee = AST_LIST_REMOVE_HEAD(&member->spy_list.head, spy_list.entry);
			AST_LIST_TRAVERSE_SAFE_BEGIN(&spyee->spy_list.head, entry, spy_list.entry)
			{
				if (member == entry)
				{
#if	ASTERISK_SRC_VERSION == 104
					AST_LIST_REMOVE_CURRENT(&spyee->spy_list.head, spy_list.entry);
#else
					AST_LIST_REMOVE_CURRENT(spy_list.entry);
#endif
					break;
				}
			}
			AST_LIST_TRAVERSE_SAFE_END
		}
		else
		{
			AST_LIST_TRAVERSE_SAFE_BEGIN(&member->spy_list.head, entry, spy_list.entry)
			{
				AST_LIST_REMOVE_HEAD(&entry->spy_list.head, spy_list.entry);
				ast_queue_frame(entry->chan, &kick_frame);
#if	ASTERISK_SRC_VERSION == 104
				AST_LIST_REMOVE_CURRENT(&member->spy_list.head, spy_list.entry);
#else
				AST_LIST_REMOVE_CURRENT(spy_list.entry);
#endif
			}
			AST_LIST_TRAVERSE_SAFE_END
		}
	}

	ast_rwlock_unlock(&conf->listheaders->lock);

	// remove member from channel table
	if (member->bucket)
	{
		AST_LIST_LOCK(member->bucket);
		AST_LIST_REMOVE(member->bucket, member, hash_entry);
		AST_LIST_UNLOCK(member->bucket);
	}

	// output to manager...
	manager_event(
		EVENT_FLAG_CONF,
		"ConferenceLeave",
		"ConferenceName: %s\r\n"
		"Type:  %s\r\n"
		"UniqueID: %s\r\n"
		"Member: %d\r\n"
		"Flags: %s\r\n"
		"Channel: %s\r\n"
		"CallerID: %s\r\n"
		"CallerIDName: %s\r\n"
		"Duration: %ld\r\n"
		"Moderators: %d\r\n"
		"Count: %d\r\n",
		conf_name,
		member->type,
#if	ASTERISK_SRC_VERSION < 1100
		member->chan->uniqueid,
#else
		ast_channel_uniqueid(member->chan),
#endif
		member->conf_id,
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
		(long)ast_tvdiff_ms(ast_tvnow(),member->time_entered) / 1000,
		moderators,
		membercount
	);

	// delete the member
	delete_member(member);

}

void list_conferences(int fd)
{
	int duration;
	char duration_str[10];

        // any conferences?
	if (conflist)
	{

		// acquire mutex
		ast_mutex_lock(&conflist_lock);

		ast_conference *conf = conflist;

		ast_cli(fd, "%-20.20s %-20.20s %-20.20s %-20.20s\n", "Name", "Members", "Volume", "Duration");

		// loop through conf list
		while (conf)
		{
			duration = (int)(ast_tvdiff_ms(ast_tvnow(),conf->time_entered) / 1000);
			snprintf(duration_str, 10, "%02u:%02u:%02u",  duration / 3600, (duration % 3600) / 60, duration % 60);
			ast_cli(fd, "%-20.20s %-20d %-20d %-20.20s\n", conf->name, conf->membercount, conf->volume, duration_str);
			conf = conf->next;
		}

		// release mutex
		ast_mutex_unlock(&conflist_lock);
	}
}

void list_members(int fd, const char *name)
{
	ast_conf_member *member;
	ast_conf_listentry *listentry;
	char volume_str[10];
	char spy_str[10];
	int duration;
	char duration_str[10];

        // any conferences?
	if (conflist)
	{

		// acquire mutex
		ast_mutex_lock(&conflist_lock);

		ast_conference *conf = conflist;

		// loop through conf list
		while (conf)
		{
			if (!strcasecmp((const char*)&(conf->name), name))
			{
				// acquire conference lock
				ast_rwlock_rdlock(&conf->listheaders->lock);

				// print the header
				ast_cli(fd, "%s:\n%-20.20s %-20.20s %-20.20s %-20.20s %-20.20s %-20.20s %-80.20s\n", conf->name, "User #", "Flags", "Audio", "Volume", "Duration", "Spy", "Channel");
				// do the biz
				for (listentry = conf->listheaders->memberlistheader.prev; listentry != &conf->listheaders->memberlistheader; listentry = listentry->prev)
				{
					member = (ast_conf_member *)((char*)listentry - offsetof(ast_conf_member, memberlistentry));
					snprintf(volume_str, 10, "%d:%d", member->talk_volume, member->listen_volume);
					if (member->spyee_channel_name && !AST_LIST_EMPTY(&member->spy_list.head))
						snprintf(spy_str, 10, "%d", AST_LIST_FIRST(&member->spy_list.head)->conf_id);
					else
						strcpy(spy_str , "*");
					duration = (int)(ast_tvdiff_ms(ast_tvnow(),member->time_entered) / 1000);
					snprintf(duration_str, 10, "%02u:%02u:%02u",  duration / 3600, (duration % 3600) / 60, duration % 60);
					ast_cli(fd, "%-20d %-20.20s %-20.20s %-20.20s %-20.20s %-20.20s %-80s\n",
#if	ASTERISK_SRC_VERSION < 1100
					member->conf_id, member->flags, !member->mute_audio ? "Unmuted" : "Muted", volume_str, duration_str , spy_str, member->chan->name);
#else
					member->conf_id, member->flags, !member->mute_audio ? "Unmuted" : "Muted", volume_str, duration_str , spy_str, ast_channel_name(member->chan));
#endif
				}

				// release conference lock
				ast_rwlock_unlock(&conf->listheaders->lock);

				break;
			}

			conf = conf->next;
		}

		// release mutex
		ast_mutex_unlock(&conflist_lock);
	}
}

void list_all(int fd)
{
	ast_conf_member *member;
	ast_conf_listentry *listentry;
	char volume_str[10];
	char spy_str[10];
	int duration;
	char duration_str[10];

        // any conferences?
	if (conflist)
	{
		// acquire mutex
		ast_mutex_lock(&conflist_lock);

		ast_conference *conf = conflist;

		// loop through conf list
		while (conf)
		{
			// acquire conference lock
			ast_rwlock_rdlock(&conf->listheaders->lock);

			// print the header
			ast_cli(fd, "%s:\n%-20.20s %-20.20s %-20.20s %-20.20s %-20.20s %-20.20s %-80.20s\n", conf->name, "User #", "Flags", "Audio", "Volume", "Duration", "Spy", "Channel");
			// do the biz
			for (listentry = conf->listheaders->memberlistheader.prev; listentry != &conf->listheaders->memberlistheader; listentry = listentry->prev)
			{
				member = (ast_conf_member *)((char*)listentry - offsetof(ast_conf_member, memberlistentry));
				snprintf(volume_str, 10, "%d:%d", member->talk_volume, member->listen_volume);
				if (member->spyee_channel_name && !AST_LIST_EMPTY(&member->spy_list.head))
					snprintf(spy_str, 10, "%d", AST_LIST_FIRST(&member->spy_list.head)->conf_id);
				else
					strcpy(spy_str , "*");
				duration = (int)(ast_tvdiff_ms(ast_tvnow(),member->time_entered) / 1000);
				snprintf(duration_str, 10, "%02u:%02u:%02u",  duration / 3600, (duration % 3600) / 60, duration % 60);
				ast_cli(fd, "%-20d %-20.20s %-20.20s %-20.20s %-20.20s %-20.20s %-80s\n",
#if	ASTERISK_SRC_VERSION < 1100
				member->conf_id, member->flags, !member->mute_audio ? "Unmuted" : "Muted", volume_str, duration_str , spy_str, member->chan->name);
#else
				member->conf_id, member->flags, !member->mute_audio ? "Unmuted" : "Muted", volume_str, duration_str , spy_str, ast_channel_name(member->chan));
#endif
			}

			// release conference lock
			ast_rwlock_unlock(&conf->listheaders->lock);

			conf = conf->next;
		}

		// release mutex
		ast_mutex_unlock(&conflist_lock);
	}
}

void kick_all(void)
{
        // any conferences?
	if (conflist)
	{
		// acquire mutex
		ast_mutex_lock(&conflist_lock);

		ast_conference *conf = conflist;

		// loop through conf list
		while (conf)
		{
  			ast_conf_listentry *listentry;
			// do the biz
			ast_rwlock_rdlock(&conf->listheaders->lock);
			for (listentry = conf->listheaders->memberlistheader.next; listentry != &conf->listheaders->memberlistheader; listentry = listentry->next)
			{
				ast_conf_member *member = (ast_conf_member *)((char*)listentry - offsetof(ast_conf_member, memberlistentry));
				ast_queue_frame(member->chan, &kick_frame);
			}
			ast_rwlock_unlock(&conf->listheaders->lock);

			conf = conf->next;
		}

		// release mutex
		ast_mutex_unlock(&conflist_lock);
	}

}

void mute_conference(const char* confname)
{
        // any conferences?
	if (conflist)
	{
		// acquire mutex
		ast_mutex_lock(&conflist_lock);

		ast_conference *conf;

		// get conference
		if  ((conf = find_conf(confname)))
		{
			ast_conf_listentry *listentry;
			// do the biz
			ast_rwlock_rdlock(&conf->listheaders->lock);
			for (listentry = conf->listheaders->memberlistheader.next; listentry != &conf->listheaders->memberlistheader; listentry = listentry->next)
			  {
			  	ast_conf_member *member = (ast_conf_member *)((char*)listentry - offsetof(ast_conf_member, memberlistentry));
			    if (!member->ismoderator)
			      {
#if	defined(SPEAKER_SCOREBOARD) && defined(CACHE_CONTROL_BLOCKS)
					*(speaker_scoreboard + member->score_id) = '\x00';
#endif
				      if (!member->mute_audio)
				      {
					member->mute_audio = 1;
					remove_listentry(&member->speakerlistentry);
					member->is_speaking = 0;
					}
			      }
			  }
			ast_rwlock_unlock(&conf->listheaders->lock);
		}

		// release mutex
		ast_mutex_unlock(&conflist_lock);

		manager_event(
			EVENT_FLAG_CONF,
			"ConferenceMute",
			"ConferenceName: %s\r\n",
			confname
		);
	}

}

void unmute_conference(const char* confname)
{
        // any conferences?
	if (conflist)
	{
		// acquire mutex
		ast_mutex_lock(&conflist_lock);

		ast_conference *conf;

		// get conference
		if ((conf = find_conf(confname)))
		{
			ast_conf_listentry *listentry;
			// do the biz
			ast_rwlock_rdlock(&conf->listheaders->lock);
			for (listentry = conf->listheaders->memberlistheader.next; listentry != &conf->listheaders->memberlistheader; listentry = listentry->next)
			  {
			  	ast_conf_member *member = (ast_conf_member *)((char*)listentry - offsetof(ast_conf_member, memberlistentry));
			    if (!member->ismoderator)
			      {
			      	if (member->mute_audio)
				{
					member->mute_audio = 0;
					add_listentry(&conf->listheaders->speakerlistheader, &member->speakerlistentry);
				}
			      }
			  }
			ast_rwlock_unlock(&conf->listheaders->lock);
		}

		// release mutex
		ast_mutex_unlock(&conflist_lock);

		manager_event(
			EVENT_FLAG_CONF,
			"ConferenceUnmute",
			"ConferenceName: %s\r\n",
			confname
		);
	}
}

ast_conf_member *find_member(const char *chan)
{
	ast_conf_member *member;
	struct channel_bucket *bucket = &(channel_table[hash(chan) % CHANNEL_TABLE_SIZE]);

	AST_LIST_LOCK(bucket);

	AST_LIST_TRAVERSE(bucket, member, hash_entry)
#if	ASTERISK_SRC_VERSION < 1100
		if (!strcmp(member->chan->name, chan)) {
#else
		if (!strcmp(ast_channel_name(member->chan), chan)) {
#endif
			ast_mutex_lock(&member->lock);
			member->use_count++;

			break;
		}

	AST_LIST_UNLOCK(bucket);

	return member;
}

#if	ASTERISK_SRC_VERSION == 104 || ASTERISK_SRC_VERSION == 106
void play_sound_channel(const char *channel, char **file, int mute, int tone, int evnt, int n)
#else
void play_sound_channel(const char *channel, const char * const *file, int mute, int tone, int evnt, int n)
#endif
{
	ast_conf_member *member;

	if ((member = find_member(channel)))
	{
		if (!member->norecv_audio && (!tone || !member->soundq))
		{
			while (n-- > 0) {

				char play_data[160+AST_FRIENDLY_OFFSET] = { [AST_FRIENDLY_OFFSET] = PLAY };
#if     ASTERISK_SRC_VERSION == 104
				struct ast_frame play_frame = {.frametype = AST_FRAME_TEXT, .data = &play_data[AST_FRIENDLY_OFFSET], .datalen = 160};
#else
				struct ast_frame play_frame = {.frametype = AST_FRAME_TEXT, .data.ptr = &play_data[AST_FRIENDLY_OFFSET], .datalen = 160};
#endif
				ast_copy_string(&play_data[AST_FRIENDLY_OFFSET+1], *file, 160-2);
				ast_queue_frame(member->chan, &play_frame);

				file++;
			}

			member->muted = mute;

			member->sound_event = evnt;

		}
		if (!--member->use_count && member->delete_flag)
			ast_cond_signal(&member->delete_var);
		ast_mutex_unlock(&member->lock);
	}
}

void stop_sound_channel(const char *channel)
{
	ast_conf_member *member;

	if ((member = find_member(channel)))
	{
		if (!member->norecv_audio)
		{
			ast_queue_frame(member->chan, &stop_frame);

		}

		if (!--member->use_count && member->delete_flag)
			ast_cond_signal(&member->delete_var);
		ast_mutex_unlock(&member->lock);
	}
}

void start_moh_channel(const char *channel)
{
	ast_conf_member *member;

	if ((member = find_member(channel)))
	{
		if (!member->norecv_audio)
		{
			ast_queue_frame(member->chan, &hold_frame);
		}

		if (!--member->use_count && member->delete_flag)
			ast_cond_signal(&member->delete_var);
		ast_mutex_unlock(&member->lock);
	}
}

void stop_moh_channel(const char *channel)
{
	ast_conf_member *member;

	if ((member = find_member(channel)))
	{
		if (!member->norecv_audio)
		{
			ast_queue_frame(member->chan, &cont_frame);
		}

		if (!--member->use_count && member->delete_flag)
			ast_cond_signal(&member->delete_var);
		ast_mutex_unlock(&member->lock);
	}
}

void adjust_talk_volume_channel(const char *channel, int up)
{
	ast_conf_member *member;

	if ((member = find_member(channel)))
	{
		up ? member->talk_volume++ : member->talk_volume--;

		if (!--member->use_count && member->delete_flag)
			ast_cond_signal(&member->delete_var);
		ast_mutex_unlock(&member->lock);
	}
}

void adjust_listen_volume_channel(const char *channel, int up)
{
	ast_conf_member *member;

	if ((member = find_member(channel)))
	{
		up ? member->listen_volume++ : member->listen_volume--;

		if (!--member->use_count && member->delete_flag)
			ast_cond_signal(&member->delete_var);
		ast_mutex_unlock(&member->lock);
	}
}

void adjust_volume_conference(const char *conference, int up)
{
	ast_conference *conf;

	// acquire the conference list lock
	ast_mutex_lock(&conflist_lock);

	if ((conf = find_conf(conference)))
	{
		// acquire the conference lock
		ast_rwlock_wrlock(&conf->listheaders->lock);

		// adjust volume
		up ? conf->volume++ : conf->volume--;

		// release the conference lock
		ast_rwlock_unlock(&conf->listheaders->lock);
	}

	// release the conference list lock
	ast_mutex_unlock(&conflist_lock);
}

int hash(const char *name)
{
	int i = 0, h = 0, g;
	while (name[i])
	{
		h = (h << 4) + name[i++];
		if ((g = h & 0xF0000000))
			h ^= g >> 24;
		h &= ~g;
	}
	return h;
}
