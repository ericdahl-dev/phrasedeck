#!/bin/sh
# Checks a built phrased-bin package (#78): the Helper and its user unit, installed but not enabled
# (each user opts in), and the typing tools as dependencies (Hyprland and other wlroots compositors
# use wtype). Needs pacman.
set -eu
pkg=$1
fail() { echo "check-pkg: $*" >&2; exit 1; }
[ -f "$pkg" ] || fail "no such file: $pkg"

files=$(pacman -Qlpq "$pkg")
for path in /usr/bin/phrased /usr/lib/systemd/user/phrased.service; do
	echo "$files" | grep -qx "$path" || fail "missing $path"
done
tar -tvf "$pkg" usr/bin/phrased | grep -q '^-rwxr-xr-x' || fail "/usr/bin/phrased isn't 0755"
echo "$files" | grep -q 'graphical-session.target.wants' && fail "enables the unit for every user; it must be opt-in"
pacman -Qip "$pkg" | grep -q '^Packager *: .*<eric@skeyelab.com>$' || fail "packager isn't eric@skeyelab.com"
tar -xOf "$pkg" .INSTALL 2>/dev/null | grep -q 'systemctl --user enable --now phrased' ||
	fail "the install script doesn't tell the user how to turn it on"

depends=$(pacman -Qip "$pkg" | sed -n 's/^Depends On *: //p')
for dep in wtype wl-clipboard; do
	echo "$depends" | grep -qw "$dep" || fail "doesn't depend on $dep (Depends On: $depends)"
done

optdepends=$(pacman -Qip "$pkg" | sed -n '/^Optional Deps/,/^Required By/p')
for dep in ydotool xdotool xclip; do
	echo "$optdepends" | grep -qw "$dep" || fail "doesn't list $dep as an optional dependency (GNOME/KDE, X11; #84)"
done

unit=$(tar -xOf "$pkg" usr/lib/systemd/user/phrased.service)
echo "$unit" | grep -qx 'ExecStart=/usr/bin/phrased' || fail "the unit doesn't run /usr/bin/phrased"
echo "$unit" | grep -qx 'StartLimitBurst=3' || fail "the unit has no start limit"
echo "check-pkg: $pkg OK"
