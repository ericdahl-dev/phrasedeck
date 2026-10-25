package main

import (
	"context"
	"log"
	"maps"
	"strconv"

	"github.com/brutella/dnssd"
	dnssdlog "github.com/brutella/dnssd/log"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/lan"
)

// bonjourConfig describes the _phrasedeck._tcp service: on the LAN interfaces only, under
// phrased-<host>.local. TXT names the tailnet host (ts) and the certificate fingerprint (fp)
// so the app can match a computer it already paired, by either. TXT is public and carries no
// token; fp is for matching only, since the app trusts only the pin it stored at pairing.
func bonjourConfig(hostname, tsName, fp string, port int, addrs []lan.Addr) dnssd.Config {
	var ifaces []string
	seen := map[string]bool{}
	for _, a := range addrs {
		if !seen[a.Iface] {
			seen[a.Iface] = true
			ifaces = append(ifaces, a.Iface)
		}
	}
	text := map[string]string{"v": "1", "port": strconv.Itoa(port), "fp": fp}
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
// A tailnet name sent on tsUp (Tailscale came up after start) is added to TXT live.
func advertise(ctx context.Context, cfg dnssd.Config, tsUp <-chan string) {
	if len(cfg.Ifaces) == 0 {
		return
	}
	dnssdlog.Info.SetOutput(log.Writer())
	rp, err := dnssd.NewResponder()
	var h dnssd.ServiceHandle
	if err == nil {
		var svc dnssd.Service
		if svc, err = dnssd.NewService(cfg); err == nil {
			h, err = rp.Add(svc)
		}
	}
	if err != nil {
		log.Printf("Bonjour: %v", err)
		return
	}
	log.Printf("advertising %s on %v", cfg.Type, cfg.Ifaces)
	done := make(chan error, 1)
	go func() { done <- rp.Respond(ctx) }()
	for {
		select {
		case err := <-done:
			if err != nil && ctx.Err() == nil {
				log.Printf("Bonjour stopped: %v", err)
			}
			return
		case ts := <-tsUp:
			if h, err = retag(rp, h, cfg, ts); err != nil {
				log.Printf("Bonjour: adding ts=%s: %v", ts, err)
			} else {
				log.Printf("Bonjour: TXT now has ts=%s", ts)
			}
		}
	}
}

type registrar interface {
	Add(dnssd.Service) (dnssd.ServiceHandle, error)
	Remove(dnssd.ServiceHandle)
}

// retag re-registers the service with ts in its TXT. Not dnssd's UpdateText, which writes the
// record without holding the responder's lock.
func retag(rp registrar, old dnssd.ServiceHandle, cfg dnssd.Config, ts string) (dnssd.ServiceHandle, error) {
	cfg.Text = maps.Clone(cfg.Text)
	cfg.Text["ts"] = ts
	svc, err := dnssd.NewService(cfg)
	if err != nil {
		return old, err
	}
	rp.Remove(old)
	return rp.Add(svc)
}
