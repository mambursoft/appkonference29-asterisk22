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

#ifndef _KONFERENCE_FRAME_H
#define _KONFERENCE_FRAME_H

//
// includes
//

#include "app_conference.h"
#include "conf_frame.h"
#include "member.h"

//
// function declarations
//

// mixing
conf_frame* mix_frames( ast_conference* conf, conf_frame* frames_in, int speaker_count, int listener_count ) ;
conf_frame* mix_multiple_speakers( ast_conference* conf, conf_frame* frames_in, int speakers, int listeners ) ;
conf_frame* mix_single_speaker( ast_conference* conf, conf_frame* frames_in ) ;

// frame creation and deletion
conf_frame* create_conf_frame( ast_conf_member* member, const struct ast_frame* fr ) ;
conf_frame* create_mix_frame( ast_conf_member* member, conf_frame* next, conf_frame** cf ) ;
conf_frame* delete_conf_frame( conf_frame* cf ) ;

// convert frame function
struct ast_frame* convert_frame( struct ast_trans_pvt* trans, struct ast_frame* fr, int consume ) ;

// slinear frame function
struct ast_frame* create_slinear_frame( struct ast_frame** fr, char* data ) ;

#endif
