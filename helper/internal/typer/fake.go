package typer

import (
	"errors"
	"sync"
)

var ErrFake = errors.New("fake failure")

// Fake records calls; used in tests. If Hold is set, Type blocks until it's closed,
// which lets server tests keep the typing lock busy; Entered, if set, gets a value as each
// Type starts.
type Fake struct {
	mu       sync.Mutex
	log      []string
	FailOn   string
	NotReady bool
	Hold     chan struct{}
	Entered  chan struct{}
}

// Calls returns a copy of the calls made so far.
func (f *Fake) Calls() []string {
	f.mu.Lock()
	defer f.mu.Unlock()
	return append([]string(nil), f.log...)
}

func (f *Fake) rec(s string) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	if f.FailOn != "" && s == f.FailOn {
		return ErrFake
	}
	f.log = append(f.log, s)
	return nil
}

func (f *Fake) Type(t string) error {
	if f.Entered != nil {
		f.Entered <- struct{}{}
	}
	if f.Hold != nil {
		<-f.Hold
	}
	return f.rec("type:" + t)
}

func (f *Fake) Paste(t string) error {
	return f.rec("paste:" + t)
}

func (f *Fake) ShiftEnter() error { return f.rec("shift-enter") }
func (f *Fake) Enter() error      { return f.rec("enter") }
func (f *Fake) Ready() bool       { return !f.NotReady }
