package main

import (
	"bufio"
	"fmt"
	"net"
	"net/netip"
	"os"
	"path/filepath"
	"strings"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/lan"
)

const (
	// subnetsFile in the config dir lists -lan subnets, one per line; # starts a comment.
	subnetsFile = "lan-subnets"
	// lanAddrFile is where the running helper records its LAN address for `phrased pair`.
	lanAddrFile = "lan-addr"
)

// subnets returns the -lan subnets: the config file's, then the flags'.
func subnets(o options) ([]netip.Prefix, error) {
	var values []string
	if f, err := os.Open(filepath.Join(o.config, subnetsFile)); err == nil {
		sc := bufio.NewScanner(f)
		for sc.Scan() {
			line, _, _ := strings.Cut(sc.Text(), "#")
			if line = strings.TrimSpace(line); line != "" {
				values = append(values, line)
			}
		}
		f.Close()
		if err := sc.Err(); err != nil {
			return nil, fmt.Errorf("%s: %w", subnetsFile, err)
		}
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
