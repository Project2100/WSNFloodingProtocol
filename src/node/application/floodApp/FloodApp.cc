/****************************************************************************
 *  Copyright: National ICT Australia,  2007 - 2011                         *
 *  Developed at the ATP lab, Networked Systems research theme              *
 *  Author(s): Athanassios Boulis, Yuriy Tselishchev                        *
 *  This file is distributed under the terms in the attached LICENSE file.  *
 *  If you do not find this file, copies can be found by writing to:        *
 *                                                                          *
 *      NICTA, Locked Bag 9013, Alexandria, NSW 1435, Australia             *
 *      Attention:  License Inquiry.                                        *
 *                                                                          *
 ****************************************************************************/

#include "FloodApp.h"

Define_Module(FloodApp);

void FloodApp::startup()
{
	// Initiate logging stream
	char filename[64] = {0};
	snprintf(filename, 63, "Dev%s_AppLog", SELF_NETWORK_ADDRESS);
	log = std::fopen(filename, "w+");
	
	std::fprintf(log, "Application module is: %s\n", getParentModule()->getParentModule()->getSubmodule("node", 0)->par("ApplicationName").stringValue());
	
	std::fprintf(log, "Routing module is: %s\n", getParentModule()->getParentModule()->getSubmodule("node", 0)->getSubmodule("Communication")->par("RoutingProtocolName").stringValue());
	
	std::fprintf(log, "MAC module is: %s\n", getParentModule()->getParentModule()->getSubmodule("node", 0)->getSubmodule("Communication")->par("MACProtocolName").stringValue());


	
	// Gets the sink address
	recipientAddress = par("nextRecipient").stringValue();

	std::fprintf(log, "Destination is %s\n", recipientAddress.c_str());

	recipientId = atoi(recipientAddress.c_str());

	startupDelay = par("startupDelay");
	delayLimit = par("delayLimit");
	
	// In the 0 case, 
	packet_spacing = 5;
	
	dataSN = 0;
	
	numNodes = getParentModule()->getParentModule()->par("numNodes");

	// Is it necessary?
	packetsSent.clear();
	packetsReceived.clear();
	bytesReceived.clear();

	if (recipientAddress.compare(SELF_NETWORK_ADDRESS) != 0) {
		std::fprintf(log, "Device is NOT Sink\n");
		if (packet_spacing == 0) {
			std::fprintf(log, "Null packet spacing, node will stay silent");
		}
		else {
			std::fprintf(log, "Sending route request packet #%d", dataSN);
			trace() << "Sending route request packet #" << dataSN;

			setTimer(SEND_PACKET, packet_spacing + startupDelay);
		}
	}
	else {
		std::fprintf(log, "Device is Sink\n");
		// Being the sink, we wait for incoming messages
		trace() << "I am the sink, listening for any messages...";
	}

	declareOutput("Packets received per node");

}

void FloodApp::fromNetworkLayer(ApplicationPacket * rcvPacket,
		const char *source, double rssi, double lqi)
{
	
	std::fprintf(log, "Packet received\n");

	int sequenceNumber = rcvPacket->getSequenceNumber();
	int sourceId = atoi(source);


	if (recipientAddress.compare(SELF_NETWORK_ADDRESS) == 0) {
		
		// This node is the final recipient for the packet
		// AP190807 - NOTE: Shortcircuiting condition
		if (delayLimit == 0 || (simTime() - rcvPacket->getCreationTime()) <= delayLimit) { 
			trace() << "Received packet #" << sequenceNumber << " from node " << source;
			collectOutput("Packets received per node", sourceId);
			packetsReceived[sourceId]++;
			bytesReceived[sourceId] += rcvPacket->getByteLength();
		}
		
		else {
			trace() << "Packet #" << sequenceNumber << " from node " << source << " exceeded delay limit of " << delayLimit << "s";
		}
	}
	else {
		// Packet has to be forwarded to the next hop recipient
		ApplicationPacket* fwdPacket = rcvPacket->dup();
		// Reset the size of the packet, otherwise the app overhead will keep adding on
		fwdPacket->setByteLength(0);
		toNetworkLayer(fwdPacket, recipientAddress.c_str());
	}

	
}

void FloodApp::timerFiredCallback(int index)
{
	switch (index) {
		case SEND_PACKET:{
			trace() << "Sending packet #" << dataSN;
			toNetworkLayer(createGenericDataPacket(0, dataSN), recipientAddress.c_str());
			packetsSent[recipientId]++;
			dataSN++;
			setTimer(SEND_PACKET, packet_spacing);
			break;
		}
	}
}

// This method processes a received carrier sense interupt. Used only for demo purposes
// in some simulations. Feel free to comment out the trace command.
void FloodApp::handleRadioControlMessage(RadioControlMessage *radioMsg)
{
	switch (radioMsg->getRadioControlMessageKind()) {
		case CARRIER_SENSE_INTERRUPT:
			trace() << "CS Interrupt received! current RSSI value is: " << radioModule->readRSSI();
                        break;
	}
}

void FloodApp::finishSpecific() {
	declareOutput("Packets reception rate");
	declareOutput("Packets loss rate");

	cTopology *topo;	// temp variable to access packets received by other nodes
	topo = new cTopology("topo");
	topo->extractByNedTypeName(cStringTokenizer("node.Node").asVector());

	long bytesDelivered = 0;
	for (int i = 0; i < numNodes; i++) {
		FloodApp *appModule = dynamic_cast<FloodApp*>
			(topo->getNode(i)->getModule()->getSubmodule("Application"));
		if (appModule) {
			int packetsSent = appModule->getPacketsSent(self);
			if (packetsSent > 0) { // this node sent us some packets
				float rate = (float)packetsReceived[i]/packetsSent;
				collectOutput("Packets reception rate", i, "total", rate);
				collectOutput("Packets loss rate", i, "total", 1-rate);
			}

			bytesDelivered += appModule->getBytesReceived(self);
		}
	}
	delete(topo);

	if (bytesDelivered > 0) {
		double energy = (resMgrModule->getSpentEnergy() * 1000000000)/(bytesDelivered * 8);	//in nanojoules/bit
		declareOutput("Energy nJ/bit");
		collectOutput("Energy nJ/bit","",energy);
	}

	std::fclose(log);
}
