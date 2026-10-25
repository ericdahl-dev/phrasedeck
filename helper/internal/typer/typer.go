// Package typer sends keystrokes to the focused window.
package typer

import "github.com/ericdahl-dev/phrasedeck/helper/internal/plan"

// Backend is one OS's way of typing.
type Backend interface {
	// Type types one line, splitting it however this OS's keyboard API needs.
	Type(text string) error
	Paste(text string) error
	ShiftEnter() error
	Enter() error
	// Ready reports whether the backend can type (e.g. macOS Accessibility granted).
	Ready() bool
}

// Run sends steps to b in order and stops at the first error.
func Run(b Backend, steps []plan.Step) error {
	for _, s := range steps {
		var err error
		switch s.Kind {
		case plan.TypeText:
			err = b.Type(s.Text)
		case plan.PasteText:
			err = b.Paste(s.Text)
		case plan.ShiftEnter:
			err = b.ShiftEnter()
		case plan.Enter:
			err = b.Enter()
		}
		if err != nil {
			return err
		}
	}
	return nil
}
