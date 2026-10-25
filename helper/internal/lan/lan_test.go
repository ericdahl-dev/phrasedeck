package lan

import (
	"io"
	"net"
	"net/netip"
	"testing"
	"time"
)

func TestParsePrefixesAcceptsPrivateSubnets(t *testing.T) {
	got, err := ParsePrefixes([]string{"192.168.1.0/24", " 10.0.0.0/8 ", "fd00::/64", "192.168.1.7/24"})
	if err != nil {
		t.Fatal(err)
	}
	want := []string{"192.168.1.0/24", "10.0.0.0/8", "fd00::/64", "192.168.1.0/24"}
	for i, p := range got {
		if p.String() != want[i] {
			t.Errorf("prefix %d = %v, want %v", i, p, want[i])
		}
	}
}

func TestParsePrefixesRefusesPublicOrTooWide(t *testing.T) {
	for _, s := range []string{"0.0.0.0/0", "8.8.8.0/24", "192.0.0.0/2", "::/0", "2001:db8::/32", "100.64.0.0/10", "nonsense", "192.168.1.1"} {
		if _, err := ParsePrefixes([]string{s}); err == nil {
			t.Errorf("ParsePrefixes(%q) accepted it", s)
		}
	}
}

// accepts reports whether a loopback connection gets through a listener allowing prefixes.
func accepts(t *testing.T, prefixes ...string) bool {
	t.Helper()
	var ps []netip.Prefix
	for _, p := range prefixes {
		ps = append(ps, netip.MustParsePrefix(p))
	}
	ln, err := Listen("127.0.0.1:0", ps)
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	accepted := make(chan net.Conn, 1)
	go func() {
		if c, err := ln.Accept(); err == nil {
			accepted <- c
		}
	}()
	c, err := net.Dial("tcp", ln.Addr().String())
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	select {
	case a := <-accepted:
		a.Close()
		return true
	case <-time.After(300 * time.Millisecond):
		c.SetReadDeadline(time.Now().Add(time.Second))
		if _, err := c.Read(make([]byte, 1)); err != io.EOF {
			t.Fatalf("dropped connection not closed: %v", err)
		}
		return false
	}
}

func TestListenAcceptsPeersInTheSubnet(t *testing.T) {
	// ParsePrefixes refuses loopback; Listen itself takes any prefix, which lets this test
	// stand loopback in for the home subnet.
	if !accepts(t, "127.0.0.0/8") {
		t.Fatal("a peer inside -lan was dropped")
	}
}

func TestListenDropsPeersOutsideTheSubnet(t *testing.T) {
	if accepts(t, "10.0.0.0/8") {
		t.Fatal("a peer outside -lan was accepted")
	}
}

func TestLocalAddrsOnlyInsideThePrefixes(t *testing.T) {
	addrs := LocalAddrs([]netip.Prefix{netip.MustParsePrefix("127.0.0.0/8")})
	if len(addrs) == 0 {
		t.Fatal("no loopback address found")
	}
	for _, a := range addrs {
		if !a.IP.IsLoopback() || a.Iface == "" {
			t.Errorf("unexpected %+v", a)
		}
	}
	if got := LocalAddrs([]netip.Prefix{netip.MustParsePrefix("198.18.0.0/15")}); len(got) != 0 {
		t.Errorf("addresses outside the prefixes: %+v", got)
	}
}
