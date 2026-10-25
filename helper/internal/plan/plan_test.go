package plan

import (
	"reflect"
	"strings"
	"testing"
)

func check(t *testing.T, got, want []Step) {
	t.Helper()
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %#v want %#v", got, want)
	}
}

func TestTypeSimpleWithEnter(t *testing.T) {
	check(t, Build("hello", true, ModeAuto), []Step{{Kind: TypeText, Text: "hello"}, {Kind: Enter}})
}

func TestNoEnter(t *testing.T) {
	check(t, Build("merge ", false, ModeAuto), []Step{{Kind: TypeText, Text: "merge "}})
}

func TestAutoPastesAnyNewline(t *testing.T) {
	check(t, Build("a\nb", true, ModeAuto), []Step{{Kind: PasteText, Text: "a\nb"}, {Kind: Enter}})
}

func TestAutoNormalizesCRLFBeforePaste(t *testing.T) {
	check(t, Build("a\r\nb", false, ModeAuto), []Step{{Kind: PasteText, Text: "a\nb"}})
}

func TestAutoPastesLongText(t *testing.T) {
	long := strings.Repeat("x", PasteThreshold+1)
	check(t, Build(long, true, ModeAuto), []Step{{Kind: PasteText, Text: long}, {Kind: Enter}})
}

func TestAutoTypesAtThreshold(t *testing.T) {
	got := Build(strings.Repeat("x", PasteThreshold), false, ModeAuto)
	if got[0].Kind != TypeText {
		t.Fatalf("pasted at threshold: %#v", got[0])
	}
}

func TestAutoCountsRunesNotBytes(t *testing.T) {
	got := Build(strings.Repeat("é", PasteThreshold), false, ModeAuto)
	if got[0].Kind != TypeText {
		t.Fatalf("pasted %d runes of 2-byte text: %#v", PasteThreshold, got[0])
	}
}

func TestTypeModeNewlinesBecomeShiftEnter(t *testing.T) {
	check(t, Build("a\nb", true, ModeType), []Step{
		{Kind: TypeText, Text: "a"}, {Kind: ShiftEnter},
		{Kind: TypeText, Text: "b"}, {Kind: Enter},
	})
}

func TestTypeModeCRLF(t *testing.T) {
	check(t, Build("a\r\nb", false, ModeType), []Step{
		{Kind: TypeText, Text: "a"}, {Kind: ShiftEnter}, {Kind: TypeText, Text: "b"},
	})
}

func TestTypeModeNeverPastes(t *testing.T) {
	for _, s := range Build(strings.Repeat("x", 1000), false, ModeType) {
		if s.Kind == PasteText {
			t.Fatal("type mode pasted")
		}
	}
}

// Splitting text for the keyboard is each backend's job, so a line stays one step.
func TestALineIsOneStep(t *testing.T) {
	line := strings.Repeat("é", 299)
	check(t, Build(line, false, ModeType), []Step{{Kind: TypeText, Text: line}})
}

func TestForcedPasteKeepsNewlines(t *testing.T) {
	check(t, Build("a\nb", false, ModePaste), []Step{{Kind: PasteText, Text: "a\nb"}})
}

func TestEmptyTextOnlyEnter(t *testing.T) {
	check(t, Build("", true, ModeAuto), []Step{{Kind: Enter}})
}
