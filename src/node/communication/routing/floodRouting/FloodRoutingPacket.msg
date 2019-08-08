//****************************************************************************
//*  Copyright: National ICT Australia,  2007 - 2010                         *
//*  Developed at the ATP lab, Networked Systems research theme              *
//*  Author(s): Yuriy Tselishchev                                            *
//*  This file is distributed under the terms in the attached LICENSE file.  *
//*  If you do not find this file, copies can be found by writing to:        *
//*                                                                          *
//*      NICTA, Locked Bag 9013, Alexandria, NSW 1435, Australia             *
//*      Attention:  License Inquiry.                                        *
//*                                                                          *  
//****************************************************************************/

cplusplus {{
#include "RoutingPacket_m.h"
}}

class RoutingPacket;

enum PacketType {
	RREQ = 0;
	RREP = 1;
	DATA = 2;
	ACK = 3;
}

packet FloodRoutingPacket extends RoutingPacket {
	int type enum (PacketType);
	string source;
	string destination;
	string route[10];		// Route taken by this packet so far
	int index;				// Route length
}

