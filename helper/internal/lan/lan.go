// Package lan is phrased's optional home-network route: an HTTPS listener for -lan subnets,
// its self-signed certificate, and its Bonjour advertisement.
package lan

import (
	"fmt"
	"net"
	"net/netip"
	"strings"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/tailnet"
)

// DefaultPort is the LAN listener's port.
const DefaultPort = 47812

// private are the ranges a -lan subnet must fall inside, so a typo like 0.0.0.0/0 can't put
// the helper on the internet.
var private = []netip.Prefix{
	netip.MustParsePrefix("10.0.0.0/8"),
	netip.MustParsePrefix("172.16.0.0/12"),
	netip.MustParsePrefix("192.168.0.0/16"),
	netip.MustParsePrefix("fc00::/7"),
}

// ParsePrefixes parses -lan values such as "192.168.1.0/24". Each must lie inside a private
// range; host bits are cleared.
func ParsePrefixes(values []string) ([]netip.Prefix, error) {
	var out []netip.Prefix
	for _, v := range values {
		p, err := netip.ParsePrefix(strings.TrimSpace(v))
		if err != nil {
			return nil, fmt.Errorf("-lan %q: want a subnet like 192.168.1.0/24", v)
		}
		p = p.Masked()
		if !insidePrivate(p) {
			return nil, fmt.Errorf("-lan %v: not a private home subnet (10/8, 172.16/12, 192.168/16 or fc00::/7)", p)
		}
		out = append(out, p)
	}
	return out, nil
}

func insidePrivate(p netip.Prefix) bool {
	for _, r := range private {
		if r.Bits() <= p.Bits() && r.Contains(p.Addr()) {
			return true
		}
	}
	return false
}

// Contains reports whether ip is in any of prefixes.
func Contains(prefixes []netip.Prefix, ip netip.Addr) bool {
	ip = ip.Unmap()
	for _, p := range prefixes {
		if p.Contains(ip) {
			return true
		}
	}
	return false
}

// Listen listens on addr and closes, without a response, every connection that comes from
// neither a tailnet peer nor one of prefixes.
func Listen(addr string, prefixes []netip.Prefix) (net.Listener, error) {
	return tailnet.ListenAllowing(addr, func(ip netip.Addr) bool {
		return tailnet.Allowed(ip) || Contains(prefixes, ip)
	})
}

// Addr is one of this machine's addresses and the interface it's on.
type Addr struct {
	IP    net.IP
	Iface string
}

// LocalAddrs returns this machine's addresses inside prefixes.
// shortcut: read once at startup, so a DHCP change needs a restart; re-read on a timer if
// that bites.
func LocalAddrs(prefixes []netip.Prefix) []Addr {
	ifaces, err := net.Interfaces()
	if err != nil {
		return nil
	}
	var out []Addr
	for _, ifc := range ifaces {
		if ifc.Flags&net.FlagUp == 0 {
			continue
		}
		addrs, _ := ifc.Addrs()
		for _, a := range addrs {
			ipnet, ok := a.(*net.IPNet)
			if !ok {
				continue
			}
			ip, ok := netip.AddrFromSlice(ipnet.IP)
			if ok && Contains(prefixes, ip) {
				out = append(out, Addr{IP: ipnet.IP, Iface: ifc.Name})
			}
		}
	}
	return out
}
