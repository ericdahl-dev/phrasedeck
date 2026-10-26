#!/bin/sh
# Prints run=true to $GITHUB_OUTPUT when this build should do its work: always on a push, and on a
# pull request only when the diff touches a path matching the extended regex REGEX. The jobs stay
# (they're required checks) and skip their steps otherwise. Needs fetch-depth: 0.
# Usage: touches.sh REGEX
set -eu
run=true
if [ "${GITHUB_EVENT_NAME:-}" = pull_request ]; then
	changed=$(git diff --name-only "origin/$GITHUB_BASE_REF...HEAD")
	echo "Changed files:"; echo "$changed"
	echo "$changed" | grep -Eq "$1" || run=false
fi
echo "run=$run"
echo "run=$run" >> "${GITHUB_OUTPUT:-/dev/null}"
