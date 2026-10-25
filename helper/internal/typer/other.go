//go:build !darwin && !linux

package typer

import "errors"

// ErrNoBackend is returned on an OS without a typing backend yet (macOS and Linux have one).
var ErrNoBackend = errors.New("no typing backend for this OS yet")

type none struct{}

// New returns this OS's backend.
func New() Backend { return none{} }

func (none) Type(string) error  { return ErrNoBackend }
func (none) Paste(string) error { return ErrNoBackend }
func (none) ShiftEnter() error  { return ErrNoBackend }
func (none) Enter() error       { return ErrNoBackend }
func (none) Problem() string    { return "this OS has no typing backend" }
