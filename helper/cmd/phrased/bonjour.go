package main

import (
	"context"
	"log"
	"strconv"

	"github.com/brutella/dnssd"
	dnssdlog "github.com/brutella/dnssd/log"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/lan"
)

// bonjourConfig describes the _phrasedeck._tcp service: on the LAN interfaces only, under
// phrased-<host>.local, with a TXT record that names the tailnet host so the app can match a
// computer it already paired. TXT is public, so it carries no token and no fingerprint.
func bonjourConfig(hostname, tsName string, port int, addrs []lan.Addr) dnssd.Config {
	var ifaces []string
	seen := map[string]bool{}
	for _, a := range addrs {
		if !seen[a.Iface] {
			seen[a.Iface] = true
			ifaces = append(ifaces, a.Iface)
		}
	}
	text := map[string]string{"v": "1", "port": strconv.Itoa(port)}
	if tsName != "" {
		text["ts"] = tsName
	}
	return dnssd.Config{
		Name:   shortHost(hostname),
		Type:   "_phrasedeck._tcp",
		Host:   bonjourHost(hostname),
		Port:   port,
		Text:   text,
		Ifaces: ifaces,
	}
}

// advertise announces the service until ctx ends. Failures are logged, not fatal: the LAN
// route still works for a device that already knows the address.
// shortcut: interfaces are read at startup (and dnssd can't watch links on macOS), so a
// network change needs a restart; re-register on a timer if that bites.
func advertise(ctx context.Context, cfg dnssd.Config) {
	if len(cfg.Ifaces) == 0 {
		return
	}
	dnssdlog.Info.SetOutput(log.Writer())
	rp, err := dnssd.NewResponder()
	if err == nil {
		var svc dnssd.Service
		if svc, err = dnssd.NewService(cfg); err == nil {
			_, err = rp.Add(svc)
		}
	}
	if err != nil {
		log.Printf("Bonjour: %v", err)
		return
	}
	log.Printf("advertising %s on %v", cfg.Type, cfg.Ifaces)
	if err := rp.Respond(ctx); err != nil && ctx.Err() == nil {
		log.Printf("Bonjour stopped: %v", err)
	}
}
