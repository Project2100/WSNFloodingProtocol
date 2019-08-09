///**
// * @file FloodApp.ned
// * @author Andrea Proietto
// * @date 2019-08-01
// */

package node.application.floodApp;

// The sensor node module. Connects to the wireless channel in order to communicate 
// with other nodes. Connects to psysical processes so it can sample them.

simple FloodApp like node.application.iApplication {
 parameters:
 	string applicationID = default ("floodApp");
	bool collectTraceInfo = default (false);
	int priority = default (1);
	int packetHeaderOverhead = default (5);		// in bytes
	int constantDataPayload = default (100);	// in bytes
	double delayLimit = default (0); 			// application delay limit (0 - no limit)
											// discard packets arriving after the limit

	string nextRecipient = default ("0");	// Destination for packets received in this node. 
											// These parameters can be used to create an
											// application-level static routing. This way we can
											// have a multi-hop throughput test   

	double packetSpacing = default (5); // Time between packet generations, in seconds
	double startupDelay = default (0);	// delay in seconds before the app stars producing packets

	double latencyHistogramMax = default (200);
	int latencyHistogramBuckets = default (10);

 gates:
 	output toCommunicationModule;
	output toSensorDeviceManager;
	input fromCommunicationModule;
	input fromSensorDeviceManager;
	input fromResourceManager;
}
