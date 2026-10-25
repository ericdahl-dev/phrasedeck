package main

import (
	"fmt"
	"net"
	"net/netip"
	"strings"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/confdir"
	"github.com/ericdahl-dev/phrasedeck/helper/internal/lan"
)

// subnets returns the -lan subnets: the config file's, then the flags'.
func subnets(o options) ([]netip.Prefix, error) {
	values, err := confdir.New(o.config).Subnets()
	if err != nil {
		return nil, err
	}
	return lan.ParsePrefixes(append(values, o.lan...))
}

// listenLAN opens the LAN listener, or returns nil when no -lan subnet is set.
func listenLAN(o options, prefixes []netip.Prefix) (net.Listener, error) {
	if len(prefixes) == 0 {
		return nil, nil
	}
	return lan.Listen(fmt.Sprintf(":%d", o.lanPort), prefixes)
}

// shortHost is the first label of the machine's host name, lowercase.
func shortHost(hostname string) string {
	short, _, _ := strings.Cut(strings.ToLower(hostname), ".")
	return short
}

// lanHosts are the Host names a LAN request may carry: this machine's LAN IPs, its
// <host>.local name, and the phrased-<host>.local name phrased advertises.
func lanHosts(hostname string, addrs []lan.Addr) []string {
	var out []string
	for _, a := range addrs {
		out = append(out, a.IP.String())
	}
	if short := shortHost(hostname); short != "" {
		out = append(out, short+".local", bonjourHost(hostname)+".local")
	}
	return out
}

// bonjourHost is the .local name phrased advertises, kept apart from the system's own
// <host>.local so phrased never answers for a name mDNSResponder or avahi owns.
func bonjourHost(hostname string) string {
	return "phrased-" + shortHost(hostname)
}

// pairAddr picks the LAN address `phrased pair` puts in the QR code, IPv4 first.
func pairAddr(addrs []lan.Addr) string {
	for _, a := range addrs {
		if a.IP.To4() != nil {
			return a.IP.String()
		}
	}
	if len(addrs) > 0 {
		return addrs[0].IP.String()
	}
	return ""
}
