package main

import (
	"errors"
	"flag"
	"fmt"
	"io"
	"os/exec"
	"path/filepath"
	"runtime"
	"time"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/auth"
)

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
