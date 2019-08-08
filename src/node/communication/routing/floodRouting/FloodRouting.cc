/**
 * @brief Flood routing protocol implementation
 * 
 * @details
 * Implemented using notions primarily from DSR, and some from AODV
 * 
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

void FloodRouting::startup() {

	// Initiate logging stream
	char filename[64] = {0};
	snprintf(filename, 63, "Dev%s_RoutingLog", SELF_NETWORK_ADDRESS);
	log = std::fopen(filename, "w+");
}


void FloodRouting::fromApplicationLayer(cPacket * pkt, const char *destination)
{

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

		// Initialize an empty route
		for (size_t i = 0; i < 10; i++) {
			netPacket->setRoute(i, "");
		}
		netPacket->setIndex(0);

		encapsulatePacket(netPacket, pkt);
		toMacLayer(netPacket, BROADCAST_MAC_ADDRESS);

		std::fprintf(log, "Request \"%s\" broadcast to MAC layer\n\n", packetName);
	}

}


void FloodRouting::fromMacLayer(cPacket * pkt, int srcMacAddress, double rssi, double lqi)
{
    // Receiving a packet, two scenarios:
    // - we're not the sink: find out the packet's type (REQ/ACK), and rebroadcast accordingly
    // - we ARE the sink: establish route, construct ACK and broadcast


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



	//------------------------------------------------------------
	// Select a course of action, based on the packet type
	switch (netPacket->getType()) {
	case PacketType::RREQ:

		// This is a route request, see if it reached the destination
		if (destination.compare(SELF_NETWORK_ADDRESS)) {

			// We're not the destination, the request should be retransmitted

			// Look if the packet has already traversed us
			if (std::memcmp(netPacket->getSource(), SELF_NETWORK_ADDRESS, std::strlen(netPacket->getSource())) == 0) {
				std::fprintf(log, "This request came from us, discarding\n\n");
				return;
			}
			for (int i = 0; i < netPacket->getIndex(); i++) {
				if (std::memcmp(netPacket->getRoute(i), SELF_NETWORK_ADDRESS, std::strlen(netPacket->getSource())) == 0) {
					std::fprintf(log, "This packet looped back to us, discarding\n\n");
					return;
				}
			}

			// If we're here, then the packet must be forwarded
			std::fprintf(log, "Forwarding required, destination: %s\n", destination.c_str());

			FloodRoutingPacket* p = netPacket->dup();

			p->setRoute(p->getIndex(), SELF_NETWORK_ADDRESS);
			p->setIndex(p->getIndex() + 1);
			
			toMacLayer(p, BROADCAST_MAC_ADDRESS);
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

				// Finally, construct the corresponding RREP to send back to the source
				packetName[2] = 'P';
				FloodRoutingPacket *netPacket2 = new FloodRoutingPacket(packetName, NETWORK_LAYER_PACKET);
				netPacket2->setSource(SELF_NETWORK_ADDRESS);
				netPacket2->setDestination(source.c_str());
				netPacket2->setType(PacketType::RREP);

				// Transcribe the new route
				int idx = 0;
				for (auto relay : route) {
					netPacket2->setRoute(idx, relay.c_str());
					idx++;
					if (idx == route.size() - 1) break; // We don't want to write down the destination too, it's unnecessary, and in fringe cases it might not even be possible
				}
				netPacket2->setIndex(0);

				toMacLayer(netPacket2, addressTable[netPacket2->getRoute(netPacket2->getIndex())]);

				std::fprintf(log, "Reply \"%s\" sent to device %s\n\n", packetName, netPacket2->getRoute(netPacket2->getIndex()));
			}
		}


		break;

	case PacketType::RREP:

		// This is a route reply, see if it reached the destination
		if (destination.compare(SELF_NETWORK_ADDRESS) == 0) {
			std::fprintf(log, "Reply has reached destination\n");
			
			// The reply is home, add the route to the map and bail
			try {
				routeTable.at(source);
				// AP190808: If we're here, then we've already made a route to here.
				// 			 For now, ignore the new request, may consider different strategies in the future 
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
		else {
			// Reply should be forwarded, look if the packet has already traversed us
			if (std::memcmp(netPacket->getSource(), SELF_NETWORK_ADDRESS, std::strlen(netPacket->getSource())) == 0) {
				std::fprintf(log, "This request came from us, discarding\n\n");
				return;
			}
			for (int i = 0; i < netPacket->getIndex(); i++) {
				if (std::memcmp(netPacket->getRoute(i), SELF_NETWORK_ADDRESS, std::strlen(netPacket->getSource())) == 0) {
					std::fprintf(log, "This packet looped back to us, discarding\n\n");
					return;
				}
			}

			// If we're here, then the packet must be forwarded
			std::fprintf(log, "Reply must be relaid\n");

			// Duplicate the reply and move the cursor forward
			FloodRoutingPacket* p = netPacket->dup();
			p->setIndex(p->getIndex() + 1);

			// Check if the route is exausted
			std::string dest = p->getRoute(p->getIndex());
			if (dest == "") dest = destination;
			
			toMacLayer(p, addressTable[dest]);
			std::fprintf(log, "Reply \"%s\" sent to device %s\n\n", packetName, dest.c_str());
		}

		break;

	case PacketType::DATA:
	
		// This is a regular packet, see if it reached the destination
		if (destination.compare(SELF_NETWORK_ADDRESS) == 0) {
			
			// The packet has arrived, deliver it to the app layer
			std::fprintf(log, "Data packet reached destination, delivering to application layer\n\n");
			toApplicationLayer(decapsulatePacket(pkt));
			
		}
		else {
			
			// If we're here, then the packet must be forwarded
			std::fprintf(log, "Data must be relaid\n");

			// Duplicate the reply and move the cursor forward
			FloodRoutingPacket* p = netPacket->dup();
			p->setIndex(p->getIndex() + 1);

			// Check if the route is exausted
			std::string dest = p->getRoute(p->getIndex());
			if (dest == "") dest = destination;
			
			toMacLayer(p, addressTable[dest]);
			std::fprintf(log, "Data \"%s\" sent to device %s\n\n", p->getName(), dest.c_str());
		}

		break;

	case PacketType::ACK:
		// TODO
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

	std::fclose(log);
}
