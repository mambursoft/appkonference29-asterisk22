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

#ifndef _KONFERENCE_CLI_H
#define _KONFERENCE_CLI_H

//
// includes
//

#include "app_conference.h"

//
// function declarations
//

#ifndef AST_CLI_DEFINE

int conference_version(int fd, int argc, char *argv[]);

int conference_restart(int fd, int argc, char *argv[]);

int conference_list(int fd, int argc, char *argv[]);
#ifdef	KICK_MEMBER
int conference_kick(int fd, int argc, char *argv[]);
#endif
int conference_kickchannel(int fd, int argc, char *argv[]);
#ifdef MUTE_MEMBER
int conference_mute(int fd, int argc, char *argv[]);
#endif
#ifdef UNMUTE_MEMBER
int conference_unmute(int fd, int argc, char *argv[]);
#endif
int conference_muteconference(int fd, int argc, char *argv[]);
int conference_unmuteconference(int fd, int argc, char *argv[]);
int conference_mutechannel(int fd, int argc, char *argv[]);
int conference_unmutechannel(int fd, int argc, char *argv[]);

int conference_play_sound(int fd, int argc, char *argv[]);
int conference_stop_sounds(int fd, int argc, char *argv[]);

int conference_stop_moh(int fd, int argc, char *argv[]);
int conference_start_moh(int fd, int argc, char *argv[]);

int conference_talkvolume(int fd, int argc, char *argv[]);
int conference_listenvolume(int fd, int argc, char *argv[]);
int conference_volume(int fd, int argc, char *argv[]);

int conference_end(int fd, int argc, char *argv[]);

#else

char *conference_version(struct ast_cli_entry *, int, struct ast_cli_args *);

char *conference_restart(struct ast_cli_entry *, int, struct ast_cli_args *);

char *conference_list(struct ast_cli_entry *, int, struct ast_cli_args *);
#ifdef	KICK_MEMBER
char *conference_kick(struct ast_cli_entry *, int, struct ast_cli_args *);
#endif
char *conference_kickchannel(struct ast_cli_entry *, int, struct ast_cli_args *);
#ifdef MUTE_MEMBER
char *conference_mute(struct ast_cli_entry *, int, struct ast_cli_args *);
#endif
#ifdef UNMUTE_MEMBER
char *conference_unmute(struct ast_cli_entry *, int, struct ast_cli_args *);
#endif
char *conference_muteconference(struct ast_cli_entry *, int, struct ast_cli_args *);
char *conference_unmuteconference(struct ast_cli_entry *, int, struct ast_cli_args *);
char *conference_mutechannel(struct ast_cli_entry *, int, struct ast_cli_args *);
char *conference_unmutechannel(struct ast_cli_entry *, int, struct ast_cli_args *);

char *conference_play_sound(struct ast_cli_entry *, int, struct ast_cli_args *);
char *conference_stop_sounds(struct ast_cli_entry *, int, struct ast_cli_args *);

char *conference_stop_moh(struct ast_cli_entry *, int, struct ast_cli_args *);
char *conference_start_moh(struct ast_cli_entry *, int, struct ast_cli_args *);

char *conference_talkvolume(struct ast_cli_entry *, int, struct ast_cli_args *);
char *conference_listenvolume(struct ast_cli_entry *, int, struct ast_cli_args *);
char *conference_volume(struct ast_cli_entry *, int, struct ast_cli_args *);

char *conference_end(struct ast_cli_entry *, int, struct ast_cli_args *);

#endif

void register_conference_cli(void);
void unregister_conference_cli(void);


#endif
