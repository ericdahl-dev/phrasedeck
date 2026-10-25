// Command phrased types phrases sent over the tailnet into the focused window.
//
//	phrased [flags]          run the helper
//	phrased pair             open a 2-minute pairing window and print its code
//	phrased devices          list paired devices
//	phrased revoke <name>    revoke a device
package main

import (
	"context"
	"crypto/tls"
	"errors"
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"net/netip"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync/atomic"
	"syscall"
	"time"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/auth"
	"github.com/ericdahl-dev/phrasedeck/helper/internal/lan"
	"github.com/ericdahl-dev/phrasedeck/helper/internal/server"
	"github.com/ericdahl-dev/phrasedeck/helper/internal/tailnet"
	"github.com/ericdahl-dev/phrasedeck/helper/internal/typer"
)

// macTailscale is where the Mac app keeps its CLI when `tailscale` isn't on PATH.
const macTailscale = "/Applications/Tailscale.app/Contents/MacOS/Tailscale"

const defaultPort = 47811

type list []string

func (l *list) String() string     { return strings.Join(*l, ",") }
func (l *list) Set(v string) error { *l = append(*l, v); return nil }

type options struct {
	port       int
	config     string
	tailscale  string
	allowHosts list
	lan        list
	lanPort    int
}

func parseFlags(args []string, home string) (options, error) {
	var o options
	fs := flag.NewFlagSet("phrased", flag.ContinueOnError)
	fs.IntVar(&o.port, "port", defaultPort, "port to listen on")
	fs.StringVar(&o.config, "config", filepath.Join(home, ".config", "phrased"), "config directory (holds tokens.json)")
	fs.StringVar(&o.tailscale, "tailscale", defaultTailscale(), "path to the tailscale CLI")
	fs.Var(&o.allowHosts, "allow-host", "extra Host name to accept (repeatable)")
	fs.Var(&o.lan, "lan", "home subnet to also serve over HTTPS, like 192.168.1.0/24 (repeatable; also <config>/lan-subnets)")
	fs.IntVar(&o.lanPort, "lan-port", lan.DefaultPort, "HTTPS port for -lan subnets")
	if err := fs.Parse(args); err != nil {
		return o, err
	}
	if fs.NArg() > 0 {
		return o, fmt.Errorf("unknown argument %q", fs.Arg(0))
	}
	return o, nil
}

func defaultTailscale() string {
	if p, err := exec.LookPath("tailscale"); err == nil {
		return p
	}
	if runtime.GOOS == "darwin" {
		return macTailscale
	}
	return "tailscale"
}

func main() {
	home, err := os.UserHomeDir()
	if err != nil {
		log.Fatal(err)
	}
	if handled, err := command(os.Args[1:], home, os.Stdout); handled {
		if err != nil && !errors.Is(err, flag.ErrHelp) {
			log.Fatal(err)
		}
		return
	}
	o, err := parseFlags(os.Args[1:], home)
	if errors.Is(err, flag.ErrHelp) {
		return
	}
	if err != nil {
		log.Fatal(err)
	}
	if err := run(o); err != nil {
		log.Fatal(err)
	}
}

