#!/bin/sh
# Checks a built .deb (#78): the Helper and its user unit, installed but not enabled (each user opts
# in), the GNOME-safe dependencies, and that a failing unit gives up instead of looping. Needs dpkg-deb.
set -eu
deb=$1
fail() { echo "check-deb: $*" >&2; exit 1; }
[ -f "$deb" ] || fail "no such file: $deb"

contents=$(dpkg-deb -c "$deb")
for path in ./usr/bin/phrased ./usr/lib/systemd/user/phrased.service; do
	echo "$contents" | grep -q " $path\( \|$\)" || fail "missing $path"
done
echo "$contents" | grep ' ./usr/bin/phrased$' | grep -q '^-rwxr-xr-x' || fail "/usr/bin/phrased isn't 0755"
echo "$contents" | grep -q 'graphical-session.target.wants' && fail "enables the unit for every user; it must be opt-in"
dpkg-deb -f "$deb" Maintainer | grep -q '<eric@skeyelab.com>$' || fail "maintainer isn't eric@skeyelab.com"
dpkg-deb --ctrl-tarfile "$deb" | tar -xO ./postinst 2>/dev/null | grep -q 'systemctl --user enable --now phrased' ||
	fail "postinst doesn't tell the user how to turn it on"

# wtype can't type on GNOME (Mutter), so it's only suggested until #69; wl-clipboard is for paste.
depends=$(dpkg-deb -f "$deb" Depends)
echo "$depends" | grep -qw wtype && fail "depends on wtype, which GNOME can't use (Depends: $depends)"
for dep in wl-clipboard ydotool; do
	dpkg-deb -f "$deb" Recommends | grep -qw "$dep" || fail "doesn't recommend $dep (GNOME and KDE type with ydotool, #84)"
done
dpkg-deb -f "$deb" Suggests | grep -qw wtype || fail "doesn't suggest wtype"

unit=$(dpkg-deb --fsys-tarfile "$deb" | tar -xO ./usr/lib/systemd/user/phrased.service)
echo "$unit" | grep -qx 'ExecStart=/usr/bin/phrased' || fail "the unit doesn't run /usr/bin/phrased"
echo "$unit" | grep -qx 'WantedBy=graphical-session.target' || fail "the unit isn't wanted by graphical-session.target"
# A second user can't bind :47811, so phrased exits; without a limit Restart= would retry forever.
echo "$unit" | grep -qx 'StartLimitBurst=3' && echo "$unit" | grep -qx 'StartLimitIntervalSec=60' ||
	fail "the unit has no start limit, so a phrased that can't start restarts forever"
echo "check-deb: $deb OK"
