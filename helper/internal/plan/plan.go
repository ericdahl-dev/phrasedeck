// Package plan turns a phrase into the keyboard steps a typer runs.
package plan

import (
	"strings"
	"unicode/utf8"
)

type Kind int

const (
	TypeText Kind = iota
	PasteText
	ShiftEnter
	Enter
)

type Mode string

const (
	ModeAuto  Mode = "auto"
	ModeType  Mode = "type"
	ModePaste Mode = "paste"
)

// Valid reports whether m is a mode Build understands; "" means auto.
func (m Mode) Valid() bool {
	switch m {
	case "", ModeAuto, ModeType, ModePaste:
		return true
	}
	return false
}

// PasteThreshold is the length (in runes) above which auto mode pastes instead of typing.
const PasteThreshold = 300

// ChunkSize is how many runes go into one typing step. Backends may split further.
const ChunkSize = 20

type Step struct {
	Kind Kind
	Text string
}

// Build plans text in the given mode ("" is auto). The final Enter comes only from enter.
func Build(text string, enter bool, mode Mode) []Step {
	if mode == "" {
		mode = ModeAuto
	}
	text = strings.ReplaceAll(text, "\r\n", "\n")
	var steps []Step
	paste := mode == ModePaste || (mode == ModeAuto &&
		(strings.Contains(text, "\n") || utf8.RuneCountInString(text) > PasteThreshold))
	if text != "" {
		if paste {
			steps = append(steps, Step{Kind: PasteText, Text: text})
		} else {
			for i, line := range strings.Split(text, "\n") {
				if i > 0 {
					steps = append(steps, Step{Kind: ShiftEnter})
				}
				steps = append(steps, chunk(line)...)
			}
		}
	}
	if enter {
		steps = append(steps, Step{Kind: Enter})
	}
	return steps
}

func chunk(s string) []Step {
	var out []Step
	r := []rune(s)
	for len(r) > 0 {
		n := min(ChunkSize, len(r))
		out = append(out, Step{Kind: TypeText, Text: string(r[:n])})
		r = r[n:]
	}
	return out
}
