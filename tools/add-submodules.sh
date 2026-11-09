#!/bin/bash
# Add the engine's middleware dependencies as pinned git submodules.
#
# The repository stays source-only. Each entry pins one upstream commit in
# .gitmodules plus a git link (mode 160000); ten to hundreds of MB never enter
# this tree. A fresh clone pulls every dependency with:
#
#   git submodule update --init --depth 1
#
# Per entry the script accepts three states:
#   1. nothing on disk        -> blobless clone, then it shallow-fetches THE
#                                PIN ITSELF and checks out at it (no pin here
#                                is the remote HEAD);
#   2. an existing local fork -> adopt that checkout and set its head to the
#                                same sha (fetching only when missing locally);
#   3. already registered here -> verify the recorded sha equals the pin, do
#                                nothing else.
# Then each entry is registered with `git submodule add` and verified through
# our own ls-files (160000) read instead of trusting tool silence. It stages
# .gitmodules and the gitlinks but does NOT commit: reviewing what goes into
# the commit and the commit message stay with the person, by the project's
# rule.
#
# Usage: tools/add-submodules.sh [clone-root]
#   clone-root is where existing SDK clones live (default: the repository
#   root). If a clone of an SDK does not exist there, the script clones it
#   from GitHub.
#
# The pins (recorded in docs/implementation-status.md, "Dependency versions"):
#   physx-3.4     NVIDIAGameWorks/PhysX-3.4
#                 5e42a5f112351a223c19c17bb331e6c55037b8eb
#                 (v3.4.0-rc-2-22)
#   bullet3       bulletphysics/bullet3
#                 d3afe76132fc76b0f9b2359cbaef47a3766e7fbb (3.25)
#   box2d         erincatto/box2d
#                 dd8bd3fb525f0f33f1079f8ca3c5770783c44a53 (v3.2.0)
#   angelscript   anjo76/angelscript
#                 0020e24ac91a159dc68386c22137fc37516be216 (v2.38.0)
#   sdl2          libsdl-org/SDL
#                 859844eae358447be8d66e6da59b6fb3df0ed778 (release-2.30.0)
#   openal-soft   kcat/openal-soft
#                 d3875f333fb6abe2f39d82caca329414871ae53b (1.23.1)
#   freetype      freetype/freetype
#                 934c366090b19542d5a4204307c72c5b69bc75cc (VER-2-13-2)
#   libogg        xiph/ogg
#                 65b355cc20171010ecabf245e7b339aeab8ddbb9 (v1.3.5)
#   libvorbis     xiph/vorbis
#                 0c55fa38933fd4bdb7db7c298b27e7bf2f2c5e98 (v1.3.7)
#   libtheora     xiph/theora
#                 35da3dc1f96f414bdaa1146fa6b67033fdf64ed6 (v1.1.1)

set -eu

# The repository root (the script lives in tools/).
ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
CLONE_ROOT="${1:-$ROOT}"
cd "$ROOT"

say() { printf '%s\n' "$1"; }
die() {
    if [ "${STATE_TOUCHED:-0}" = "1" ]; then
        resume
    fi
    printf '\nSTOPPED: %s\n' "$1" >&2
    exit 1
}

# If the script fails mid-way, it must not leave a half-staged state behind:
# the gitlinks and .gitmodules it already staged go out of the index again.
# A full reset would also wipe what our text patch left in this tree, so this
# is an index-only rollback; only .gitmodules, which the script itself
# creates, gets its file deleted too. The SDK clones on disk survive
# untouched (they are work, and re-running the script adopts them).
resume() {
    git reset --mixed --quiet
    if ! git cat-file -e "HEAD:.gitmodules" 2>/dev/null; then
        rm -f .gitmodules
    fi
    say "failed mid-way: the staged state is undone; the clones on disk stay,"
    say "re-run the script to adopt them"
}

say "=== the middleware dependencies ==="

# The submodule entries: path, url, pinned commit.
# physx-3.4 is marked dirty: the CMake patches the SDK's -Werror build flags
# in place, which makes its checkout "modified" in git status.
ENTRIES=(
    "physx-3.4|https://github.com/NVIDIAGameWorks/PhysX-3.4|\
5e42a5f112351a223c19c17bb331e6c55037b8eb"
    "bullet3|https://github.com/bulletphysics/bullet3|\
d3afe76132fc76b0f9b2359cbaef47a3766e7fbb"
    "box2d|https://github.com/erincatto/box2d|\
dd8bd3fb525f0f33f1079f8ca3c5770783c44a53"
    "angelscript|https://github.com/anjo76/angelscript|\
0020e24ac91a159dc68386c22137fc37516be216"
    "sdl2|https://github.com/libsdl-org/SDL|\
859844eae358447be8d66e6da59b6fb3df0ed778"
    "openal-soft|https://github.com/kcat/openal-soft|\
d3875f333fb6abe2f39d82caca329414871ae53b"
    "freetype|https://github.com/freetype/freetype|\
934c366090b19542d5a4204307c72c5b69bc75cc"
    "libogg|https://github.com/xiph/ogg|\
65b355cc20171010ecabf245e7b339aeab8ddbb9"
    "libvorbis|https://github.com/xiph/vorbis|\
0c55fa38933fd4bdb7db7c298b27e7bf2f2c5e98"
    "libtheora|https://github.com/xiph/theora|\
35da3dc1f96f414bdaa1146fa6b67033fdf64ed6"
)

