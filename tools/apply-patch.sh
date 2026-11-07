#!/bin/bash
# Apply a patch, build every configuration, run every suite, regenerate the
# .docx files whose .md changed, and stage the result. It stops there.
#
# It does NOT commit and does NOT push: the commit message, the staging review
# and the push stay with the person, by the project's rule. What this removes is
# the copying: one command instead of eight blocks.
#
#   tools/apply-patch.sh ~/Downloads/step2/step2.patch
#
# Every step prints what it measured. A step that fails stops the script with
# the output still on screen, so nothing is applied on top of a broken state.
set -u
# Without pipefail a failing command at the head of a pipe is invisible, and
# `cmake --build | grep` would report success on a failed build.
set -o pipefail

PATCH=${1:-}
if [ -z "$PATCH" ]; then
    echo "usage: tools/apply-patch.sh <patch>"
    exit 2
fi
if [ ! -f "$PATCH" ]; then
    echo "no such patch: $PATCH"
    exit 2
fi
# `-d .git` is wrong inside a git worktree, where .git is a file pointing at the
# real gitdir. Ask git instead of looking for the directory.
if [ "$(git rev-parse --show-toplevel 2>/dev/null)" != "$(pwd -P)" ]; then
    echo "run this from the repository root ($(git rev-parse --show-toplevel 2>/dev/null || echo 'not a repository'))"
    exit 2
fi

step() { printf '\n=== %s ===\n' "$1"; }
die() { printf '\nSTOPPED: %s\n' "$1"; exit 1; }

step "1. state before"
git log --oneline -1
if [ -n "$(git status --porcelain --untracked-files=all)" ]; then
    echo "the working tree is not clean:"
    git status --short --untracked-files=all
    die "commit or stash first, so the patch cannot mix with other changes"
fi
echo "working tree clean"

step "2. does the patch apply"
git apply --check "$PATCH" || die "the patch does not apply to this tree"
echo "applies cleanly"
git apply "$PATCH" || die "apply failed"
echo "applied"
git diff --stat | tail -3
git diff --check || die "whitespace errors in the patch"

step "3. build and test, every configuration"
for cfg in "build:Debug" "build-gl:Debug" "build-rel:Release"; do
    dir=${cfg%%:*}
    name=${cfg##*:}
    if [ ! -d "$dir" ]; then
        printf -- '--- %s (%s): not configured, skipping\n' "$dir" "$name"
        continue
    fi
    printf -- '--- %s (%s) build\n' "$dir" "$name"
    build_log=$(cmake --build "$dir" --parallel 2>&1)
    if printf '%s\n' "$build_log" | grep -E 'error:|warning:'; then
        die "build warnings or errors in $dir (full output above)"
    fi
    printf '  built with no warning\n'
    printf -- '--- %s (%s) ctest\n' "$dir" "$name"
    test_log=$(ctest --test-dir "$dir" 2>&1)
    printf '%s\n' "$test_log" | grep -E 'tests passed|tests failed' || true
    printf '%s\n' "$test_log" | grep -q '100% tests passed' || die "a test failed in $dir"
done
if [ -d build-gl ]; then
    printf -- '--- build-gl labelled opengl\n'
    gl_log=$(ctest --test-dir build-gl -L opengl 2>&1)
    printf '%s\n' "$gl_log" | grep -E 'tests passed|tests failed' || true
    printf '%s\n' "$gl_log" | grep -q '100% tests passed' || die "a labelled test failed"
fi

step "4. regenerate only the .docx whose .md changed"
changed=$(git diff --name-only | grep '\.md$' || true)
if [ -z "$changed" ]; then
    echo "no .md changed: nothing to regenerate (running pandoc twice on an"
    echo "unchanged .md would only churn the binary)"
else
    for md in $changed; do
        docx="${md%.md}.docx"
        [ -f "$docx" ] || { echo "  $md has no .docx beside it, skipping"; continue; }
        pandoc "$md" -o "$docx" || die "pandoc failed on $md"
        unzip -t "$docx" >/dev/null || die "$docx is not a readable zip"
        printf '  %s regenerated\n' "$docx"
    done
fi

step "5. boundary rules"
printf 'contract free of GL types: '
grep -c 'GLuint\|GLenum\|SDL_' include/grace/render/render_backend.hpp include/grace/render/render_target.hpp | grep -v ':0' && die "GL type leaked into the contract"
echo "clean"
printf '#version outside src/render/opengl/: '
grep -rn '#version' src/ include/ tests/ 2>/dev/null | grep -v 'src/render/opengl/' | grep -v 'test_shader_source_rule' || echo "none"
printf 'grace_core platform-free: '
grep -rln 'SDL_\|GL/' src/core/ include/grace/core/ 2>/dev/null || echo "clean"
printf 'debug markers: '
grep -rn 'TODO\|FIXME\|XXX\|HACK' src/ include/ tests/ 2>/dev/null | wc -l
printf 'application.cpp (abstraction-leak canary): '
git diff --name-only | grep -q 'src/app/application.cpp' && echo "CHANGED - check this" || echo "untouched"

step "6. stage"
# Exact paths only, never `git add .` / `git add -A` (the project's rule). The
# tree was verified clean in step 1, so these are exactly the patch's files;
# naming them keeps that true even if the rule is read later without step 1.
# `git diff --name-only` lists tracked files only, so a patch that ADDS a file
# left it untracked and out of the commit. Measured: this script staged 13 of
# the 14 paths of the milestone 7 third increment, silently dropping the new
# tests/test_volume_inject.cpp. `git status --porcelain` lists both, and its
# `??` prefix is the case that was being lost.
git status --porcelain --untracked-files=all | while IFS= read -r entry; do
    f=${entry#???}
    case "$f" in
        *" -> "*) f=${f#* -> } ;;   # a rename reports both sides
    esac
    if [ -e "$f" ]; then git add -- "$f"; else git rm --cached -q -- "$f"; fi
done
staged=$(git diff --cached --name-only | wc -l)
changed=$(git status --porcelain --untracked-files=all | wc -l)
if [ "$staged" -ne "$changed" ]; then
    echo "staged $staged of $changed paths:"
    git status --short --untracked-files=all
    die "a path was left out of the staging area"
fi
printf 'staged all %s paths\n' "$staged"
git diff --cached --check || die "whitespace in the staged diff"
git diff --cached --name-status
printf 'private material in the patch: '
git diff --cached --name-only | grep -E 'grace2|sdk|decompiled|ghidra|ida|phys3x' && die "private material staged" || echo "none"

step "next: review, then commit and push"
cat <<'NEXT'
  The change is staged and nothing else. To finish:

    git commit -F <message-file>
    git push origin main
    ./tools/verify-published-tree.sh

  Nothing was committed, so `git restore --staged --worktree .` undoes all of it.
NEXT
