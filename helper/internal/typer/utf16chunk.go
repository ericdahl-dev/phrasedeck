package typer

import "unicode/utf16"

// utf16Chunks splits s into chunks of at most max UTF-16 units, never splitting a surrogate
// pair. macOS keyboard events carry at most 20 units each.
func utf16Chunks(s string, max int) [][]uint16 {
	var out [][]uint16
	var cur []uint16
	for _, r := range s {
		units := utf16.Encode([]rune{r})
		if len(cur)+len(units) > max {
			out = append(out, cur)
			cur = nil
		}
		cur = append(cur, units...)
	}
	if len(cur) > 0 {
		out = append(out, cur)
	}
	return out
}
