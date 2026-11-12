// The Netplay lobby's address rules: which addresses are shown and how they
// are labelled, that the hidden form never contains the address, and that an
// address copied in the lobby can be pasted whole into Join Netplay.
#include "netplay_address.hpp"

#include <string>

int main()
{
  using moderngekko::frontend::AddressKind;
  using moderngekko::frontend::ClassifyAddress;
  using moderngekko::frontend::MaskedAddress;
  using moderngekko::frontend::NormalizeInviteCode;
  using moderngekko::frontend::ParseIPv4;
  using moderngekko::frontend::SpacedAddress;
  using moderngekko::frontend::SplitHostAndPort;

  if (!ParseIPv4("192.168.1.20") || ParseIPv4("192.168.1") || ParseIPv4("192.168.1.256") ||
      ParseIPv4("192.168.1.20.5") || ParseIPv4("a.b.c.d") || ParseIPv4(""))
    return 1;

  // Local networks, Linux and Windows names.
  if (ClassifyAddress("wlp2s0", "192.168.1.20") != AddressKind::Home ||
      ClassifyAddress("enp3s0", "10.0.0.7") != AddressKind::Home ||
      ClassifyAddress("Wi-Fi", "172.20.10.3") != AddressKind::Home)
    return 2;
  // Tailscale, by its interface or by its address range.
  if (ClassifyAddress("tailscale0", "100.101.12.34") != AddressKind::Tailscale ||
      ClassifyAddress("Tailscale", "100.64.0.1") != AddressKind::Tailscale ||
      ClassifyAddress("utun4", "100.127.255.254") != AddressKind::Tailscale ||
      ClassifyAddress("eth0", "100.128.0.1") != AddressKind::Public)
    return 3;
  // A computer plugged straight into the internet.
  if (ClassifyAddress("eth0", "203.0.113.5") != AddressKind::Public)
    return 4;
  // Containers, virtual machines, loopback and link-local are left out.
  if (ClassifyAddress("docker0", "172.17.0.1") != AddressKind::Ignore ||
      ClassifyAddress("br-1a2b3c", "172.18.0.1") != AddressKind::Ignore ||
      ClassifyAddress("virbr0", "192.168.122.1") != AddressKind::Ignore ||
      ClassifyAddress("podman0", "10.88.0.1") != AddressKind::Ignore ||
      ClassifyAddress("vEthernet (WSL)", "172.30.48.1") != AddressKind::Ignore ||
      ClassifyAddress("VirtualBox Host-Only Network", "192.168.56.1") != AddressKind::Ignore ||
      ClassifyAddress("vEthernet (Default Switch)", "172.25.160.1") != AddressKind::Ignore ||
      ClassifyAddress("waydroid0", "192.168.240.1") != AddressKind::Ignore ||
      ClassifyAddress("lxcbr0", "10.0.3.1") != AddressKind::Ignore ||
      ClassifyAddress("Bluetooth Network Connection", "192.168.44.1") != AddressKind::Ignore ||
      ClassifyAddress("lo", "127.0.0.1") != AddressKind::Ignore ||
      ClassifyAddress("eth0", "169.254.3.4") != AddressKind::Ignore ||
      ClassifyAddress("eth0", "not an address") != AddressKind::Ignore)
    return 5;

  if (SpacedAddress("192.168.1.20:2626") != "192.168.1.20 : 2626" ||
      SpacedAddress("no-port") != "no-port")
    return 6;

  // Hidden: the port stays, no digit of the address does, and every address
  // gives the same text.
  const std::string hidden = MaskedAddress("203.0.113.5:2626");
  if (!hidden.ends_with(" : 2626") || hidden.find("203") != std::string::npos ||
      hidden.find("113") != std::string::npos || hidden != MaskedAddress("10.0.0.7:2626"))
    return 7;

  // What Copy gives, pasted into Join Netplay, splits into address and port.
  const auto copied = SplitHostAndPort("192.168.1.20:2626");
  const auto spaced = SplitHostAndPort(" 100.101.12.34 : 7777 ");
  const auto named = SplitHostAndPort("my-pc.tailnet.ts.net:2626");
  if (!copied || copied->host != "192.168.1.20" || copied->port != 2626 || !spaced ||
      spaced->host != "100.101.12.34" || spaced->port != 7777 || !named ||
      named->host != "my-pc.tailnet.ts.net")
    return 8;
  // Anything else stays as typed.
  if (SplitHostAndPort("192.168.1.20") || SplitHostAndPort("192.168.1.20:") ||
      SplitHostAndPort(":2626") || SplitHostAndPort("192.168.1.20:0") ||
      SplitHostAndPort("192.168.1.20:70000") || SplitHostAndPort("192.168.1.20:26a") ||
      SplitHostAndPort("fe80::1") || SplitHostAndPort("[::1]:2626"))
    return 9;

  // Invite codes: as the server gives them, typed in capitals, or spaced.
  if (NormalizeInviteCode("a1b2c3d4") != "a1b2c3d4" ||
      NormalizeInviteCode("A1B2C3D4") != "a1b2c3d4" ||
      NormalizeInviteCode(" a1b2 c3d4 ") != "a1b2c3d4" ||
      NormalizeInviteCode("a1b2-c3d4") != "a1b2c3d4")
    return 10;
  if (NormalizeInviteCode("") || NormalizeInviteCode("a1b2c3d") ||
      NormalizeInviteCode("a1b2c3d4e") || NormalizeInviteCode("a1b2c3dz") ||
      NormalizeInviteCode("192.168.1.20"))
    return 11;
  return 0;
}
