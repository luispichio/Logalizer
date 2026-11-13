#!/usr/bin/env bash
set -euo pipefail
SOURCE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMP_DIR"' EXIT
mkdir -p "$TEMP_DIR/repo/scripts" "$TEMP_DIR/bin" "$TEMP_DIR/dist"
cp "$SOURCE_DIR/scripts/create-github-release.sh" "$TEMP_DIR/repo/scripts/"
REAL_GIT="$(command -v git)"
cat > "$TEMP_DIR/bin/git" <<EOF
#!/usr/bin/env bash
if [[ "\${1:-}" == push ]]; then exit 0; fi
exec "$REAL_GIT" "\$@"
EOF
cat > "$TEMP_DIR/bin/gh" <<'EOF'
#!/usr/bin/env bash
if [[ "$1 $2" == 'release view' ]]; then exit 1; fi
if [[ "$1 $2" == 'release create' ]]; then touch "$GH_CREATED"; fi
EOF
chmod +x "$TEMP_DIR/bin/git" "$TEMP_DIR/bin/gh"
export PATH="$TEMP_DIR/bin:$PATH" GH_CREATED="$TEMP_DIR/published" DIST_DIR="$TEMP_DIR/dist"
cd "$TEMP_DIR/repo"
git init -q
git add scripts
git -c user.name=Test -c user.email=test@example.com commit -q -m initial
git tag v1.2.3
printf 'commit=%s\ntag=v1.2.3\nclean=1\n' "$(git rev-parse HEAD)" > "$DIST_DIR/RELEASE-METADATA"
echo package > "$DIST_DIR/logalizer.deb"
(cd "$DIST_DIR" && sha256sum ./* > SHA256SUMS)
scripts/create-github-release.sh v1.2.3
[[ -f "$GH_CREATED" ]]
rm "$GH_CREATED"
echo tampered >> "$DIST_DIR/logalizer.deb"
if scripts/create-github-release.sh v1.2.3; then
    echo "Tampered artifacts were accepted" >&2
    exit 1
fi
[[ ! -e "$GH_CREATED" ]]
echo "Release validation tests passed."
