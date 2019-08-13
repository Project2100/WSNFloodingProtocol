///**
// * @file FloodRoutingPacket.msg
// * @author Andrea Proietto
// * @date 2019-08-01
// */

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
	int type enum (PacketType);	// The type of this packet
	string source;
	string destination;
	string route[10];			// Route buffer: if packet is a request, it contains the traversed devices so far, otherwise it holds the complete route to traverse
	int index;					// Route index: if packet is a request, it doubles as the route size, otherwise it acts as a route cursor
	int SEQ;					// Packet sequence number
}