# Staged changes would mix with what this script itself goes on staging,
# which a later selective review cannot tell apart anymore. Unstaged ones,
# though, are exactly how our text patch leaves its parts lying around here:
# applying our diff from earlier in these instructions and only afterwards
# invoking us keeps every change safely separable.
if [ -n "$(git diff --cached --name-only | head -1)" ]; then
    die "there are staged changes; commit them before adding submodules"
fi

gitrev() { git -C "$1" rev-parse HEAD; }

for entry in "${ENTRIES[@]}"; do
    IFS='|' read -r path url pin <<< "$entry"
    say ""
    say "--- $path -> $url @ ${pin:0:8}"

    dir="$CLONE_ROOT/$path"

    if [ -d "$dir/.git" ] || [ -f "$dir/.git" ]; then
        # An existing clone: adopt it. If its local history does not carry the
        # pin, shallow-fetch that one commit first.
        say "adopting the existing clone at $dir"
        if ! git -C "$dir" cat-file -e "$pin^{commit}" 2>/dev/null; then
            say "the pin is not in the local history; shallow-fetching it"
            git -C "$dir" fetch --quiet --depth 1 origin "$pin" \
                || die "$path: cannot fetch the pin $pin"
        fi
        git -C "$dir" checkout --quiet --detach "$pin" \
            || die "$path: cannot check out the pin $pin"
    elif [ -e "$dir" ]; then
        die "$path: $dir exists but is not a git repository; move it aside"
    else
        say "cloning $url (blobless, no checkout)"
        git clone --quiet --filter=blob:none --no-checkout "$url" "$dir" \
            || die "$path: clone failed"
        say "fetching the pinned commit"
        git -C "$dir" fetch --quiet --depth 1 origin "$pin" \
            || die "$path: cannot fetch the pin $pin"
        git -C "$dir" checkout --quiet --detach "$pin" \
            || die "$path: cannot check out the pin $pin"
    fi

    # A gitlink records a commit, not a tag object, and some pins are
    # annotated tags (measured: FreeType's VER-2-13-2 checks out at 920c5502,
    # the commit behind the tag object). Resolve the pin to its commit inside
    # the clone, where the object exists, and pin that commit.
    pin_commit="$(git -C "$dir" rev-parse "$pin^{commit}")" \
        || die "$path: cannot resolve the pin $pin to a commit"

    actual="$(gitrev "$dir")"
    if [ "$actual" != "$pin_commit" ]; then
        die "$path: the checkout is $actual, not the pin commit $pin_commit"
    fi

    if [ "$CLONE_ROOT" != "$ROOT" ]; then
        # The pinned checkout lives outside this tree. git submodule add
        # would clone the URL at the remote HEAD (measured: the AngelScript
        # clone at CLONE_ROOT registered as cd01f495, not the pin), so the
        # pinned checkout is copied into the tree first and the add then
        # adopts an existing git repository at the path, recording its head
        # (the pin) as the gitlink.
        if [ -e "$ROOT/$path" ]; then
            if [ -d "$ROOT/$path/.git" ] || [ -f "$ROOT/$path/.git" ]; then
                git -C "$ROOT/$path" checkout --quiet --detach "$pin_commit" \
                    || die "$path: cannot check out the pin in $ROOT/$path"
            else
                die "$path: $ROOT/$path exists but is not a git repository"
            fi
        else
            say "copying the pinned checkout into $ROOT/$path"
            cp -a "$dir" "$ROOT/$path" \
                || die "$path: cannot copy the pinned checkout into the tree"
        fi
    fi

    # A path ignored by .gitignore cannot be registered as a submodule
    # ("The following paths are ignored..."). The SDK clones were ignored
    # before this change, so the script removes that line as part of the
    # conversion; the git tree records the removal in the same commit as the
    # gitlinks, which is the only state that is true for both.
    if [ -f .gitignore ] && grep -qF "/$path/" .gitignore; then
        STATE_TOUCHED=1
        grep -vF "/$path/" .gitignore > .gitignore.tmp
        mv .gitignore.tmp .gitignore
        git add .gitignore
        say "removed the /$path/ ignore line from .gitignore"
    fi

    # Idempotency: a re-run must not re-clone or re-add an entry that is
    # already registered at the pin.
    if [ "$(git ls-files --stage -- "$path" | awk '{print $1, $2}')" \
        = "160000 $pin_commit" ]; then
        say "already registered at the pin; nothing to do"
        continue
    fi

    # Register the submodule. The gitlink records the checked-out commit (the
    # pin), and .gitmodules records the URL and the ignore rule.
    if ! git submodule add "$url" "$path"; then
        die "$path: git submodule add failed"
    fi

    # The gate: read the index back, not the tool's silence.
    registered="$(git ls-files --stage -- "$path" | awk '{print $1, $2}')"
    if [ "$registered" != "160000 $pin_commit" ]; then
        die "$path: registered as '$registered', expected '160000 $pin_commit'"
    fi
    say "pinned at $actual (from $pin); staged"
done

# physx-3.4's checkout is patched by the CMake build (the -Werror flags), so
# its submodule is marked dirty in .gitmodules; the gitlink stays pinned.
git config -f .gitmodules submodule.physx-3.4.ignore dirty
git add .gitmodules

say ""
say "=== staged ==="
git status --porcelain --untracked-files=no
say ""
say ".gitmodules:"
cat .gitmodules
say ""
say "git submodule status:"
git submodule status
say ""
say "next: review the staged entries, then commit them"
say "(the commit message stays with you)."
