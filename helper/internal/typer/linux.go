//go:build linux

package typer

import "os"

// New returns this OS's backend.
func New() Backend { return pick(os.Getenv) }
