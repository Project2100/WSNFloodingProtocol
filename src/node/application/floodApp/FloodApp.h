/**
 * @file FloodApp.h
 * @brief Application module to be used in conjunction with the `FloodRouting` module
 * 
 * @author Andrea Proietto
 * @date 2019-08-01
 */

#ifndef _FLOODAPP_H_
#define _FLOODAPP_H_

#include "VirtualApplication.h"
#include <map>

using namespace std;

enum FloodAppTimers {
	SEND_PACKET = 1
};

class FloodApp: public VirtualApplication {
 private:
 
	double startupDelay;
	double delayLimit;
	double packet_spacing;
	int dataSN;
	int recipientId;
	string recipientAddress;
	
	std::FILE* log;												/**< @brief Logging file pointer */
	
	//variables below are used to determine the packet delivery rates.	
	int numNodes;
	map<long,int> packetsReceived;
	map<long,int> bytesReceived;
	map<long,int> packetsSent;

 protected:
	void startup();
	void fromNetworkLayer(ApplicationPacket *, const char *, double, double);
	void handleRadioControlMessage(RadioControlMessage *);
	void timerFiredCallback(int);
	void finishSpecific();

 public:
	int getPacketsSent(int addr) { return packetsSent[addr]; }
	int getPacketsReceived(int addr) { return packetsReceived[addr]; }
	int getBytesReceived(int addr) { return bytesReceived[addr]; }
	
};

#endif				// _FLOODAPP_APPLICATIONMODULE_H_
