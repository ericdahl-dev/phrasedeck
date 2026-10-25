//go:build linux

package typer

// New returns this OS's backend.
func New() Backend { return newWayland() }
