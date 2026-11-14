#include "netplay_router.hpp"

#include <iostream>
#include <string>

#if defined(MODERNGEKKO_UPNP)
#include <miniupnpc.h>
#include <upnpcommands.h>
#include <upnperrors.h>
#endif

namespace moderngekko::frontend
{
// The log says what the router answered, never an address: players attach
// it to reports.
struct RouterPortOpener::Router
{
#if defined(MODERNGEKKO_UPNP)
  UPNPUrls urls{};
  IGDdatas data{};
  bool have_urls = false;
  bool mapped = false;
#endif
};

RouterPortOpener::RouterPortOpener(std::uint16_t port)
    : m_port(port), m_router(std::make_unique<Router>())
{
#if defined(MODERNGEKKO_UPNP) && MINIUPNPC_API_VERSION >= 18
  m_thread = std::thread([this] { Run(); });
#else
  m_state = RouterPort::Unknown;
#endif
}

RouterPortOpener::~RouterPortOpener()
{
  if (m_thread.joinable())
    m_thread.join();
#if defined(MODERNGEKKO_UPNP)
  if (m_router->mapped)
  {
    const std::string port = std::to_string(m_port);
    const int result = UPNP_DeletePortMapping(m_router->urls.controlURL,
                                              m_router->data.first.servicetype, port.c_str(),
                                              "UDP", nullptr);
    std::cerr << "netplay: router: "
              << (result == UPNPCOMMAND_SUCCESS ? "closed UDP port " : "could not close UDP port ")
              << m_port << '\n';
  }
  if (m_router->have_urls)
    FreeUPNPUrls(&m_router->urls);
#endif
}

void RouterPortOpener::Run()
{
#if defined(MODERNGEKKO_UPNP) && MINIUPNPC_API_VERSION >= 18
  // Routers answer within a second, or never. With no answer, miniupnpc asks
  // four ways in turn: about four seconds, which closing the room may wait for.
  int error = 0;
  UPNPDev* devices = upnpDiscover(1000, nullptr, nullptr, 0, 0, 2, &error);
  if (!devices)
  {
    std::cerr << "netplay: router: no UPnP answer (it may be turned off on the router)\n";
    m_state = RouterPort::Unknown;
    return;
  }
  char lan_address[64] = {};
  char wan_address[64] = {};
  const int found = UPNP_GetValidIGD(devices, &m_router->urls, &m_router->data, lan_address,
                                     sizeof(lan_address), wan_address, sizeof(wan_address));
  freeUPNPDevlist(devices);
  m_router->have_urls = found != UPNP_NO_IGD;
  if (found != UPNP_CONNECTED_IGD && found != UPNP_PRIVATEIP_IGD)
  {
    std::cerr << "netplay: router: no internet gateway answered UPnP\n";
    m_state = RouterPort::Unknown;
    return;
  }
  // An internet address shared with other customers, or another router's:
  // an opened port would still be out of reach.
  if (found == UPNP_PRIVATEIP_IGD || RouterAddressIsShared(wan_address))
  {
    std::cerr << "netplay: router: its internet address is shared (CGNAT) or private, "
                 "so friends in other houses may not reach this computer\n";
    m_state = RouterPort::Shared;
    return;
  }
  const std::string port = std::to_string(m_port);
  const int result = UPNP_AddPortMapping(m_router->urls.controlURL,
                                         m_router->data.first.servicetype, port.c_str(),
                                         port.c_str(), lan_address, "Netplay (ModernGekko)",
                                         "UDP", nullptr, "0");
  if (result != UPNPCOMMAND_SUCCESS)
  {
    std::cerr << "netplay: router: did not open UDP port " << m_port << " ("
              << strupnperror(result) << ")\n";
    m_state = RouterPort::Refused;
    return;
  }
  m_router->mapped = true;
  std::cerr << "netplay: router: opened UDP port " << m_port << '\n';
  m_state = RouterPort::Opened;
#endif
}
}  // namespace moderngekko::frontend
