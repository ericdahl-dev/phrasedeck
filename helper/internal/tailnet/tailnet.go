// Package tailnet keeps the helper reachable only from Tailscale peers.
package tailnet

import (
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net"
	"net/netip"
	"os/exec"
	"strings"
	"sync"
)

var (
	cgnat  = netip.MustParsePrefix("100.64.0.0/10")
	ula    = netip.MustParsePrefix("fd7a:115c:a1e0::/48")
	maxLog = 1024
)

// Allowed reports whether ip is a Tailscale address.
func Allowed(ip netip.Addr) bool {
	ip = ip.Unmap()
	return cgnat.Contains(ip) || ula.Contains(ip)
}

// Listen listens on addr but closes, without a response, every connection whose remote
// address isn't Allowed.
func Listen(addr string) (net.Listener, error) {
	ln, err := net.Listen("tcp", addr)
	if err != nil {
		return nil, err
	}
	return &filter{Listener: ln, allow: Allowed, seen: map[netip.Addr]bool{}}, nil
}

type filter struct {
	net.Listener
	allow func(netip.Addr) bool
	mu    sync.Mutex
	seen  map[netip.Addr]bool
}

func (f *filter) Accept() (net.Conn, error) {
	for {
		c, err := f.Listener.Accept()
		if err != nil {
			return nil, err
		}
		ap, err := netip.ParseAddrPort(c.RemoteAddr().String())
		if err == nil && f.allow(ap.Addr()) {
			return c, nil
		}
		c.Close()
		f.logOnce(ap.Addr())
	}
}

// logOnce logs a dropped address the first time it's seen.
// shortcut: stops logging new addresses after maxLog, so a scan can't grow the map forever.
func (f *filter) logOnce(ip netip.Addr) {
	f.mu.Lock()
	defer f.mu.Unlock()
	if f.seen[ip] || len(f.seen) >= maxLog {
		return
	}
	f.seen[ip] = true
	log.Printf("dropped connection from %v (not a tailnet address)", ip)
}

// ParseSelf returns this machine's names from `tailscale status --json`: its host name, the
// short and full MagicDNS names, and its tailnet IPs, all lowercase.
func ParseSelf(statusJSON []byte) ([]string, error) {
	var st struct {
		Self *struct {
			HostName     string
			DNSName      string
			TailscaleIPs []string
		}
	}
	if err := json.Unmarshal(statusJSON, &st); err != nil {
		return nil, fmt.Errorf("tailscale status: %w", err)
	}
	if st.Self == nil {
		return nil, errors.New("tailscale status has no Self (is Tailscale running and logged in?)")
	}
	var names []string
	add := func(n string) {
		n = strings.ToLower(strings.TrimSuffix(n, "."))
		for _, have := range names {
			if have == n {
				return
			}
		}
		if n != "" {
			names = append(names, n)
		}
	}
	dns := strings.TrimSuffix(st.Self.DNSName, ".")
	short, _, _ := strings.Cut(dns, ".")
	add(short)
	add(st.Self.HostName)
	add(dns)
	for _, ip := range st.Self.TailscaleIPs {
		add(ip)
	}
	return names, nil
}

// SelfNames runs `tailscale status --json` and returns ParseSelf's names.
func SelfNames(tailscalePath string) ([]string, error) {
	out, err := exec.Command(tailscalePath, "status", "--json").Output()
	if err != nil {
		return nil, fmt.Errorf("%s status --json: %w", tailscalePath, err)
	}
	return ParseSelf(out)
}

// HostAllowed reports whether a request's Host header, with any port removed, is one of allow.
func HostAllowed(host string, allow []string) bool {
	if h, _, err := net.SplitHostPort(host); err == nil {
		host = h
	}
	host = strings.ToLower(strings.TrimSuffix(host, "."))
	if host == "" {
		return false
	}
	for _, a := range allow {
		if host == strings.ToLower(a) {
			return true
		}
	}
	return false
}
