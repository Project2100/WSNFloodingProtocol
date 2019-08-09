///**
// * @file FloodRouting.ned
// * @author Andrea Proietto
// * @date 2019-08-01
// */

package node.communication.routing.floodRouting;

simple FloodRouting like node.communication.routing.iRouting {
    parameters:
        bool collectTraceInfo = default (false);
        int maxNetFrameSize = default (0);
        int netDataFrameOverhead = default (10);
        int netBufferSize = default (32);

    gates:
        output toCommunicationModule;
        output toMacModule;
        input fromCommunicationModule;
        input fromMacModule;
        input fromCommModuleResourceMgr;
}

