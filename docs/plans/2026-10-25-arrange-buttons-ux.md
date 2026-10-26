# Arrange Buttons like the iPhone Home Screen: UX spec (#81)

Status: draft for Eric's review. Terms follow `CONTEXT.md` (Deck, Page, Button, Phrase, Square, Size).
"Width" means the layout width of a phone or iPad (defined below); it is not a Device, which in
`CONTEXT.md` is a paired sender.

The decisions in #81 are final and not reopened here: Home Screen-style entry, edge flip between
Pages, live reflow, gaps allowed, one arrangement per width.

## 1. What exists today (main, 2026-10-25)

- Each Page is a vertical `ScrollView`. `Packing.pack` places Buttons in `order`, first fit from the
  top left, and grows down. Rows are unbounded. A Size wider than the Page is narrowed for display;
  the stored Size is unchanged.
- The "+" tile is a trailing 1x1 after packing.
- Pinch changes the column count (how big a Square is), stored per horizontal size class
  (`deck.columns.compact` / `deck.columns.regular`), never synced. `PinchGuard` blocks taps
  during a pinch and for 0.5 s after it.
- Long-press menu on a Button: **Edit, Delete**. Delete has no confirmation. (The brief lists a
  Size item; there isn't one in `PhraseButton`. Size is set in the editor. If a Size item lands
  later, it stays in the menu next to Edit.)
- Swiping left and right changes Page (`TabView`, page style).

## 2. Widths and spots

- **Width** = the horizontal size class × orientation: `compact` and `compact-landscape` (iPhone,
  iPad Slide Over), `regular` and `regular-landscape` (iPad full screen and split, iPhone Max/Plus
  landscape). Landscape means the Deck's own size is wider than tall (from its GeometryReader, not
  the device), so Split View counts. Portrait and landscape keep **independent** arrangements
  (Eric, 2026-10-25). Rows saved before this say `compact` or `regular` and read as portrait; the
  key is a String, so there's no CloudKit schema change.
- A width with **no spots yet starts from the other orientation**: its Buttons come in that
  orientation's reading order and are packed to this width's column count. Its first edit saves its
  own spots, and from then on the two are independent.
- A **spot** is (column, row) of a Button's top-left Square, for one width.
- A Page **has an arrangement** at a width once any of its Buttons has a spot there. A Page with no
  arrangement is packed from `order`, exactly as today.
- **Spots are written only by**: a drop, an accessibility move, a Size change saved in the editor,
  and a new or moved Button arriving on a Page (it gets the end spot). Deleting a Button removes
  only its own spots. Showing the deck, pinching and rotating **never write spots**, so pinching
  back restores the old layout exactly.
- **First commit snapshots the Page.** The first time anything is written on a Page at a width, every
  Button on that Page gets the spot it is shown in at that moment. After that, `order` changes made
  on another width can't disturb it.
- **Arranging never changes `order`.** `order` is synced and drives every width that has no
  arrangement, so rewriting it would move the iPad when arranging on the iPhone. The one change
  that does reach other widths is moving a Button to another Page (`page` is Deck data). On the
  other widths that Button goes to the end of its new Page (rule 8).

### Showing a Page (resolution)

Given the Page's column count C and the spots for the current width:

1. Every Button's width is `min(Size columns, C)` (as today). Height is unchanged.
2. Take Buttons that have a spot, in reading order of their spots (row, then column; ties by `order`,
   then `uid`). Clamp the column to `0...C - width`. If the clamped footprint is free, place it there.
3. A Button that doesn't fit (overlap after clamping, or after a Size change or a sync merge) goes
   to the first free spot at or after its own spot, in reading order, adding rows as needed.
4. Buttons with no spot go after everything placed: the first free spot after the last occupied
   Square, in `order`. Gaps the user left are not filled.
5. In normal mode the "+" tile takes the first free Square after the last occupied Square. It has no
   spot and can't be dragged. It is hidden in arrange mode.

A Page never fills up: rows are unbounded and the Page scrolls, as today (see open question 1).

## 3. Gestures

| Gesture | Normal mode | Arrange mode |
|---|---|---|
| Tap Button | Send (with the confirm tap if set) | Nothing. Taps never type. |
| Long-press Button (about 0.5 s), lift | Menu: **Edit, Edit Deck, Delete** | Picks the Button up; lifting drops it in place |
| Long-press, then drag without lifting | Menu closes as the finger moves past about 10 pt; enters arrange mode carrying that Button | Drag (no hold needed, see below) |
| Touch and drag a Button | Scrolls / swipes as today | Drags immediately, no long-press needed |
| Tap "−" badge | n/a | Delete confirmation (section 9) |
| Swipe on empty space | Change Page | Change Page |
| Vertical drag on empty space | Scroll | Scroll |
| Pinch | Change Square size (as today) | Disabled |
| Done (toolbar, trailing) | n/a | Exit arrange mode |
| Tap empty space | Nothing | Exit arrange mode |

