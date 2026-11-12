/*
 * access-control.h — an extended-ACL-style IPv4 packet filter for the router.
 *
 * ns-3 has no Cisco ACL commands, so the filter is built as a small
 * Ipv4RoutingProtocol that is placed FIRST (highest priority) inside the
 * router's Ipv4ListRouting.
 *
 * Why this works (ns-3.49, src/internet/model/ipv4-list-routing.cc):
 *   Ipv4ListRouting::RouteInput() first hands packets addressed to the router
 *   itself to local delivery. Every other packet — every packet the router is
 *   asked to FORWARD — is offered to the routing protocols in priority order.
 *   The first protocol that returns true takes ownership of the packet.
 *
 *   - DENY:   the filter returns true without forwarding → the packet is
 *             dropped inside the router's IP forwarding path. Optionally the
 *             router's own error callback is invoked, which fires the
 *             Ipv4L3Protocol "Drop" trace (DROP_ROUTE_ERROR) and returns an
 *             ICMP "unreachable" to the sender.
 *   - PERMIT: the filter returns false → global routing forwards it normally.
 *
 * Fields inspected: IPv4 source address and IPv4 destination address
 * (matched against network/mask pairs). The UDP destination port is read only
 * to count blocked packets per flow; it is not part of the decision.
 * Rules are evaluated top-down, first match wins; no match = permit
 * (Rule 3: "other traffic continues normally").
 */
#ifndef SMALL_OFFICE_ACCESS_CONTROL_H
#define SMALL_OFFICE_ACCESS_CONTROL_H

#include "ns3/ipv4-header.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/output-stream-wrapper.h"
#include "ns3/udp-header.h"
#include "ns3/iana-internet-protocol-numbers.h"

#include <map>
#include <ostream>
#include <tuple>
#include <vector>

namespace smalloffice
{

enum class AclAction
{
    PERMIT,
    DENY
};

struct AclRule
{
    ns3::Ipv4Address srcNet;
    ns3::Ipv4Mask srcMask;
    ns3::Ipv4Address dstNet;
    ns3::Ipv4Mask dstMask;
    AclAction action;

    bool Matches(const ns3::Ipv4Header& h) const
    {
        return srcMask.IsMatch(h.GetSource(), srcNet) && dstMask.IsMatch(h.GetDestination(), dstNet);
    }
};

class AclFilterRouting : public ns3::Ipv4RoutingProtocol
{
  public:
    /// (source, destination, UDP destination port) — used for per-flow counts.
    using FlowKey = std::tuple<uint32_t, uint32_t, uint16_t>;

    static ns3::TypeId GetTypeId()
    {
        static ns3::TypeId tid = ns3::TypeId("smalloffice::AclFilterRouting")
                                     .SetParent<ns3::Ipv4RoutingProtocol>()
                                     .SetGroupName("SmallOffice");
        return tid;
    }

    void AddRule(const AclRule& rule)
    {
        m_rules.push_back(rule);
    }

    void SetEnabled(bool enabled)
    {
        m_enabled = enabled;
    }

    void SetSendIcmp(bool sendIcmp)
    {
        m_sendIcmp = sendIcmp;
    }

    uint64_t GetDenied() const
    {
        return m_denied;
    }

    uint64_t GetPermitted() const
    {
        return m_permitted;
    }

    uint64_t GetDeniedForFlow(const FlowKey& key) const
    {
        auto it = m_deniedPerFlow.find(key);
        return it == m_deniedPerFlow.end() ? 0 : it->second;
    }

    // ---------------------------------------------------- Ipv4RoutingProtocol
    ns3::Ptr<ns3::Ipv4Route> RouteOutput(ns3::Ptr<ns3::Packet>,
                                         const ns3::Ipv4Header&,
                                         ns3::Ptr<ns3::NetDevice>,
                                         ns3::Socket::SocketErrno& sockerr) override
    {
        // The filter never originates routes; let the next protocol answer.
        sockerr = ns3::Socket::ERROR_NOROUTETOHOST;
        return nullptr;
    }

    bool RouteInput(ns3::Ptr<const ns3::Packet> p,
                    const ns3::Ipv4Header& header,
                    ns3::Ptr<const ns3::NetDevice>,
                    const UnicastForwardCallback&,
                    const MulticastForwardCallback&,
                    const LocalDeliverCallback&,
                    const ErrorCallback& ecb) override
    {
        if (!m_enabled)
        {
            return false; // filter off: behave exactly like the baseline
        }
        AclAction action = AclAction::PERMIT; // implicit "permit any" (Rule 3)
        for (const auto& rule : m_rules)
        {
            if (rule.Matches(header))
            {
                action = rule.action;
                break; // first match wins, like a Cisco ACL
            }
        }
        if (action == AclAction::PERMIT)
        {
            ++m_permitted;
            return false; // hand the packet on to global routing
        }

        ++m_denied;
        uint16_t port = 0;
        if (header.GetProtocol() == ns3::iana::internetprotocolnumbers::UDP &&
            p->GetSize() >= ns3::UdpHeader().GetSerializedSize())
        {
            ns3::UdpHeader udp;
            p->PeekHeader(udp);
            port = udp.GetDestinationPort();
        }
        ++m_deniedPerFlow[{header.GetSource().Get(), header.GetDestination().Get(), port}];
        if (m_sendIcmp)
        {
            // Router's error path: Drop trace (DROP_ROUTE_ERROR) + ICMP unreachable.
            ecb(p, header, ns3::Socket::ERROR_NOROUTETOHOST);
        }
        return true; // consumed: the packet is NOT forwarded
    }

    void NotifyInterfaceUp(uint32_t) override
    {
    }

    void NotifyInterfaceDown(uint32_t) override
    {
    }

    void NotifyAddAddress(uint32_t, ns3::Ipv4InterfaceAddress) override
    {
    }

    void NotifyRemoveAddress(uint32_t, ns3::Ipv4InterfaceAddress) override
    {
    }

    void SetIpv4(ns3::Ptr<ns3::Ipv4> ipv4) override
    {
        m_ipv4 = ipv4;
    }

    void PrintRoutingTable(ns3::Ptr<ns3::OutputStreamWrapper> stream,
                           ns3::Time::Unit = ns3::Time::S) const override
    {
        std::ostream& os = *stream->GetStream();
        os << "Access-control filter (" << (m_enabled ? "ENABLED" : "disabled") << ")\n";
        for (size_t i = 0; i < m_rules.size(); ++i)
        {
            const auto& r = m_rules[i];
            os << "  rule " << i + 1 << ": " << (r.action == AclAction::DENY ? "deny  " : "permit")
               << " src " << r.srcNet << "/" << r.srcMask.GetPrefixLength() << " dst " << r.dstNet
               << "/" << r.dstMask.GetPrefixLength() << "\n";
        }
        os << "  rule " << m_rules.size() + 1 << ": permit any (implicit)\n";
    }

  private:
    ns3::Ptr<ns3::Ipv4> m_ipv4;
    std::vector<AclRule> m_rules;
    bool m_enabled = false;
    bool m_sendIcmp = false;
    uint64_t m_denied = 0;
    uint64_t m_permitted = 0;
    std::map<FlowKey, uint64_t> m_deniedPerFlow;
};

} // namespace smalloffice

#endif
