/*
 * office-network.cc — Design and Testing of a Small Office Network Using ns-3
 * COMP 3343, Acadia University. ns-3.49.
 *
 * Topology:
 *
 *                     [Router]
 *            192.168.10.1 |  | 192.168.20.1
 *              ----------- -- -----------
 *              Admin LAN (CSMA)   Staff LAN (CSMA)
 *              /        \          /        \
 *        AdminPC1  AdminPC2   StaffPC1  StaffPC2
 *        .10.10    .10.11     .20.10    .20.11
 *
 * Run:  ./ns3 run "office-network --help"
 */
#include "access-control.h"
#include "network-config.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <tuple>

using namespace ns3;
using namespace smalloffice;

NS_LOG_COMPONENT_DEFINE("SmallOfficeNetwork");

namespace
{

constexpr uint8_t kUdp = iana::internetprotocolnumbers::UDP;

// ---------------------------------------------------------------------------
// Validation helpers: every check is printed and remembered so the program
// exits non-zero if the network was not built as designed.
// ---------------------------------------------------------------------------
int g_failedChecks = 0;
std::vector<std::string> g_checkLog;

void
Check(bool ok, const std::string& what)
{
    std::string line = std::string(ok ? "CHECK PASS: " : "CHECK FAIL: ") + what;
    std::cout << line << std::endl;
    g_checkLog.push_back(line);
    if (!ok)
    {
        ++g_failedChecks;
    }
}

/// Adds an IPv4 interface with a specific address to a node's device.
/// The addresses in the plan are not contiguous (.1, .10, .11), so they are
/// set explicitly instead of with Ipv4AddressHelper's sequential allocator.
void
AssignAddress(Ptr<NetDevice> dev, const char* ip, const char* mask)
{
    Ptr<Ipv4> ipv4 = dev->GetNode()->GetObject<Ipv4>();
    int32_t ifIndex = ipv4->AddInterface(dev);
    ipv4->AddAddress(ifIndex, Ipv4InterfaceAddress(Ipv4Address(ip), Ipv4Mask(mask)));
    ipv4->SetMetric(ifIndex, 1);
    ipv4->SetUp(ifIndex);
}

/// Flow key used by the router traces: (source, destination, destination port).
using FlowKey = std::tuple<uint32_t, uint32_t, uint16_t>;

struct RouterCounters
{
    uint64_t rx = 0;        ///< packets of this flow that reached the router's IP layer
    uint64_t forwarded = 0; ///< packets the router actually forwarded
    uint64_t dropTrace = 0; ///< packets reported by the router's Drop trace (route error)
};

std::map<FlowKey, RouterCounters> g_router;

/// Router "Rx" trace: the packet still carries its IPv4 header here.
void
RouterRx(Ptr<const Packet> p, Ptr<Ipv4>, uint32_t)
{
    Ptr<Packet> copy = p->Copy();
    Ipv4Header ip;
    copy->RemoveHeader(ip);
    if (ip.GetProtocol() != kUdp)
    {
        return;
    }
    UdpHeader udp;
    copy->PeekHeader(udp);
    g_router[{ip.GetSource().Get(), ip.GetDestination().Get(), udp.GetDestinationPort()}].rx++;
}

/// Router "UnicastForward" trace: fired only when a packet is really forwarded.
void
RouterForward(const Ipv4Header& ip, Ptr<const Packet> p, uint32_t)
{
    if (ip.GetProtocol() != kUdp)
    {
        return;
    }
    UdpHeader udp;
    p->PeekHeader(udp);
    g_router[{ip.GetSource().Get(), ip.GetDestination().Get(), udp.GetDestinationPort()}]
        .forwarded++;
}

/// Router "Drop" trace: with --filterSendIcmp=true, denied packets pass through
/// the router's error path and are reported here as DROP_ROUTE_ERROR.
void
RouterDrop(const Ipv4Header& ip,
           Ptr<const Packet> p,
           Ipv4L3Protocol::DropReason reason,
           Ptr<Ipv4>,
           uint32_t)
{
    if (ip.GetProtocol() != kUdp || reason != Ipv4L3Protocol::DROP_ROUTE_ERROR)
    {
        return;
    }
    UdpHeader udp;
    p->PeekHeader(udp);
    g_router[{ip.GetSource().Get(), ip.GetDestination().Get(), udp.GetDestinationPort()}]
        .dropTrace++;
}

/// Checks that host `from` routes towards `to` through `gateway`
/// (empty gateway = directly connected, i.e. same subnet).
void
CheckRoute(Ptr<Node> from, const char* fromName, const char* to, const char* gateway)
{
    Ptr<Ipv4> ipv4 = from->GetObject<Ipv4>();
    Ipv4Header hdr;
    hdr.SetDestination(Ipv4Address(to));
    Socket::SocketErrno err;
    Ptr<Ipv4Route> route = ipv4->GetRoutingProtocol()->RouteOutput(nullptr, hdr, nullptr, err);
    std::ostringstream what;
    what << "route " << fromName << " -> " << to << " via "
         << (gateway ? gateway : "direct (same subnet)");
    if (!route)
    {
        Check(false, what.str() + " [no route]");
        return;
    }
    Ipv4Address expected = gateway ? Ipv4Address(gateway) : Ipv4Address::GetAny();
    bool ok = route->GetGateway() == expected;
    if (!ok)
    {
        what << " [got gateway " << route->GetGateway() << "]";
    }
    Check(ok, what.str());
}

std::string
Fmt(double v, int precision = 4)
{
    if (std::isnan(v))
    {
        return "";
    }
    std::ostringstream os;
    os << std::fixed << std::setprecision(precision) << v;
    return os.str();
}

} // namespace