Conflicts and how they resolve:

- **Long-press vs menu.** The menu appears on the long-press as today. Moving the finger more than
  about 10 pt while the menu is up dismisses it and starts the drag; lifting without moving leaves
  the menu open. Implementation risk: if SwiftUI's `.contextMenu` can't hand off to a drag, the
  engineer may replace it with a custom long-press plus menu that behaves the same.
- **Long-press vs pinch.** A second finger landing before the menu appears cancels the long-press;
  the pinch wins. `PinchGuard` keeps blocking taps as today.
- **Pinch in arrange mode** is off: changing the column count mid-arrange would reflow everything
  under the finger.
- **Swipe vs drag in arrange mode.** A touch that starts on a Button drags it; one that starts on
  empty space swipes or scrolls.

## 4. States and transitions

```
Normal --long-press--> Menu --Edit Deck--> Arrange
Normal --long-press + drag--> Dragging (in Arrange)
Menu --Edit / Delete--> as today (Delete now confirms, open question 4)
Menu --tap outside--> Normal
Arrange --touch Button and move--> Dragging
Dragging --hold at left/right edge 0.6 s--> Page flips, still Dragging
Dragging --lift--> Arrange (spot saved)
Dragging --interrupted (call, app to background)--> Arrange, Button back at its origin, nothing saved
Arrange --tap "−"--> Confirm delete --Delete / Cancel--> Arrange
Arrange --Done / tap empty space / app to background--> Normal
```

- Each drop saves immediately; Done only leaves the mode. A crash loses at most the drag in flight.
- A sync import that arrives while dragging is applied after the drop. If it changed spots on this
  width, the per-Button result is last-write-wins, and resolution (section 2) settles any overlap.
- The Pages sheet and the editor can't be opened from arrange mode: the toolbar shows only Done,
  and the "Pages" button is hidden.

## 5. Dragging and live reflow

- **Target spot**: the Square nearest the dragged Button's top-left corner, column clamped to
  `0...C - width`, row 0 or more. Dragging below the last row targets new rows.
- **Dwell**: the target is accepted after the finger stays over it for 0.15 s, so a fast sweep
  doesn't shuffle the Page.
- **Reflow is computed from the layout at pickup, not from the last reflow.** Moving back to where you
  started puts every neighbor back.
- While dragging, the Button's origin spot is empty.
- Over empty Squares: nothing moves.
- Over occupied Squares: the dragged Button takes the target. Each Button it overlaps, in reading
  order of its pickup spot, shifts forward to the first free spot at or after that spot, adding rows
  if needed. Buttons that aren't overlapped never move. This can consume a gap after the target,
  which matches the Home Screen.
- Larger Sizes follow the same rule. A 3x3 over 1x1s displaces each overlapped 1x1; a 3x3 at 2
  columns is shown 2 wide and collides as 2 wide.
- What you see under your finger is what is saved on lift. Lifting saves the dragged Button's spot
  and every displaced Button's new spot (after the first-commit snapshot).
- Near the top or bottom of the screen (within 44 pt), the Page auto-scrolls.

## 6. Page edge flip

- Holding the dragged Button within 24 pt of the left or right screen edge for **0.6 s** flips to the
  previous or next Page. Holding on flips again every 0.8 s.
- Feedback: on entering the zone, that edge shows a soft highlight that fills over the 0.6 s; the
  flip uses the normal page slide and a light haptic.
- When the Button leaves its Page, that Page snaps back to its pickup layout with the origin spot
  empty (a gap, which is allowed). The new Page reflows under the finger by section 5, from its own
  layout at the moment of arrival.
- Dropping on another Page sets the Button's Page and its spot there. Other widths see it at the end
  of the new Page (rule 2.4 or 8).
- At the first or last Page, the edge shows no highlight and nothing happens (open question 3).

## 7. When the Square size or width changes

- **Pinch to fewer columns**: Buttons whose spot no longer fits are shown at the first free spot at or
  after it (resolution step 3). Nothing is saved, so pinching back restores the layout.
- **Pinch to more columns**: spots keep their columns; new columns on the right are empty.
- **Rotation or Split View** switches to that width's own arrangement (size class × orientation), or
  starts from the other orientation's order if it has none (section 2). Nothing is saved until an
  edit.
- **Size made bigger in the editor**: on Save, the edited Button keeps its spot and anything it now
  overlaps shifts forward (section 5 rule), saved for the current width. Other widths settle it by
  resolution when shown.
- **A full Page** can't happen while rows are unbounded.

## 8. Where a new Button goes

- To the Page chosen in the editor (the Page whose "+" was tapped, as today), with `order` at the end
  as today.
- On every width where that Page has an arrangement, it takes the first free spot after the last
  occupied Square. Gaps are left alone. On widths without one, packing puts it at the end as today.
- A Button moved to another Page in the editor's Page picker follows the same rule.

## 9. Delete

