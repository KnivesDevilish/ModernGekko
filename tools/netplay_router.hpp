#pragma once

// The Netplay host's router. While a room is open, the host asks it (with
// UPnP, in the background) to pass what reaches the room's UDP port from the
// internet on to this computer, and to stop when the room closes: friends in
// other houses can then reach the host, with an invite code or its address.
// The router also says what its own internet address is: one an internet
// provider shares between many customers (CGNAT), or one of another router
// in front of it, can't be reached from the internet, opened port or not. The
// lobby tells the host which it is, so the players know who should host.
//
// Without UPnP in the build (MODERNGEKKO_UPNP, the vendored Dolphin's
// miniupnpc), or when no router answers, nothing is asked or said.

#include "netplay_address.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string_view>
#include <thread>

namespace moderngekko::frontend
{
enum class RouterPort
{
  Checking,  // still asking the router
  Unknown,   // no router answered (UPnP off, or not in this build)
  Opened,    // the router opened the port and has an internet address of its own
  Shared,    // the router's internet address is shared (CGNAT) or private
  Refused,   // the router has an internet address of its own, but kept the port closed
};

// Whether a router's internet address (IPv4, dotted) can't be reached from
// the internet: private, shared by an internet provider (100.64.0.0/10),
// link-local, unspecified, or not an address at all.
inline bool RouterAddressIsShared(std::string_view address)
{
  const auto octets = ParseIPv4(address);
  if (!octets)
    return true;
  const int a = (*octets)[0];
  const int b = (*octets)[1];
  return a == 0 || a == 10 || a == 127 || (a == 100 && b >= 64 && b <= 127) ||
         (a == 169 && b == 254) || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168);
}

// Opens `port` (UDP) on the router from a thread of its own, as soon as it is
// made; closes it again when destroyed (which waits for the router's answer:
// about four seconds at most, when no router answers). Make it once
// networking is up (ENet initialized: on Windows that starts Winsock) and
// destroy it before it shuts down.
class RouterPortOpener
{
public:
  explicit RouterPortOpener(std::uint16_t port);
  ~RouterPortOpener();
  RouterPortOpener(const RouterPortOpener&) = delete;
  RouterPortOpener& operator=(const RouterPortOpener&) = delete;

  RouterPort State() const { return m_state.load(); }

private:
  struct Router;  // what the router said (miniupnpc's data), in the .cpp
  void Run();

  std::uint16_t m_port;
  std::atomic<RouterPort> m_state{RouterPort::Checking};
  std::unique_ptr<Router> m_router;
  std::thread m_thread;
};
}  // namespace moderngekko::frontend
