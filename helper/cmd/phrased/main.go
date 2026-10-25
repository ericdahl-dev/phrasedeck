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
	"net/http"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
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
	status, err := tailscaleStatus(o.tailscale)
	var names []string
	var tsName string
	if err == nil {
		if names, err = tailnet.ParseSelf(status); err == nil {
			tsName, _ = tailnet.PairHost(status)
		}
	}
	if err != nil {
		if len(prefixes) == 0 {
			// Without the allowlist every request would get 403, so don't start.
			return fmt.Errorf("reading this machine's tailnet names (use -tailscale to point at the CLI): %w", err)
		}
		log.Printf("Tailscale isn't available (%v); serving the LAN route only until it is", err)
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
	var tlsCert tls.Certificate
	if lanLn != nil {
		lanAddrs = lan.LocalAddrs(prefixes)
		if len(lanAddrs) == 0 {
			log.Printf("warning: this machine has no address in %v, so no LAN device can reach it", prefixes)
		}
		allow = append(allow, lanHosts(hostname, lanAddrs)...)
		if tlsCert, cfg.CertSHA256, err = lan.LoadOrCreateCert(o.config); err != nil {
			return fmt.Errorf("LAN certificate: %w", err)
		}
		cfg.LANPort = o.lanPort
		if err := os.WriteFile(filepath.Join(o.config, lanAddrFile), []byte(pairAddr(lanAddrs)+"\n"), 0o600); err != nil {
			log.Printf("can't record the LAN address for phrased pair: %v", err)
		}
	} else if err := os.Remove(filepath.Join(o.config, lanAddrFile)); err != nil && !errors.Is(err, os.ErrNotExist) {
		// A stale file would put a LAN route the helper no longer serves into the QR code.
		log.Printf("can't remove %s: %v", lanAddrFile, err)
	}
	cfg.AllowHosts = allow
	// One handler for both listeners, so rate limits and the typing lock are shared.
	handler := server.New(cfg)

	addr := fmt.Sprintf(":%d", o.port)
	ln, err := tailnet.Listen(addr)
	if err != nil {
		return err
	}
	// So `phrased pair` puts the port in use into its QR code.
	if err := os.WriteFile(filepath.Join(o.config, "port"), []byte(strconv.Itoa(o.port)+"\n"), 0o600); err != nil {
		log.Printf("can't record the port for phrased pair: %v", err)
	}
	servers := []*http.Server{newHTTPServer(handler)}
	log.Printf("listening on %s (tailnet peers only)", addr)
	if lanLn != nil {
		lanSrv := newHTTPServer(handler)
		lanSrv.TLSConfig = &tls.Config{Certificates: []tls.Certificate{tlsCert}, MinVersion: tls.VersionTLS12}
		servers = append(servers, lanSrv)
		log.Printf("listening on :%d over HTTPS (tailnet peers and %v); certificate SHA-256 %s", o.lanPort, prefixes, cfg.CertSHA256)
	}
	log.Printf("allowed hosts: %s", strings.Join(allow, ", "))
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
	if lanLn != nil {
		go func() { errc <- servers[1].ServeTLS(lanLn, "", "") }()
		_ = tsName // advertised over Bonjour in the next commit
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
		return nil
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
