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

char *conference_version(struct ast_cli_entry *, int, struct ast_cli_args *);

char *conference_restart(struct ast_cli_entry *, int, struct ast_cli_args *);

char *conference_list(struct ast_cli_entry *, int, struct ast_cli_args *);
char *conference_kickchannel(struct ast_cli_entry *, int, struct ast_cli_args *);
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

#ifdef	VIDEO
char *conference_videoswitch(struct ast_cli_entry *, int, struct ast_cli_args *);
#endif

void register_conference_cli(void);
void unregister_conference_cli(void);


#endif
