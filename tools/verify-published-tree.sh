#!/bin/sh
# Build and test the tree as published, not the working tree.
#
# The working tree can contain unstaged files, so a local build proves nothing
# about what a clone would get. This script clones the current branch into a
# temporary directory and runs the full matrix there.
set -eu

BRANCH="${1:-main}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

git clone --quiet --branch "$BRANCH" . "$WORK/tree"

# Report whether the local branch is ahead of the remote, because a clone
# of the local repository would otherwise verify commits that were never
# published.
if git rev-parse --verify --quiet "origin/$BRANCH" >/dev/null; then
    ahead="$(git rev-list --count "origin/$BRANCH..$BRANCH")"
    if [ "$ahead" -gt 0 ]; then
        printf '%s\n' "warning: $BRANCH is $ahead commit(s) ahead of origin/$BRANCH"
        printf '%s\n' "the tree below is what is committed locally, not what is published"
    fi
fi
cd "$WORK/tree"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build build --parallel >/dev/null
ctest --test-dir build --output-on-failure

cmake -S . -B build-gl -G Ninja -DCMAKE_BUILD_TYPE=Debug -DGRACE_ENABLE_OPENGL=ON >/dev/null
cmake --build build-gl --parallel >/dev/null
ctest --test-dir build-gl --output-on-failure
