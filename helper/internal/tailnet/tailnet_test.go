package tailnet

import (
	"io"
	"net"
	"net/netip"
	"reflect"
	"testing"
	"time"
)

func TestAllowed(t *testing.T) {
	for ip, want := range map[string]bool{
		"100.64.0.1":         true,
		"100.127.255.254":    true,
		"fd7a:115c:a1e0::1":  true,
		"::ffff:100.64.0.1":  true,
		"100.63.255.255":     false,
		"100.128.0.0":        false,
		"127.0.0.1":          false,
		"192.168.1.5":        false,
		"10.0.0.1":           false,
		"8.8.8.8":            false,
		"::1":                false,
		"fe80::1":            false,
		"fd7a:115c:a1e1::1":  false,
		"::ffff:192.168.1.5": false,
	} {
		if got := Allowed(netip.MustParseAddr(ip)); got != want {
			t.Errorf("Allowed(%s) = %v, want %v", ip, got, want)
		}
	}
}

func TestListenDropsNonTailnetPeers(t *testing.T) {
	ln, err := Listen("127.0.0.1:0")
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
	c.SetReadDeadline(time.Now().Add(2 * time.Second))
	if _, err := c.Read(make([]byte, 1)); err != io.EOF {
		t.Fatalf("want the connection closed (EOF), got %v", err)
	}
	select {
	case <-accepted:
		t.Fatal("Accept returned a loopback connection")
	case <-time.After(50 * time.Millisecond):
	}
}

func TestFilterPassesAllowedPeers(t *testing.T) {
	inner, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	ln := &filter{Listener: inner, allow: func(netip.Addr) bool { return true }, seen: map[netip.Addr]bool{}}
	defer ln.Close()
	go func() {
		if c, err := net.Dial("tcp", ln.Addr().String()); err == nil {
			defer c.Close()
			time.Sleep(time.Second)
		}
	}()
	c, err := ln.Accept()
	if err != nil {
		t.Fatal(err)
	}
	c.Close()
}

const statusFixture = `{
  "Version": "1.76.0",
  "Self": {
    "HostName": "Erics-MacBook-Pro",
    "DNSName": "erics-macbook-pro.tail1234.ts.net.",
    "TailscaleIPs": ["100.101.102.103", "fd7a:115c:a1e0::abcd"]
  },
  "Peer": {"x": {"HostName": "omarchy-laptop", "DNSName": "omarchy-laptop.tail1234.ts.net."}}
}`

func TestParseSelf(t *testing.T) {
	got, err := ParseSelf([]byte(statusFixture))
	if err != nil {
		t.Fatal(err)
	}
	want := []string{"erics-macbook-pro", "erics-macbook-pro.tail1234.ts.net", "100.101.102.103", "fd7a:115c:a1e0::abcd"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %v want %v", got, want)
	}
}

func TestParseSelfNeedsSelf(t *testing.T) {
	if _, err := ParseSelf([]byte(`{"Self":null}`)); err == nil {
		t.Fatal("want an error without Self")
	}
}

func TestHostAllowed(t *testing.T) {
	allow := []string{"omarchy-laptop", "omarchy-laptop.tail1234.ts.net", "100.101.102.103", "fd7a:115c:a1e0::abcd"}
	for host, want := range map[string]bool{
		"omarchy-laptop:47811":            true,
		"OMARCHY-LAPTOP":                  true,
		"omarchy-laptop.tail1234.ts.net":  true,
		"omarchy-laptop.tail1234.ts.net.": true,
		"100.101.102.103:47811":           true,
		"[fd7a:115c:a1e0::abcd]:47811":    true,
		"evil.example":                    false,
		"localhost":                       false,
		"":                                false,
		"omarchy-laptop.evil.example":     false,
		"100.101.102.104:47811":           false,
	} {
		if got := HostAllowed(host, allow); got != want {
			t.Errorf("HostAllowed(%q) = %v, want %v", host, got, want)
		}
	}
}
