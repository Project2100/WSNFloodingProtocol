/**
 * @file FloodRouting.cc
 * @author Andrea Proietto
 * @date 2019-08-01
 */

#include "FloodRouting.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <list>

Define_Module(FloodRouting);

#define PACKET_NAME_MAXCH 64

#define	LOGDESC_TX "Routing packet breakdown (TX)"
#define	LOGDESC_RX "Routing packet breakdown (RX)"
#define LOGDESC_DATATX "New data packets"
#define LOGDESC_OTHRTX "New other packets"
#define LOGDESC_DATARE "Relaid data packets"
#define LOGDESC_OTHRRE "Relaid other packets"
#define LOGDESC_DATARX "Data packets"
#define LOGDESC_OTHRRX "Other packets"
#define LOGDESC_DISCRX "Discarded packets"
#define LOGDESC_APPLRX "Packets forwarded to application layer"

void FloodRouting::startup() {

	// Initiate logging stream
	char filename[64] = {0};
	snprintf(filename, 63, "Dev%s_RoutingLog", SELF_NETWORK_ADDRESS);
	log = std::fopen(filename, "w+");

	declareOutput(LOGDESC_TX);
	declareOutput(LOGDESC_RX);
}


void FloodRouting::fromApplicationLayer(cPacket* pkt, const char *destination) {

	// Look up the routing table for a path to the destination
	try {
		std::list<std::string> route = routeTable.at(destination);

		// AP190808: If we're here, then we have a valid route; build a DATA packet, and send it in unicast
		char packetName[PACKET_NAME_MAXCH] = {0};
		ApplicationPacket *appPacket = dynamic_cast <ApplicationPacket*>(pkt);
		std::snprintf(packetName, PACKET_NAME_MAXCH - 1 , "DATA-packet::%s:%u", SELF_NETWORK_ADDRESS, appPacket->getSequenceNumber());
		
		FloodRoutingPacket *netPacket = new FloodRoutingPacket(packetName, NETWORK_LAYER_PACKET);
		netPacket->setSource(SELF_NETWORK_ADDRESS);
		netPacket->setDestination(destination);
		netPacket->setType(PacketType::DATA);
		netPacket->setSEQ(SEQ);

		// Transcribe the route into the packet header
		int idx = 0;
		for (auto relay : route) {
			netPacket->setRoute(idx, relay.c_str());
			idx++;
			if (idx == route.size() - 1) break; // We don't want to write down the destination too, it's unnecessary, and in fringe cases it might not even be possible
		}
		netPacket->setIndex(0);
		
		// Check if the route is exausted
		std::string dest = netPacket->getRoute(netPacket->getIndex());
		if (dest == "") dest = destination;

		encapsulatePacket(netPacket, pkt);
		// Unicast to first relay using our MAC cache
		toMacLayer(netPacket, addressTable[dest]);
		SEQ++;
		collectOutput(LOGDESC_TX, LOGDESC_DATATX);

		std::fprintf(log, "Data \"%s\" sent to device %s\n\n", packetName, dest.c_str());
	}
	catch (const std::out_of_range& e) {
		// No route to destination, build a RREQ packet instead and broadcast

		char packetName[PACKET_NAME_MAXCH] = {0};
		ApplicationPacket *appPacket = dynamic_cast <ApplicationPacket*>(pkt);
		std::snprintf(packetName, PACKET_NAME_MAXCH - 1, "REQ-packet::%s:%u", SELF_NETWORK_ADDRESS, appPacket->getSequenceNumber());
		
		FloodRoutingPacket *netPacket = new FloodRoutingPacket(packetName, NETWORK_LAYER_PACKET);
		netPacket->setSource(SELF_NETWORK_ADDRESS);
		netPacket->setDestination(destination);
		netPacket->setType(PacketType::RREQ);
		netPacket->setSEQ(SEQ);

		// Initialize an empty route
		for (size_t i = 0; i < 10; i++) {
			netPacket->setRoute(i, "");
		}
		netPacket->setIndex(0);

		encapsulatePacket(netPacket, pkt);
		toMacLayer(netPacket, BROADCAST_MAC_ADDRESS);
		SEQ++;
		collectOutput(LOGDESC_TX, LOGDESC_OTHRTX);

		std::fprintf(log, "Request \"%s\" broadcast to MAC layer\n\n", packetName);
	}

}


