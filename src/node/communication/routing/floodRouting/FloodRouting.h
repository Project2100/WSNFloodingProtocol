/**
 * Flood routing protocol
 * 
 * @author Andrea Proietto
 * @date 2019-08-01
 */

#ifndef _FLOODROUTING_H_
#define _FLOODROUTING_H_

#include "VirtualRouting.h"
#include "FloodRoutingPacket_m.h"

using namespace std;

class FloodRouting: public VirtualRouting {

private:
	std::map<std::string, int> addressTable;
	std::FILE* log;

protected:

	void startup();
	void fromApplicationLayer(cPacket *, const char *);
	void fromMacLayer(cPacket *, int, double, double);
	void finish();
	
};

#endif				//FLOODROUTINGMODULE
