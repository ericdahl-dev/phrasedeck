# phrasedeck

An iPhone deck of buttons that types saved phrases into a computer, through a small program running on that computer.

## Language

**Computer**:
A machine paired with the phone, as the phone knows it: a name, an address and a token.
_Avoid_: server, host, machine, device

**Helper**:
The `phrased` program running on a Computer. It receives phrases from the phone and types them.
_Avoid_: daemon, server, agent

**Ready**:
A Helper is Ready when it has everything it needs to type on its Computer (the OS permission and the typing tools). A Send to a Helper that isn't Ready is Not typed.
_Avoid_: healthy, enabled, authorized

**Button**:
A pressable area on a Page: a label, a color and a Size, holding one Phrase.
_Avoid_: key, tile, shortcut

**Phrase**:
The text a Button types into the Computer.
_Avoid_: snippet, prompt, macro

**Deck**:
Everything you have: all your Pages and their Buttons, the same on each of your phones and iPads. There is one Deck per person.
_Avoid_: board, profile, layout

**Page**:
One named screen of Buttons in the Deck. You swipe between Pages.
_Avoid_: tab, screen, folder

**Square**:
The grid's unit cell on one phone or iPad. Pinching the Deck changes how big a Square is on that phone or iPad only.
_Avoid_: cell, zoom, scale, grid density

**Size** (of a Button):
How many Squares a Button covers, from 1x1 to 3x3 (for example 2x1). It's the same everywhere the Deck appears, but a Page narrower than the Size shows the Button only as wide as the Page.
_Avoid_: span

**Arrange mode**:
The Home Screen-style mode for moving and deleting Buttons: they jiggle, taps don't type, and Done ends it.
_Avoid_: edit mode, jiggle mode, wiggle mode

**Spot**:
Where a Button sits on its Page: the column and row of its top-left Square. Gaps between Spots are allowed.
_Avoid_: position, slot, cell, index

**Layout width**:
The kind of screen an arrangement belongs to: compact (iPhone) or regular (iPad). Each Layout width keeps its own Spots, while the Deck itself is the same everywhere.
_Avoid_: size class, device width, breakpoint

## Reaching a Computer

**Route**:
The path a phrase takes from the phone to a Computer. There are two: the Tailnet route and the Home network route.
_Avoid_: connection, transport, channel

**Tailnet route**:
Through Tailscale, so it works from anywhere both devices are signed in.
_Avoid_: VPN, remote, Tailscale route

**Home network route**:
Directly over the same Wi-Fi, encrypted and checked against the Computer's Pin. Used only when the Computer is Nearby.
_Avoid_: LAN, local, Bonjour, direct

**Pin**:
The Computer's identity, which the phone remembers from pairing. On the Home network route, a Computer that doesn't match its Pin isn't trusted, and nothing is sent.
_Avoid_: fingerprint, certificate, key, hash

**Nearby**:
A Computer whose Helper the phone sees on the same Wi-Fi right now, paired or not. Only a Nearby Computer can use the Home network route.
_Avoid_: discovered, local, Bonjour

## Pairing

**Pairing window**:
The two minutes after `phrased pair` runs, when the Helper accepts a new Device.
_Avoid_: session, invite

**Pairing code**:
The 6 digits the Helper shows during a Pairing window.
_Avoid_: PIN (it clashes with Pin), OTP, password

**Pairing link**:
What the QR code holds: the Pairing code, where to reach the Computer, and its Pin when the Home network route is on.
_Avoid_: QR (that's only how it's shown), invite link

**Token**:
The secret a Device keeps after pairing, and shows the Helper with every Send.
_Avoid_: key, password, credential

**Device**:
Anything paired with a Helper that sends Phrases (a phone, an iPad, a desk panel), holding its own Token. Revoking a Device cuts off only that one.
_Avoid_: client, phone (other things pair too)

## Sending

**Send**:
One tap's attempt to type a Button's Phrase on a Computer.
_Avoid_: request, call, job

**Typed**:
The Send finished; the whole Phrase was typed.

**Not typed**:
Nothing reached the screen (the Computer was busy, unreachable, not paired, or the Send was blocked before leaving the phone). It's safe to tap again.
_Avoid_: failed, error

**Maybe typed**:
Part or all of the Phrase may have been typed. Check the Computer before tapping again.
_Avoid_: unknown, timed out, partial