void FloodRouting::fromMacLayer(cPacket* pkt, int srcMacAddress, double rssi, double lqi) {

	// Cast the packet
	FloodRoutingPacket *netPacket = dynamic_cast <FloodRoutingPacket*>(pkt);
	if (!netPacket) {
		std::fprintf(log, "Unrecognized packet, discarding\n\n");
		return;
	}

	std::fprintf(log, "Packet received from MAC layer: \"%s\"\n", netPacket->getName());
	char packetName[PACKET_NAME_MAXCH] = {0};
	std::memcpy(packetName, netPacket->getName(), PACKET_NAME_MAXCH - 1);

	// Packet is valid, get the sender, and (re)map the source's MAC-routing pair
	std::string sender = std::string(netPacket->getIndex()
			? netPacket->getRoute(netPacket->getIndex() - 1)
			: netPacket->getSource());
	addressTable[sender] = srcMacAddress;
	
	std::string destination(netPacket->getDestination());
	std::string source(netPacket->getSource());

	// Check source: there is no point in reading a packet we transmitted ourselves
	if (std::memcmp(netPacket->getSource(), SELF_NETWORK_ADDRESS, std::strlen(netPacket->getSource())) == 0) {
		std::fprintf(log, "This request came from us, discarding\n\n");
		collectOutput(LOGDESC_RX, LOGDESC_DISCRX);
		return;
	}

	// Sequence number check
	int SEQn = netPacket->getSEQ();
	
	try {
		int SEQm = SEQTable.at(source);

		if (SEQn <= SEQm) {
			// This packet is old, discard it
			std::fprintf(log, "This packet has an older SEQ - tracked: %d, packet: %d - discarding\n\n", SEQm, SEQn);
			collectOutput(LOGDESC_RX, LOGDESC_DISCRX);
			return;
		}
		else {
			// SEQ number is valid, register it
			// NOTE: Not dealing with failed transmissions, or any gap related phenomenon
			SEQTable[source] = SEQn;
		}

	}
	catch (const std::out_of_range& e) {
		// We never got a packet from this device, register SEQ unconditionally
		std::fprintf(log, "First time listening from %s: registering SEQ: %d\n", source.c_str(), SEQn);
		SEQTable[source] = SEQn;
	}

	//------------------------------------------------------------
	// Select a course of action, based on the packet type
	switch (netPacket->getType()) {

	case PacketType::DATA:
		collectOutput(LOGDESC_RX, LOGDESC_DATARX);
	
		// This is a regular packet, see if it reached the destination
		if (destination.compare(SELF_NETWORK_ADDRESS) == 0) {
			
			// The packet has arrived, deliver it to the app layer
			std::fprintf(log, "Data packet reached destination, delivering to application layer\n\n");
			toApplicationLayer(decapsulatePacket(pkt));
			collectOutput(LOGDESC_RX, LOGDESC_APPLRX);
			
		}
		else {
			
			// If we're here, then the packet must be forwarded
			std::fprintf(log, "Data must be relaid\n");

			// Duplicate the reply and move the cursor forward
			FloodRoutingPacket* p = netPacket->dup();
			p->setIndex(p->getIndex() + 1);

			// Check if the route is exausted: this means we must use the packet's destination address instead
			std::string dest;
			if (p->getIndex() >= 10) dest = destination;
			else {
				dest = p->getRoute(p->getIndex());
				if (dest == "") dest = destination;
			}
			
			toMacLayer(p, addressTable[dest]);
			collectOutput(LOGDESC_TX, LOGDESC_DATARE);
			std::fprintf(log, "Data \"%s\" sent to device %s\n\n", p->getName(), dest.c_str());
		}

		break;

	case PacketType::RREQ:
		collectOutput(LOGDESC_RX, LOGDESC_OTHRRX);

		// This is a route request, see if it reached the destination
		if (destination.compare(SELF_NETWORK_ADDRESS)) {

			// This packet is not for us, it must be forwarded
			std::fprintf(log, "Forwarding packet...\n");

			// Dupe the packet, and record ourselves in the route
			FloodRoutingPacket* p = netPacket->dup();
			p->setRoute(p->getIndex(), SELF_NETWORK_ADDRESS);
			p->setIndex(p->getIndex() + 1);
			
			toMacLayer(p, BROADCAST_MAC_ADDRESS);
			collectOutput(LOGDESC_TX, LOGDESC_OTHRRE);
			std::fprintf(log, "Request \"%s\" broadcast to MAC layer\n\n", p->getName());
		}
		else {
			// This packet's trip is finished, send it to app
			std::fprintf(log, "Packet reached destination\n");

			// Save the route, if not present
			try {
				routeTable.at(source);
				// AP190808: If we're here, then we've already made a route to here.
				// 			 For now, ignore the new request, may consider different strategies in the future
				std::fprintf(log, "Request ignored, we already have a route\n\n");
				collectOutput(LOGDESC_RX, LOGDESC_DISCRX);
				return;
			}
			catch (const std::out_of_range& e) {

				std::fprintf(log, "Displaying route: %s", netPacket->getSource());
				for (auto it = 0; it < netPacket->getIndex(); ++it) {
					std::fprintf(log, " -> %s", netPacket->getRoute(it));
				}
				std::fprintf(log, " -> %s\n", netPacket->getDestination());

				std::fprintf(log, "Saving route...\n");
				std::list<std::string> route;
				route.push_front(source);
				for (auto i = 0; i < netPacket->getIndex(); i++) {
					route.push_front(std::string(netPacket->getRoute(i)));
				}
				routeTable[source] = route;
			
				// Deliver the data to the application layer - keep its name
				std::fprintf(log, "Unpacking and delivering to application\n");
				toApplicationLayer(decapsulatePacket(pkt));
				collectOutput(LOGDESC_RX, LOGDESC_APPLRX);

				// Finally, construct the corresponding RREP to send back to the source
				packetName[2] = 'P';
				FloodRoutingPacket *netPacket2 = new FloodRoutingPacket(packetName, NETWORK_LAYER_PACKET);
				netPacket2->setSource(SELF_NETWORK_ADDRESS);
				netPacket2->setDestination(source.c_str());
				netPacket2->setType(PacketType::RREP);
				netPacket2->setSEQ(SEQ);

				// Transcribe the new route
				int idx = 0;
				for (auto relay : route) {
					netPacket2->setRoute(idx, relay.c_str());
					idx++;
					if (idx == route.size() - 1) break; // We don't want to write down the destination too, it's unnecessary, and in fringe cases it might not even be possible
				}
				netPacket2->setIndex(0);

				toMacLayer(netPacket2, addressTable[netPacket2->getRoute(netPacket2->getIndex())]);
				SEQ++;
				collectOutput(LOGDESC_TX, LOGDESC_OTHRRE);

				std::fprintf(log, "Reply \"%s\" sent to device %s\n\n", packetName, netPacket2->getRoute(netPacket2->getIndex()));
			}
		}

		break;

	case PacketType::RREP:
		collectOutput(LOGDESC_RX, LOGDESC_OTHRRX);

		// This is a route reply, see if it reached the destination
		if (destination.compare(SELF_NETWORK_ADDRESS)) {

			// This packet is not for us, it must be forwarded
			std::fprintf(log, "Forwarding packet...\n");

			// Dupe the reply, and move the cursor forward
			FloodRoutingPacket* p = netPacket->dup();
			p->setIndex(p->getIndex() + 1);

			// Check if the route is exausted: this means we must use the packet's destination address instead
			std::string dest;
			if (p->getIndex() >= 10) dest = destination;
			else {
				dest = p->getRoute(p->getIndex());
				if (dest == "") dest = destination;
			}
			
			toMacLayer(p, addressTable[dest]);
			collectOutput(LOGDESC_TX, LOGDESC_OTHRRE);
			std::fprintf(log, "Reply \"%s\" sent to device %s\n\n", packetName, dest.c_str());
		}
		else {
			std::fprintf(log, "Reply has reached destination\n");
			
			// The reply is home, add the route to the map and bail
			try {
				routeTable.at(source);
				// AP190808: If we're here, then we've already made a route to here.
				// 			 For now, ignore the new request, may consider different strategies in the future
				std::fprintf(log, "Reply ignored, we already have a route\n\n");
				collectOutput(LOGDESC_RX, LOGDESC_DISCRX);
			}
			catch (const std::out_of_range& e) {

				std::fprintf(log, "Displaying route: %s", netPacket->getSource());
				for (auto it = 0; it < netPacket->getIndex(); ++it) {
					std::fprintf(log, " -> %s", netPacket->getRoute(it));
				}
				std::fprintf(log, " -> %s\n", netPacket->getDestination());

				std::fprintf(log, "Saving route...\n");
				std::list<std::string> route;
				route.push_front(source);
				for (auto i = 0; i < netPacket->getIndex(); i++) {
					route.push_front(std::string(netPacket->getRoute(i)));
				}
				routeTable[source] = route;
				std::fprintf(log, "Route saved\n\n");
			}
		}

		break;

	case PacketType::ACK:
		// Unimplemented

		break;

	}
}


void FloodRouting::finish() {

	std::fprintf(log, "Address mappings:\n");
	for (auto it = addressTable.begin(); it != addressTable.end(); ++it) {
		std::fprintf(log, "%s, %d\n", it->first.c_str(), it->second);
	}

	std::fprintf(log, "\nRouting table (%zu entries):\n", routeTable.size());
	if (!routeTable.empty()) {
		for (auto key = routeTable.begin(); key != routeTable.end(); ++key) {
			std::fprintf(log, "Destination: %s - Route (length: %zu):", key->first.c_str(), key->second.size());
			for (std::string node : key->second) {
				std::fprintf(log, " %s", node.c_str());
			}
			std::fprintf(log, "\n");
		}
		
	}

	
	std::fprintf(log, "\nSEQ mappings:\n");
	for (auto it = SEQTable.begin(); it != SEQTable.end(); ++it) {
		std::fprintf(log, "%s, %d\n", it->first.c_str(), it->second);
	}

	std::fclose(log);
}
