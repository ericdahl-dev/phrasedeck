#!/usr/bin/env python3
"""Writes the CSV for the heap check in #173: 48 Buttons, each with a 2000-byte Phrase.

Usage: python3 firmware/tools/make_heap_csv.py heap-48x2000.csv

Import it on the panel's web page with Replace all, and compare the `heap` serial command
(crowpanel-dev build) before and after.
"""
import sys


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit("usage: make_heap_csv.py OUT.csv")
    with open(sys.argv[1], "w", newline="", encoding="utf-8") as f:
        f.write("label,phrase,enter\r\n")
        for i in range(48):
            f.write(f"Heap {i + 1:02d},{chr(ord('a') + i % 26) * 2000},no\r\n")


if __name__ == "__main__":
    main()