int
main(int argc, char* argv[])
{
    SimConfig cfg;
    CommandLine cmd(__FILE__);
    cmd.AddValue("experiment", "Experiment id written to the output files", cfg.experiment);
    cmd.AddValue("csmaRate", "CSMA channel data rate for both LANs", cfg.csmaRate);
    cmd.AddValue("csmaDelay", "CSMA propagation delay, e.g. 500ns or 2ms", cfg.csmaDelay);
    cmd.AddValue("simTime", "Total simulation time (s)", cfg.simTime);
    cmd.AddValue("appStart", "Time traffic starts (s)", cfg.appStart);
    cmd.AddValue("drainTime", "Traffic stops this many seconds before simTime", cfg.drainTime);
    cmd.AddValue("packetSize", "UDP payload size (bytes)", cfg.packetSize);
    cmd.AddValue("loadMbps", "Offered load per flow (Mbps of UDP payload)", cfg.loadMbps);
    cmd.AddValue("flows", "Flows to run: all | intra | inter | comma list of ids 1-6", cfg.flows);
    cmd.AddValue("enableFilter", "Apply the Staff -> AdminPC1 deny rule on the router",
                 cfg.enableFilter);
    cmd.AddValue("filterSendIcmp",
                 "Denied packets go through the router error path (Drop trace + ICMP)",
                 cfg.filterSendIcmp);
    cmd.AddValue("seed", "Random seed", cfg.seed);
    cmd.AddValue("run", "Random run number", cfg.run);
    cmd.AddValue("outDir", "Directory for result files (empty = print only)", cfg.outDir);
    cmd.Parse(argc, argv);

    if (cfg.ActiveDuration() <= 0 || cfg.loadMbps <= 0 || cfg.packetSize < 12)
    {
        std::cerr << "error: invalid timing, load or packet size" << std::endl;
        return 2;
    }
    std::vector<FlowSpec> flows;
    try
    {
        flows = SelectFlows(cfg.flows);
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: bad --flows value: " << e.what() << std::endl;
        return 2;
    }

    RngSeedManager::SetSeed(cfg.seed);
    RngSeedManager::SetRun(cfg.run);

    // ----------------------------------------------------------------- nodes
    NodeContainer hosts;
    hosts.Create(NUM_HOSTS);
    Ptr<Node> router = CreateObject<Node>();

    NodeContainer adminLan(hosts.Get(ADMIN_PC1), hosts.Get(ADMIN_PC2), router);
    NodeContainer staffLan(hosts.Get(STAFF_PC1), hosts.Get(STAFF_PC2), router);

    // ------------------------------------------------------------- channels
    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", StringValue(cfg.csmaRate));
    csma.SetChannelAttribute("Delay", TimeValue(Time(cfg.csmaDelay)));
    NetDeviceContainer adminDevs = csma.Install(adminLan); // [pc1, pc2, router]
    NetDeviceContainer staffDevs = csma.Install(staffLan); // [pc1, pc2, router]

    // ------------------------------------------------------------ IP stacks
    InternetStackHelper hostStack;
    hostStack.Install(hosts);

    InternetStackHelper routerStack;
    routerStack.Install(router);

    // ------------------------------------------------------------ addresses
    AssignAddress(adminDevs.Get(0), addr::kAdminPc1, addr::kMask);
    AssignAddress(adminDevs.Get(1), addr::kAdminPc2, addr::kMask);
    AssignAddress(adminDevs.Get(2), addr::kAdminRouter, addr::kMask);
    AssignAddress(staffDevs.Get(0), addr::kStaffPc1, addr::kMask);
    AssignAddress(staffDevs.Get(1), addr::kStaffPc2, addr::kMask);
    AssignAddress(staffDevs.Get(2), addr::kStaffRouter, addr::kMask);

    // -------------------------------------------------------------- routing
    // ns-3 global routing: computes shortest paths over the whole topology
    // and installs routes on every node (hosts get routes to the other
    // subnet via the router). IP forwarding on the router is enabled by the
    // Ipv4 "IpForward" attribute (true by default) and checked below.
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // ------------------------------------------------------- access control
    // Equivalent of the proposal's Cisco extended ACL, applied on the router:
    //   Rule 1: deny   192.168.20.0/24 -> 192.168.10.10/32
    //   Rule 2: permit 192.168.20.0/24 -> 192.168.10.11/32
    //   Rule 3: permit any (implicit)
    // The filter is always installed; --enableFilter only switches it on, so
    // the baseline and filtered runs use exactly the same code path.
    Ptr<AclFilterRouting> acl = CreateObject<AclFilterRouting>();
    acl->AddRule({Ipv4Address(addr::kStaffNet), Ipv4Mask(addr::kMask),
                  Ipv4Address(addr::kAdminPc1), Ipv4Mask("255.255.255.255"), AclAction::DENY});
    acl->AddRule({Ipv4Address(addr::kStaffNet), Ipv4Mask(addr::kMask),
                  Ipv4Address(addr::kAdminPc2), Ipv4Mask("255.255.255.255"), AclAction::PERMIT});
    acl->SetEnabled(cfg.enableFilter);
    acl->SetSendIcmp(cfg.filterSendIcmp);
    auto routerList = DynamicCast<Ipv4ListRouting>(router->GetObject<Ipv4>()->GetRoutingProtocol());
    routerList->AddRoutingProtocol(acl, 100); // highest priority = consulted first

    // ----------------------------------------------- structural validation
    Check(hosts.GetN() == 4, "exactly 4 end-host nodes");
    Check(router->GetObject<Ipv4>()->GetNInterfaces() == 3,
          "router has 3 IPv4 interfaces (loopback + Admin + Staff)");
    Check(adminDevs.Get(0)->GetChannel() == adminDevs.Get(2)->GetChannel() &&
              staffDevs.Get(0)->GetChannel() == staffDevs.Get(2)->GetChannel() &&
              adminDevs.Get(0)->GetChannel() != staffDevs.Get(0)->GetChannel(),
          "Admin and Staff are separate CSMA channels, router attached to both");
    for (uint32_t h = 0; h < NUM_HOSTS; ++h)
    {
        Ipv4Address got = hosts.Get(h)->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();
        Check(got == Ipv4Address(HostAddress(h)),
              std::string(HostName(h)) + " has address " + HostAddress(h));
    }
    {
        Ptr<Ipv4> rip = router->GetObject<Ipv4>();
        Check(rip->GetAddress(1, 0).GetLocal() == Ipv4Address(addr::kAdminRouter) &&
                  rip->GetAddress(2, 0).GetLocal() == Ipv4Address(addr::kStaffRouter),
              "router interfaces are 192.168.10.1 and 192.168.20.1");
        Check(rip->IsForwarding(1) && rip->IsForwarding(2),
              "IP forwarding enabled on both router interfaces");
    }
    {
        int16_t prio = 0;
        Check(routerList->GetRoutingProtocol(0, prio) == acl && prio == 100,
              "access-control filter is the first protocol in the router's list routing");
    }
    CheckRoute(hosts.Get(ADMIN_PC1), "AdminPC1", addr::kAdminPc2, nullptr);
    CheckRoute(hosts.Get(STAFF_PC1), "StaffPC1", addr::kStaffPc2, nullptr);
    CheckRoute(hosts.Get(STAFF_PC1), "StaffPC1", addr::kAdminPc1, addr::kStaffRouter);
    CheckRoute(hosts.Get(STAFF_PC2), "StaffPC2", addr::kAdminPc2, addr::kStaffRouter);
    CheckRoute(hosts.Get(ADMIN_PC1), "AdminPC1", addr::kStaffPc1, addr::kAdminRouter);

    // --------------------------------------------------------- router traces
    // Independent evidence of what the router received and forwarded.
    Ptr<Ipv4L3Protocol> routerIp = router->GetObject<Ipv4L3Protocol>();
    routerIp->TraceConnectWithoutContext("Rx", MakeCallback(&RouterRx));
    routerIp->TraceConnectWithoutContext("UnicastForward", MakeCallback(&RouterForward));
    routerIp->TraceConnectWithoutContext("Drop", MakeCallback(&RouterDrop));

    // ----------------------------------------------------------- traffic
    // Constant-bit-rate UDP: interval = payload bits / offered rate.
    Time interval = Seconds(cfg.packetSize * 8.0 / (cfg.loadMbps * 1e6));
    Ptr<UniformRandomVariable> startJitter = CreateObject<UniformRandomVariable>();
    startJitter->SetAttribute("Min", DoubleValue(0.0));
    startJitter->SetAttribute("Max", DoubleValue(0.01));
    startJitter->SetStream(1);

    std::vector<Ptr<UdpClient>> clients;
    std::vector<Ptr<UdpServer>> servers;
    for (const auto& f : flows)
    {
        UdpServerHelper server(f.port);
        ApplicationContainer s = server.Install(hosts.Get(f.dst));
        s.Start(Seconds(0.0));
        s.Stop(Seconds(cfg.simTime));
        servers.push_back(DynamicCast<UdpServer>(s.Get(0)));

        UdpClientHelper client(Ipv4Address(HostAddress(f.dst)), f.port);
        client.SetAttribute("MaxPackets", UintegerValue(0)); // unlimited until Stop
        client.SetAttribute("Interval", TimeValue(interval));
        client.SetAttribute("PacketSize", UintegerValue(cfg.packetSize));
        ApplicationContainer c = client.Install(hosts.Get(f.src));
        c.Start(Seconds(cfg.appStart + startJitter->GetValue()));
        c.Stop(Seconds(cfg.AppStop()));
        clients.push_back(DynamicCast<UdpClient>(c.Get(0)));
    }

    // -------------------------------------------------------- flow monitor
    FlowMonitorHelper fmHelper;
    Ptr<FlowMonitor> monitor = fmHelper.InstallAll();

    if (!cfg.outDir.empty())
    {
        std::filesystem::create_directories(cfg.outDir);
        Ipv4RoutingHelper::PrintRoutingTableAllAt(
            Seconds(cfg.appStart),
            Create<OutputStreamWrapper>(cfg.outDir + "/routing-tables.txt", std::ios::out));
    }

    Simulator::Stop(Seconds(cfg.simTime));
    Simulator::Run();

    // ------------------------------------------------------------ results
    // Packets still in flight when traffic stops get `drainTime` seconds to
    // arrive; anything older than that is counted lost by FlowMonitor.
    monitor->CheckForLostPackets(Seconds(cfg.drainTime));
    auto classifier = DynamicCast<Ipv4FlowClassifier>(fmHelper.GetClassifier());
    const auto& stats = monitor->GetFlowStats();

    std::ostringstream csv;
    csv << "experiment_id,flow_id,source,destination,source_ip,destination_ip,dst_port,"
           "offered_load_mbps,filter_enabled,csma_rate,csma_delay,packet_size,seed,run,duration_s,"
           "app_tx_packets,app_rx_packets,tx_packets,rx_packets,lost_packets,"
           "blocked_packets,router_rx_packets,router_forwarded_packets,router_drop_trace_packets,"
           "throughput_mbps,"
           "packet_delivery_ratio,packet_loss_ratio,average_delay_ms,average_jitter_ms\n";

    std::cout << "\nflow  src->dst              tx     rx   PDR%    thr(Mbps)  delay(ms)\n";
    for (size_t i = 0; i < flows.size(); ++i)
    {
        const FlowSpec& f = flows[i];
        Ipv4Address src(HostAddress(f.src));
        Ipv4Address dst(HostAddress(f.dst));

        FlowMonitor::FlowStats fs; // zero-initialised if FlowMonitor never saw the flow
        for (const auto& [flowId, s] : stats)
        {
            auto t = classifier->FindFlow(flowId);
            if (t.sourceAddress == src && t.destinationAddress == dst &&
                t.destinationPort == f.port && t.protocol == kUdp)
            {
                fs = s;
            }
        }
        uint64_t appTx = clients[i]->GetTotalTx() / cfg.packetSize;
        uint64_t appRx = servers[i]->GetReceived();
        const RouterCounters& rc = g_router[{src.Get(), dst.Get(), f.port}];
        uint64_t blocked = acl->GetDeniedForFlow({src.Get(), dst.Get(), f.port});

        double tput = fs.rxBytes * 8.0 / (cfg.ActiveDuration() * 1e6);
        double pdr = fs.txPackets ? 100.0 * fs.rxPackets / fs.txPackets : NAN;
        double loss = fs.txPackets ? 100.0 * (fs.txPackets - fs.rxPackets) / fs.txPackets : NAN;
        double delay = fs.rxPackets ? fs.delaySum.GetSeconds() * 1e3 / fs.rxPackets : NAN;
        double jitter =
            fs.rxPackets > 1 ? fs.jitterSum.GetSeconds() * 1e3 / (fs.rxPackets - 1) : NAN;

        csv << cfg.experiment << ',' << f.id << ',' << HostName(f.src) << ','
            << HostName(f.dst) << ',' << src << ',' << dst << ',' << f.port << ','
            << cfg.loadMbps << ',' << (cfg.enableFilter ? "true" : "false") << ','
            << cfg.csmaRate << ',' << cfg.csmaDelay << ',' << cfg.packetSize << ',' << cfg.seed << ',' << cfg.run << ','
            << cfg.ActiveDuration() << ',' << appTx << ',' << appRx << ',' << fs.txPackets << ','
            << fs.rxPackets << ',' << fs.lostPackets << ',' << blocked << ',' << rc.rx << ','
            << rc.forwarded << ',' << rc.dropTrace << ',' << Fmt(tput, 6) << ',' << Fmt(pdr) << ',' << Fmt(loss) << ','
            << Fmt(delay, 6) << ',' << Fmt(jitter, 6) << '\n';

        std::cout << std::setw(4) << f.id << "  " << std::left << std::setw(20)
                  << (std::string(HostName(f.src)) + "->" + HostName(f.dst)) << std::right
                  << std::setw(6) << fs.txPackets << std::setw(7) << fs.rxPackets << std::setw(8)
                  << Fmt(pdr, 2) << std::setw(11) << Fmt(tput, 4) << std::setw(11)
                  << Fmt(delay, 4) << "\n";

        // Access-control policy checks (counters only, so congestion loss can
        // never be mistaken for a policy drop).
        std::string fid = "flow " + std::to_string(f.id) + ": ";
        bool deniedByPolicy =
            cfg.enableFilter && f.interDepartment && f.dst == ADMIN_PC1;
        if (deniedByPolicy)
        {
            Check(blocked > 0 && blocked == rc.rx, fid + "every packet reaching the router was "
                                                         "blocked (" + std::to_string(blocked) +
                                                         "/" + std::to_string(rc.rx) + ")");
            Check(rc.forwarded == 0, fid + "router forwarded 0 packets");
            Check(appRx == 0, fid + "destination received 0 packets");
            if (cfg.filterSendIcmp)
            {
                Check(rc.dropTrace == blocked, fid + "router Drop trace saw every blocked packet");
            }
        }
        else
        {
            Check(blocked == 0, fid + "no packets blocked by the filter");
            if (f.interDepartment)
            {
                Check(rc.forwarded == rc.rx, fid + "router forwarded every packet it received");
            }
        }

        // Internal consistency: FlowMonitor and the applications must agree.
        Check(appTx == fs.txPackets, "flow " + std::to_string(f.id) +
                                         ": app tx == FlowMonitor tx (" + std::to_string(appTx) +
                                         ")");
        Check(appRx == fs.rxPackets, "flow " + std::to_string(f.id) +
                                         ": app rx == FlowMonitor rx (" + std::to_string(appRx) +
                                         ")");
    }

    if (!cfg.outDir.empty())
    {
        std::ofstream(cfg.outDir + "/flows.csv") << csv.str();
        std::ofstream meta(cfg.outDir + "/metadata.txt");
        meta << "ns3_version=3.49\nexperiment=" << cfg.experiment
             << "\ncsma_rate=" << cfg.csmaRate << "\ncsma_delay=" << cfg.csmaDelay
             << "\nsim_time_s=" << cfg.simTime << "\napp_start_s=" << cfg.appStart
             << "\napp_stop_s=" << cfg.AppStop() << "\npacket_size_bytes=" << cfg.packetSize
             << "\noffered_load_mbps_per_flow=" << cfg.loadMbps << "\nflows=" << cfg.flows
             << "\nfilter_enabled=" << (cfg.enableFilter ? "true" : "false")
             << "\nfilter_send_icmp=" << (cfg.filterSendIcmp ? "true" : "false")
             << "\nfilter_denied_total=" << acl->GetDenied()
             << "\nfilter_permitted_total=" << acl->GetPermitted()
             << "\nseed=" << cfg.seed << "\nrun=" << cfg.run << "\nrouting=Ipv4GlobalRouting\n";
        std::ofstream checks(cfg.outDir + "/validation.txt");
        for (const auto& line : g_checkLog)
        {
            checks << line << '\n';
        }
        monitor->SerializeToXmlFile(cfg.outDir + "/flowmon.xml", false, false);
    }

    std::cout << "\nfilter: " << (cfg.enableFilter ? "ENABLED" : "disabled")
              << "  denied=" << acl->GetDenied() << "  permitted=" << acl->GetPermitted()
              << std::endl;
    Simulator::Destroy();
    std::cout << "checks failed: " << g_failedChecks << std::endl;
    return g_failedChecks == 0 ? 0 : 1;
}
