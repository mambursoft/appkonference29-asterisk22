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

#ifndef _KONFERENCE_CONF_FRAME_H
#define _KONFERENCE_CONF_FRAME_H

//
// includes
//

#include "app_conference.h"

//
// struct declarations
//

struct conf_frame
{
	// frame audio data
	struct ast_frame* fr ;

	// array of converted versions for listeners
	struct ast_frame* converted[ AC_SUPPORTED_FORMATS ] ;

	// pointer to the frame's owner
	ast_conf_member* member ; // who sent this frame

	// linked-list pointers
	conf_frame* next ;
	conf_frame* prev ;

	// pointer to mixing buffer
	char* mixed_buffer ;

	// conference + speaker volume
	int talk_volume ;
} ;


#endif
