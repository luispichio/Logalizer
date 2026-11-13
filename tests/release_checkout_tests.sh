#!/usr/bin/env bash
set -euo pipefail
SOURCE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMP_DIR"' EXIT
git init -q --bare "$TEMP_DIR/origin.git"
git init -q -b master "$TEMP_DIR/repo"
cd "$TEMP_DIR/repo"
git config user.name Test
git config user.email test@example.com
echo initial > tracked
git add tracked
git commit -q -m initial
git remote add origin "$TEMP_DIR/origin.git"
git push -q -u origin master
source "$SOURCE_DIR/scripts/release-checks.sh"
expect_failure() {
    if check_release_checkout; then
        echo "Release checkout unexpectedly accepted: $1" >&2
        exit 1
    fi
}
check_release_checkout
git switch -q -c feature
expect_failure 'wrong branch'
git switch -q master
echo changed > untracked
expect_failure 'untracked file'
rm untracked
echo modified >> tracked
expect_failure 'modified tracked file'
git add tracked
expect_failure 'staged change'
git commit -q -m 'tracked change'
expect_failure 'local commit not pushed'
git push -q origin master
check_release_checkout
git clone -q --branch master "$TEMP_DIR/origin.git" "$TEMP_DIR/other"
git -C "$TEMP_DIR/other" -c user.name=Test -c user.email=test@example.com commit -q --allow-empty -m remote
git -C "$TEMP_DIR/other" push -q origin master
expect_failure 'remote commit not integrated'
git pull -q --ff-only origin master
check_release_checkout
git switch -q --detach
expect_failure 'detached HEAD'
echo "Release checkout tests passed."
