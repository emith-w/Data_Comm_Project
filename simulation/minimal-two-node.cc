/*
 * Stage 2 sanity check: two nodes on one CSMA segment, one UDP flow.
 * Purpose: prove the ns-3 install, build and packet delivery work before
 * building the full office topology.
 *
 * Run: ./ns3 run "minimal-two-node"
 */
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"

using namespace ns3;

int
main(int argc, char* argv[])
{
    uint32_t packets = 10;
    CommandLine cmd(__FILE__);
    cmd.AddValue("packets", "Number of UDP packets to send", packets);
    cmd.Parse(argc, argv);

    NodeContainer nodes;
    nodes.Create(2);

    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", StringValue("100Mbps"));
    csma.SetChannelAttribute("Delay", TimeValue(MilliSeconds(2)));
    NetDeviceContainer devices = csma.Install(nodes);

    InternetStackHelper stack;
    stack.Install(nodes);
    Ipv4AddressHelper address;
    address.SetBase("10.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer ifaces = address.Assign(devices);

    const uint16_t port = 9000;
    UdpServerHelper server(port);
    ApplicationContainer serverApp = server.Install(nodes.Get(1));
    serverApp.Start(Seconds(0.5));
    serverApp.Stop(Seconds(5.0));

    UdpClientHelper client(ifaces.GetAddress(1), port);
    client.SetAttribute("MaxPackets", UintegerValue(packets));
    client.SetAttribute("Interval", TimeValue(MilliSeconds(100)));
    client.SetAttribute("PacketSize", UintegerValue(512));
    ApplicationContainer clientApp = client.Install(nodes.Get(0));
    clientApp.Start(Seconds(1.0));
    clientApp.Stop(Seconds(4.0));

    Simulator::Stop(Seconds(5.0));
    Simulator::Run();

    auto tx = DynamicCast<UdpClient>(clientApp.Get(0))->GetTotalTx() / 512;
    auto rx = DynamicCast<UdpServer>(serverApp.Get(0))->GetReceived();
    std::cout << "minimal-two-node: sent=" << tx << " received=" << rx << std::endl;
    Simulator::Destroy();

    // Non-zero exit if delivery failed, so automated tests can detect it.
    return (rx == packets && tx == packets) ? 0 : 1;
}
