#!/bin/sh
# Checks a built .deb (#78): the Helper, its user unit enabled for every graphical login, and the
# typing tools as dependencies, so nothing is left to set up by hand. Needs dpkg-deb.
set -eu
deb=$1
fail() { echo "check-deb: $*" >&2; exit 1; }
[ -f "$deb" ] || fail "no such file: $deb"

contents=$(dpkg-deb -c "$deb")
for path in ./usr/bin/phrased ./usr/lib/systemd/user/phrased.service \
            ./usr/lib/systemd/user/graphical-session.target.wants/phrased.service; do
	echo "$contents" | grep -q " $path\( \|$\)" || fail "missing $path"
done
echo "$contents" | grep ' ./usr/bin/phrased$' | grep -q '^-rwxr-xr-x' || fail "/usr/bin/phrased isn't 0755"
echo "$contents" | grep -q ' ./usr/lib/systemd/user/graphical-session.target.wants/phrased.service -> ../phrased.service$' ||
	fail "the wants entry isn't a link to ../phrased.service"

# wtype can't type on GNOME (Mutter), so it's only suggested until #69; wl-clipboard is for paste.
depends=$(dpkg-deb -f "$deb" Depends)
echo "$depends" | grep -qw wtype && fail "depends on wtype, which GNOME can't use (Depends: $depends)"
dpkg-deb -f "$deb" Recommends | grep -qw wl-clipboard || fail "doesn't recommend wl-clipboard"
dpkg-deb -f "$deb" Suggests | grep -qw wtype || fail "doesn't suggest wtype"

unit=$(dpkg-deb --fsys-tarfile "$deb" | tar -xO ./usr/lib/systemd/user/phrased.service)
echo "$unit" | grep -qx 'ExecStart=/usr/bin/phrased' || fail "the unit doesn't run /usr/bin/phrased"
echo "$unit" | grep -qx 'WantedBy=graphical-session.target' || fail "the unit isn't wanted by graphical-session.target"
echo "check-deb: $deb OK"
