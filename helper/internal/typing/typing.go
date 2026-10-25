// Package typing types one phrase at a time: it checks the phrase and the backend, waits
// briefly for a phrase already typing, then plans and runs the keystrokes.
package typing

import (
	"errors"
	"time"
	"unicode/utf8"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/plan"
	"github.com/ericdahl-dev/phrasedeck/helper/internal/typer"
)

// MaxText caps the text of one phrase, in runes.
const MaxText = 10000

// Outcome is what became of a phrase.
type Outcome int

const (
	Typed Outcome = iota
	BadMode
	TooLong
	NotReady // the backend can't type, e.g. macOS Accessibility not granted
	Busy     // another phrase held the keyboard for the whole wait
	Failed   // the backend failed partway; the error says how

	// NumOutcomes counts the outcomes above; keep it last.
	NumOutcomes
)

// Typist serializes phrases onto one keyboard.
type Typist struct {
	backend typer.Backend
	wait    time.Duration
	lock    chan struct{}
}

// New returns a Typist that waits up to lockWait for a phrase already typing.
func New(backend typer.Backend, lockWait time.Duration) *Typist {
	return &Typist{backend: backend, wait: lockWait, lock: make(chan struct{}, 1)}
}

// Ready reports whether the backend can type.
func (t *Typist) Ready() bool { return t.backend.Problem() == "" }

// Type types text in mode ("" is auto), then Enter if enter is set. The error is set only
// for Failed.
func (t *Typist) Type(text string, enter bool, mode plan.Mode) (Outcome, error) {
	if !mode.Valid() {
		return BadMode, nil
	}
	if utf8.RuneCountInString(text) > MaxText {
		return TooLong, nil
	}
	if problem := t.backend.Problem(); problem != "" {
		return NotReady, errors.New(problem)
	}
	timer := time.NewTimer(t.wait)
	defer timer.Stop()
	select {
	case t.lock <- struct{}{}:
		defer func() { <-t.lock }()
	case <-timer.C:
		return Busy, nil
	}
	if err := typer.Run(t.backend, plan.Build(text, enter, mode)); err != nil {
		return Failed, err
	}
	return Typed, nil
}
