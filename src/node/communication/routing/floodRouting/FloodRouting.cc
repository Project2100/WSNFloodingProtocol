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

Define_Module(FloodRouting);

void FloodRouting::startup() {

	// Initiate logging stream
	char filename[64] = {0};
	snprintf(filename, 63, "Dev%s_RoutingLog", SELF_NETWORK_ADDRESS);
	log = std::fopen(filename, "w+");
}


void FloodRouting::fromApplicationLayer(cPacket * pkt, const char *destination)
{
	
    // Packet comes from application, most likely from a sensing device (ID != 0); construct REQ and broadcast

    // OR, it may be an ACK (?), meaning we are the sink: set the intended recipient as specified by the established route and broadcast - use the address index map to get the right MAC address on each hop


	char packetName[64] = {0};
	ApplicationPacket *appPacket = dynamic_cast <ApplicationPacket*>(pkt);
	std::snprintf(packetName, 63, "Flood-packet::%s:%u", SELF_NETWORK_ADDRESS, appPacket->getSequenceNumber());
	
	FloodRoutingPacket *netPacket = new FloodRoutingPacket(packetName, NETWORK_LAYER_PACKET);
	netPacket->setSource(SELF_NETWORK_ADDRESS);
	netPacket->setDestination(destination);

	for (size_t i = 0; i < 10; i++) {
		netPacket->setRoute(i, "");
	}
	netPacket->setIndex(0);

	encapsulatePacket(netPacket, pkt);
	toMacLayer(netPacket, BROADCAST_MAC_ADDRESS);

	std::fprintf(log, "Packet sent to MAC layer: \"%s\"\n\n", packetName);
}


void FloodRouting::fromMacLayer(cPacket * pkt, int srcMacAddress, double rssi, double lqi)
{
    // Receiving a packet, two scenarios:
    // - we're not the sink: find out the packet's type (REQ/ACK), and rebroadcast accordingly
    // - we ARE the sink: establish route, construct ACK and broadcast
	
	

	// Cast the packet
	FloodRoutingPacket *netPacket = dynamic_cast <FloodRoutingPacket*>(pkt);
	if (netPacket) {

		std::fprintf(log, "Packet received from MAC layer: \"%s\"\n", netPacket->getName());

		// Packet is valid, get the addresses
		// (Re)map the source's MAC-routing pair
		if (netPacket->getIndex() == 0){
			std::string sender(netPacket->getSource());
			addressTable[sender] = srcMacAddress;
		}
		else {
			std::string sender(netPacket->getRoute(netPacket->getIndex() - 1));
			addressTable[sender] = srcMacAddress;
		}
	


		std::string destination(netPacket->getDestination());
		if (destination.compare(SELF_NETWORK_ADDRESS) == 0) {
			// This packet's trip is finished, send it to app
			std::fprintf(log, "Packet reached destination, displaying route: %s", netPacket->getSource());

			for (auto it = 0; it != netPacket->getIndex(); ++it) {
				std::fprintf(log, " -> %s", netPacket->getRoute(it));
			}

			std::fprintf(log, " -> %s", netPacket->getDestination());

			std::fprintf(log, "\nUnpacking and delivering to application\n\n");
			toApplicationLayer(decapsulatePacket(pkt));
			return;
		}
		else {

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
			std::fprintf(log, "Packet forwarding required, destination: %s - rebroadcasting\n\n", destination.c_str());

			FloodRoutingPacket* p = netPacket->dup();

			p->setRoute(p->getIndex(), SELF_NETWORK_ADDRESS);
			p->setIndex(p->getIndex() + 1);
			
			toMacLayer(p, BROADCAST_MAC_ADDRESS);
		}
	}
	else {
		std::fprintf(log, "Unrecognized packet, discarding\n\n");
	}

}


void FloodRouting::finish() {
	
	std::fprintf(log, "Routing table:\n\n");

	for (auto it = addressTable.begin(); it != addressTable.end(); ++it) {
		std::fprintf(log, "%s, %d\n", it->first.c_str(), it->second);
	}

	std::fclose(log);
}