- "−" sits on each Button's top-left corner in arrange mode: 22 pt drawn, 44 pt hit area.
- Tapping it shows an alert: **Delete "Label"?** "This removes the Button and its Phrase from your
  Deck on all your phones and iPads." Buttons: **Delete** (destructive), **Cancel**.
- On Delete the Button shrinks away. Other Buttons do not move (the gap stays). Its spots on every
  width are deleted with it.
- Recommended: the long-press menu's Delete uses the same alert (open question 4).

## 10. Haptics and animation

| Moment | Haptic | Animation |
|---|---|---|
| Enter arrange mode | medium impact | Jiggle starts: about 1.5° rotation, about 0.13 s period, random phase per Button; "−" badges fade in; Done appears |
| Pick up | light impact | Button lifts: scale 1.08, shadow, follows finger |
| Target changes and others move | selection | Neighbors move with `.snappy` (as the pinch reflow does today) |
| Edge flip | light impact | Edge highlight fills, then page slide |
| Drop | soft impact | Settles into its Squares with `.snappy` |
| Delete confirmed | medium impact | Button scales to 0 and fades |
| Done | none | Jiggle stops, badges fade out |

**Reduce Motion**: no jiggle and no lift scale. Arrange mode shows the "−" badges and a dashed outline
on each Button instead; reflow uses a cross-fade.

## 11. Accessibility (required)

Dragging must never be the only way.

- **Entering**: "Edit Deck" is in the long-press menu, which VoiceOver reaches as an action. On entry,
  announce "Editing Deck. Use actions to move Buttons. Done is in the toolbar." and move focus to the
  first Button.
- **Custom actions on each Button in arrange mode**: Move left, Move right, Move up, Move down (one
  Square; treated as a drop on that target, with the same reflow), Move to Page… (a list of Pages;
  the Button goes to the end of that Page, rule 8), Delete (same alert). Actions that can't apply are
  left out (no Move left in column 1; no Move up in row 1).
- **Announcements** after a move: "Moved to row 2, column 3" plus "2 Buttons moved" when others
  shifted. The Button's accessibility value is "Row r, column c" in arrange mode.
- **Reading order** follows the displayed spots (row, then column), not `order`, in both modes.
- **Normal mode** Buttons do not get move actions, so a VoiceOver user sending Phrases isn't slowed
  down.
- **Voice Control and Switch Control** use the same actions. "−" is labeled "Delete Label"; Done is a
  standard toolbar Button labeled "Done".
- **Full Keyboard Access** on iPad reaches the same custom actions.
- UI tests can drive the custom actions, which gives stable tests for moves alongside the drag test.

## 12. Data note (for the engineer)

- Store, per Button and per width (`compact`, `regular`): column and row. A missing entry means no
  spot.
- It must sync. The issue prefers one new record type (or one new field) over changes to the existing
  `CD_` fields; a Debug run then "Deploy Schema Changes" to Production before release.
- Deleting a Button deletes its spots. A Button moved to another Page drops its old spots and gets
  new ones by rule 8.
- Nothing about the column count is synced; it stays per phone or iPad as today.
- Unspotted Buttons and overlaps must be tolerated on read, since another phone or iPad can add or
  resize Buttons at any time.

## 13. Open questions (recommended option first)

1. **Pages and rows.** (a) Keep Pages scrolling with unbounded rows, so a Page is never full.
   (b) Fix rows to what fits on screen, like the Home Screen; a full Page sends new or displaced
   Buttons to the next Page. (c) Fixed rows, and a full Page refuses the drop.
2. **What "width" means.** (a) Horizontal size class (compact or regular), as the column count
   already uses. (b) The column count, so each pinch level keeps its own arrangement. (c) Point-width
   buckets (for example under 500, 500 to 900, over 900).
3. **Holding at the edge of the last Page.** (a) Nothing happens; Pages are made in the Pages sheet.
   (b) Make a new Page named "Page N" and drop there, like the Home Screen.
4. **Delete from the long-press menu.** (a) Use the same confirmation alert as "−". (b) Leave it
   unconfirmed as today.
5. **Confirmation style.** (a) Alert before deleting. (b) Delete at once with an Undo banner for 5 s.
6. **Tapping a Button in arrange mode.** (a) Nothing. (b) Opens the editor.
7. **Leaving arrange mode.** (a) Done or a tap on empty space. (b) Done only.
8. **Pinch in arrange mode.** (a) Disabled. (b) Allowed when nothing is being dragged.

## Decisions on the open questions (Eric, 2026-10-25)

All eight went to the recommended option:
1. Pages keep scrolling with no row limit, so a Page is never full.
2. "Width" means the horizontal size class (compact or regular).
3. Holding at the edge of the last Page does nothing. New Pages come from the Pages sheet.
4. Delete from the long-press menu uses the same confirmation as "−".
5. The confirmation is an alert ("Delete <label>?", with Delete and Cancel).
6. Tapping a Button in arrange mode does nothing.
7. Arrange mode ends with Done or a tap on empty space.
8. Pinch is off in arrange mode.
