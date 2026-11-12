/*
 * network-config.h — experiment parameters, addressing plan and flow table
 * for the Small Office Network simulation (COMP 3343, Acadia University).
 *
 * Everything that an experiment may want to change lives here and is
 * exposed as a command-line option in office-network.cc.
 */
#ifndef SMALL_OFFICE_NETWORK_CONFIG_H
#define SMALL_OFFICE_NETWORK_CONFIG_H

#include "ns3/ipv4-address.h"

#include <cstdint>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace smalloffice
{

/// Simulation parameters (defaults are the proposal's suggested values).
struct SimConfig
{
    std::string experiment = "adhoc"; ///< experiment id written to the output
    std::string csmaRate = "100Mbps"; ///< CSMA channel data rate (Fast Ethernet)
    /// CSMA propagation delay. 500 ns ~ 100 m of copper (realistic office LAN).
    /// The proposal suggested 2 ms; see docs/experiment-design.md for why that
    /// value throttles ns-3's CSMA channel, and use --csmaDelay=2ms to reproduce it.
    std::string csmaDelay = "500ns";
    double simTime = 60.0;            ///< total simulated seconds
    double appStart = 1.0;            ///< traffic starts (s) — lets ARP/routing settle
    double drainTime = 2.0;           ///< traffic stops this long before simTime
    uint32_t packetSize = 1024;       ///< UDP payload bytes per packet
    double loadMbps = 1.0;            ///< offered load per flow (application payload)
    std::string flows = "all";        ///< "all", "intra", "inter" or "1,3,5"
    bool enableFilter = false;        ///< apply the Staff→Admin-PC1 deny rule
    bool filterSendIcmp = false;      ///< send ICMP unreachable for denied packets
    uint32_t seed = 1;                ///< RngSeedManager seed
    uint32_t run = 1;                 ///< RngSeedManager run number
    std::string outDir = "";          ///< where result files go ("" = none)

    double AppStop() const
    {
        return simTime - drainTime;
    }

    double ActiveDuration() const
    {
        return AppStop() - appStart;
    }
};

/// Addressing plan from the original proposal (must be preserved).
namespace addr
{
inline const char* kMask = "255.255.255.0";
inline const char* kAdminNet = "192.168.10.0";
inline const char* kAdminRouter = "192.168.10.1";
inline const char* kAdminPc1 = "192.168.10.10";
inline const char* kAdminPc2 = "192.168.10.11";
inline const char* kStaffNet = "192.168.20.0";
inline const char* kStaffRouter = "192.168.20.1";
inline const char* kStaffPc1 = "192.168.20.10";
inline const char* kStaffPc2 = "192.168.20.11";
} // namespace addr

/// Host indices (order in the host NodeContainer).
enum Host : uint32_t
{
    ADMIN_PC1 = 0,
    ADMIN_PC2 = 1,
    STAFF_PC1 = 2,
    STAFF_PC2 = 3,
    NUM_HOSTS = 4
};

inline const char*
HostName(uint32_t h)
{
    static const char* names[] = {"AdminPC1", "AdminPC2", "StaffPC1", "StaffPC2"};
    return names[h];
}

inline const char*
HostAddress(uint32_t h)
{
    static const char* ips[] = {addr::kAdminPc1, addr::kAdminPc2, addr::kStaffPc1, addr::kStaffPc2};
    return ips[h];
}

/// One UDP flow. Each flow has its own destination port so flows never
/// collide and FlowMonitor's 5-tuple identifies each one uniquely.
struct FlowSpec
{
    uint32_t id;
    uint32_t src;
    uint32_t dst;
    uint16_t port;
    bool interDepartment;
};

/// The six traffic pairs required by the project brief.
inline const std::vector<FlowSpec>&
AllFlows()
{
    static const std::vector<FlowSpec> flows = {
        {1, ADMIN_PC1, ADMIN_PC2, 5001, false},
        {2, STAFF_PC1, STAFF_PC2, 5002, false},
        {3, STAFF_PC1, ADMIN_PC1, 5003, true},
        {4, STAFF_PC1, ADMIN_PC2, 5004, true},
        {5, STAFF_PC2, ADMIN_PC1, 5005, true},
        {6, STAFF_PC2, ADMIN_PC2, 5006, true},
    };
    return flows;
}

/// Turns the --flows option into a list of flows.
inline std::vector<FlowSpec>
SelectFlows(const std::string& selection)
{
    std::vector<FlowSpec> out;
    if (selection == "all" || selection == "intra" || selection == "inter")
    {
        for (const auto& f : AllFlows())
        {
            if (selection == "all" || (selection == "inter") == f.interDepartment)
            {
                out.push_back(f);
            }
        }
        return out;
    }
    std::set<uint32_t> ids;
    std::stringstream ss(selection);
    std::string item;
    while (std::getline(ss, item, ','))
    {
        uint32_t id = std::stoul(item);
        if (id < 1 || id > AllFlows().size())
        {
            throw std::invalid_argument("flow id out of range (1-6): " + item);
        }
        ids.insert(id);
    }
    for (uint32_t id : ids)
    {
        out.push_back(AllFlows()[id - 1]);
    }
    return out;
}

} // namespace smalloffice

#endif
