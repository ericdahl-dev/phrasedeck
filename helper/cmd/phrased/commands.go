package main

import (
	"errors"
	"flag"
	"fmt"
	"io"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"time"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/auth"
	"github.com/ericdahl-dev/phrasedeck/helper/internal/tailnet"
	"rsc.io/qr"
)

// tailscaleStatus returns `tailscale status --json`. Tests replace it.
var tailscaleStatus = runTailscaleStatus

func runTailscaleStatus(path string) ([]byte, error) {
	return exec.Command(path, "status", "--json").Output()
}

// pairURL is the QR payload the app scans: phrasedeck://pair?host=&port=&code=&v=1.
func pairURL(host string, port int, code string) string {
	q := url.Values{"host": {host}, "port": {strconv.Itoa(port)}, "code": {code}, "v": {"1"}}
	return "phrasedeck://pair?" + q.Encode()
}

// runningPort is the port the helper wrote to <config>/port when it started, or the default.
func runningPort(config string) int {
	b, err := os.ReadFile(filepath.Join(config, "port"))
	if err != nil {
		return defaultPort
	}
	if p, err := strconv.Atoi(strings.TrimSpace(string(b))); err == nil && p > 0 && p < 65536 {
		return p
	}
	return defaultPort
}

// writeQR prints text as a QR code using half-block characters, two modules per line, with a
// quiet zone. Dark modules print as spaces on a light background so it scans on dark terminals.
func writeQR(out io.Writer, text string) error {
	c, err := qr.Encode(text, qr.M)
	if err != nil {
		return err
	}
	const quiet = 2
	dark := func(x, y int) bool {
		x, y = x-quiet, y-quiet
		return x >= 0 && y >= 0 && x < c.Size && y < c.Size && c.Black(x, y)
	}
	var b strings.Builder
	for y := 0; y < c.Size+2*quiet; y += 2 {
		for x := 0; x < c.Size+2*quiet; x++ {
			switch top, bottom := dark(x, y), dark(x, y+1); {
			case top && bottom:
				b.WriteString(" ")
			case top:
				b.WriteString("▄")
			case bottom:
				b.WriteString("▀")
			default:
				b.WriteString("█")
			}
		}
		b.WriteString("\n")
	}
	_, err = io.WriteString(out, b.String())
	return err
}

// notify shows the pairing code as a notification. Tests replace it.
var notify = macNotify

// macNotify shows a macOS notification. The code is passed as an argument to a fixed script,
// never spliced into it.
func macNotify(code string) {
	if runtime.GOOS != "darwin" {
		return
	}
	exec.Command("osascript",
		"-e", "on run argv",
		"-e", `display notification ("Pairing code " & item 1 of argv) with title "phrased"`,
		"-e", "end run",
		code).Run()
}

// command runs the pair, devices or revoke subcommand. It reports false when args aren't one,
// so main runs the server instead.
func command(args []string, home string, out io.Writer) (bool, error) {
	if len(args) == 0 {
		return false, nil
	}
	name := args[0]
	if name != "pair" && name != "devices" && name != "revoke" {
		return false, nil
	}
	fs := flag.NewFlagSet("phrased "+name, flag.ContinueOnError)
	fs.SetOutput(out)
	config := fs.String("config", filepath.Join(home, ".config", "phrased"), "config directory")
	if err := fs.Parse(args[1:]); err != nil {
		return true, err
	}
	tokens := filepath.Join(*config, "tokens.json")

	switch name {
	case "pair":
		if fs.NArg() != 0 {
			return true, errors.New("usage: phrased pair [-config dir]")
		}
		code, err := auth.OpenWindow(filepath.Join(*config, "pairing.json"), time.Now())
		if err != nil {
			return true, err
		}
		fmt.Fprintf(out, "Pairing code: %s\nEnter it in phrasedeck within %v (until %s).\n",
			code, auth.PairWindow, time.Now().Add(auth.PairWindow).Format("15:04:05"))
		notify(code)
		status, err := tailscaleStatus(defaultTailscale())
		host, hostErr := "", err
		if err == nil {
			host, hostErr = tailnet.PairHost(status)
		}
		if hostErr != nil {
			fmt.Fprintf(out, "No QR code: can't read this machine's tailnet name (%v). Type the name and code in the app.\n", hostErr)
			return true, nil
		}
		link := pairURL(host, runningPort(*config), code)
		fmt.Fprintln(out, "\nOr scan this in phrasedeck (Add a computer > Scan QR code):")
		if err := writeQR(out, link); err != nil {
			return true, err
		}
		fmt.Fprintln(out, link)
	case "devices":
		store, err := auth.NewStore(tokens)
		if err != nil {
			return true, err
		}
		devices := store.Devices()
		if len(devices) == 0 {
			fmt.Fprintln(out, "no paired devices")
		}
		for _, d := range devices {
			fmt.Fprintf(out, "%s\tpaired %s\n", d.Name, d.PairedAt)
		}
	case "revoke":
		if fs.NArg() != 1 {
			return true, errors.New("usage: phrased revoke [-config dir] <name>")
		}
		store, err := auth.NewStore(tokens)
		if err != nil {
			return true, err
		}
		if err := store.Remove(fs.Arg(0)); err != nil {
			return true, err
		}
		fmt.Fprintf(out, "revoked %q\n", fs.Arg(0))
	}
	return true, nil
}