func run(o options) error {
	store, err := auth.NewStore(filepath.Join(o.config, "tokens.json"))
	if err != nil {
		return err
	}
	prefixes, err := subnets(o)
	if err != nil {
		return err
	}
	names, tsName, waitTailnet, err := tailnetAtStartup(o.tailscale, prefixes)
	if err != nil {
		return err
	}
	allow := append(names, o.allowHosts...)
	hostname, _ := os.Hostname()
	backend := typer.New()

	cfg := server.Config{
		Store:    store,
		Pairer:   auth.NewPairer(filepath.Join(o.config, "pairing.json"), store),
		Typer:    backend,
		Hostname: hostname,
		OS:       runtime.GOOS,
	}
	lanLn, err := listenLAN(o, prefixes)
	if err != nil {
		return err
	}
	var lanAddrs []lan.Addr
	var lanAllow []string
	var tlsCert tls.Certificate
	if lanLn != nil {
		lanAddrs = lan.LocalAddrs(prefixes)
		if len(lanAddrs) == 0 {
			log.Printf("warning: this machine has no address in %v, so no LAN device can reach it", prefixes)
		}
		lanAllow = append(lanHosts(hostname, lanAddrs), o.allowHosts...)
		if tlsCert, cfg.CertSHA256, err = lan.LoadOrCreateCert(o.config); err != nil {
			return fmt.Errorf("LAN certificate: %w", err)
		}
		cfg.LANPort = o.lanPort
		if ip := pairAddr(lanAddrs); ip == "" {
			os.Remove(filepath.Join(o.config, lanAddrFile))
		} else if err := os.WriteFile(filepath.Join(o.config, lanAddrFile),
			[]byte(net.JoinHostPort(ip, strconv.Itoa(o.lanPort))+"\n"), 0o600); err != nil {
			log.Printf("can't record the LAN address for phrased pair: %v", err)
		}
	} else if err := os.Remove(filepath.Join(o.config, lanAddrFile)); err != nil && !errors.Is(err, os.ErrNotExist) {
		// A stale file would put a LAN route the helper no longer serves into the QR code.
		log.Printf("can't remove %s: %v", lanAddrFile, err)
	}
	// Both listeners share one Helper, so rate limits, lockout and the typing lock are one set;
	// each accepts only its own Host names.
	helper := server.NewHelper(cfg)

	addr := fmt.Sprintf(":%d", o.port)
	ln, err := tailnet.Listen(addr)
	if err != nil {
		return err
	}
	// So `phrased pair` puts the port in use into its QR code.
	if err := os.WriteFile(filepath.Join(o.config, "port"), []byte(strconv.Itoa(o.port)+"\n"), 0o600); err != nil {
		log.Printf("can't record the port for phrased pair: %v", err)
	}
	// The tailnet allowlist is swapped in when Tailscale comes up after the helper started.
	var tailHandler atomic.Pointer[http.Handler]
	setTailnet := func(allow []string) {
		h := helper.Handler(allow)
		tailHandler.Store(&h)
	}
	setTailnet(allow)
	servers := []*http.Server{newHTTPServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		(*tailHandler.Load()).ServeHTTP(w, r)
	}))}
	log.Printf("listening on %s (tailnet peers only)", addr)
	if lanLn != nil {
		lanSrv := newHTTPServer(helper.LANHandler(lanAllow))
		lanSrv.TLSConfig = &tls.Config{Certificates: []tls.Certificate{tlsCert}, MinVersion: tls.VersionTLS12}
		servers = append(servers, lanSrv)
		log.Printf("listening on :%d over HTTPS (tailnet peers and %v); certificate SHA-256 %s", o.lanPort, prefixes, cfg.CertSHA256)
	}
	log.Printf("allowed hosts: %s", strings.Join(allow, ", "))
	if lanLn != nil {
		log.Printf("allowed hosts on the LAN listener: %s", strings.Join(lanAllow, ", "))
	}
	if runtime.GOOS == "darwin" {
		if warn := checkFirewall(); warn != "" {
			log.Printf("warning: %s", warn)
		}
	}
	if !backend.Ready() {
		log.Printf("can't type yet: grant Accessibility to this binary (System Settings > Privacy & Security > Accessibility)")
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	errc := make(chan error, 2)
	go func() { errc <- servers[0].Serve(ln) }()
	tsUp := make(chan string, 1)
	if waitTailnet {
		go waitForTailnet(ctx, o.tailscale, 30*time.Second, func(names []string, tsName string) {
			allow := append(names, o.allowHosts...)
			setTailnet(allow)
			log.Printf("Tailscale is up; allowed hosts: %s", strings.Join(allow, ", "))
			if tsName != "" {
				tsUp <- tsName
			}
		})
	}
	bonjourDone := make(chan struct{})
	if lanLn != nil {
		go func() { errc <- servers[1].ServeTLS(lanLn, "", "") }()
		go func() {
			advertise(ctx, bonjourConfig(hostname, tsName, cfg.CertSHA256, o.lanPort, lanAddrs), tsUp)
			close(bonjourDone)
		}()
	} else {
		close(bonjourDone)
	}
	select {
	case err := <-errc:
		return err
	case <-ctx.Done():
		log.Printf("shutting down")
		shut, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		for _, srv := range servers {
			srv.Shutdown(shut)
		}
		// Let Bonjour send its goodbye, so browsers drop the service now rather than at TTL.
		select {
		case <-bonjourDone:
		case <-shut.Done():
		}
		return nil
	}
}

// tailnetSelf reads this machine's tailnet names (for the Host allowlist) and its pairing host.
func tailnetSelf(tailscale string) (names []string, tsName string, err error) {
	status, err := tailscaleStatus(tailscale)
	if err != nil {
		return nil, "", err
	}
	if names, err = tailnet.ParseSelf(status); err != nil {
		return nil, "", err
	}
	tsName, _ = tailnet.PairHost(status)
	return names, tsName, nil
}

// tailnetAtStartup reads the tailnet names. Without Tailscale the helper still starts when it
// has a LAN route, and wait says to keep trying; without one it refuses, since every request
// would get 403.
func tailnetAtStartup(tailscale string, prefixes []netip.Prefix) (names []string, tsName string, wait bool, err error) {
	names, tsName, err = tailnetSelf(tailscale)
	if err == nil {
		return names, tsName, false, nil
	}
	if len(prefixes) == 0 {
		return nil, "", false, fmt.Errorf("reading this machine's tailnet names (use -tailscale to point at the CLI): %w", err)
	}
	log.Printf("Tailscale isn't available (%v); serving the LAN route only until it is", err)
	return nil, "", true, nil
}

// waitForTailnet retries every interval until Tailscale answers, then hands found the names.
func waitForTailnet(ctx context.Context, tailscale string, every time.Duration, found func(names []string, tsName string)) {
	t := time.NewTicker(every)
	defer t.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-t.C:
			if names, tsName, err := tailnetSelf(tailscale); err == nil {
				found(names, tsName)
				return
			}
		}
	}
}

func newHTTPServer(h http.Handler) *http.Server {
	return &http.Server{
		Handler:           h,
		ReadHeaderTimeout: 5 * time.Second,
		ReadTimeout:       10 * time.Second,
		WriteTimeout:      30 * time.Second,
		IdleTimeout:       60 * time.Second,
	}
}
