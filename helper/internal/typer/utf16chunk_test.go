package typer

import (
	"strings"
	"testing"
	"unicode/utf16"
)

func lens(chunks [][]uint16) []int {
	var n []int
	for _, c := range chunks {
		n = append(n, len(c))
	}
	return n
}

func join(chunks [][]uint16) string {
	var all []uint16
	for _, c := range chunks {
		all = append(all, c...)
	}
	return string(utf16.Decode(all))
}

func TestUTF16ChunksASCII(t *testing.T) {
	got := utf16Chunks(strings.Repeat("a", 45), 20)
	if l := lens(got); len(l) != 3 || l[0] != 20 || l[1] != 20 || l[2] != 5 {
		t.Fatalf("lengths %v", l)
	}
}

func TestUTF16ChunksAccented(t *testing.T) {
	got := utf16Chunks(strings.Repeat("é", 45), 20)
	if l := lens(got); len(l) != 3 || l[0] != 20 || l[2] != 5 {
		t.Fatalf("lengths %v", l)
	}
}

func TestUTF16ChunksEmojiAtBoundaryStaysWhole(t *testing.T) {
	got := utf16Chunks(strings.Repeat("a", 19)+"😀b", 20)
	if l := lens(got); len(l) != 2 || l[0] != 19 || l[1] != 3 {
		t.Fatalf("lengths %v", l)
	}
	if string(utf16.Decode(got[1])) != "😀b" {
		t.Fatalf("second chunk %q", string(utf16.Decode(got[1])))
	}
}

func TestUTF16ChunksRoundTripAndLimit(t *testing.T) {
	for _, s := range []string{"", "naïve café ✓", strings.Repeat("😀", 33), "a😀" + strings.Repeat("🇺🇸x", 12), strings.Repeat("ab", 50)} {
		got := utf16Chunks(s, 20)
		for _, c := range got {
			if len(c) == 0 || len(c) > 20 {
				t.Fatalf("%q: chunk of %d units", s, len(c))
			}
			if utf16.IsSurrogate(rune(c[len(c)-1])) && c[len(c)-1] < 0xdc00 {
				t.Fatalf("%q: chunk ends in a high surrogate", s)
			}
		}
		if join(got) != s {
			t.Fatalf("round trip: %q != %q", join(got), s)
		}
	}
}
