//go:build !darwin

package typer

import "errors"

// ErrNoBackend is returned on an OS without a typing backend yet (Linux arrives in #5).
var ErrNoBackend = errors.New("no typing backend for this OS yet")

type none struct{}

// New returns this OS's backend.
func New() Backend { return none{} }

func (none) Type(string) error  { return ErrNoBackend }
func (none) Paste(string) error { return ErrNoBackend }
func (none) ShiftEnter() error  { return ErrNoBackend }
func (none) Enter() error       { return ErrNoBackend }
func (none) Ready() bool        { return false }
