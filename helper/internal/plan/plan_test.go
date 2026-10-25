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

func TestChunksAt20Runes(t *testing.T) {
	got := Build(strings.Repeat("é", 45), false, ModeType)
	if len(got) != 3 || len([]rune(got[0].Text)) != 20 || len([]rune(got[2].Text)) != 5 {
		t.Fatalf("bad chunks: %#v", got)
	}
}

func TestForcedPasteKeepsNewlines(t *testing.T) {
	check(t, Build("a\nb", false, ModePaste), []Step{{Kind: PasteText, Text: "a\nb"}})
}

func TestEmptyTextOnlyEnter(t *testing.T) {
	check(t, Build("", true, ModeAuto), []Step{{Kind: Enter}})
}

func TestNoModeMeansAuto(t *testing.T) {
	check(t, Build("a\nb", false, ""), Build("a\nb", false, ModeAuto))
	for _, m := range []Mode{"", ModeAuto, ModeType, ModePaste} {
		if !m.Valid() {
			t.Errorf("%q not valid", m)
		}
	}
	if Mode("shout").Valid() {
		t.Error(`"shout" valid`)
	}
}
