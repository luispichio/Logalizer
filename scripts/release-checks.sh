#!/usr/bin/env bash
# Shared preconditions for starting or retrying a publication.
check_release_checkout() {
    if [[ "$(git symbolic-ref --quiet --short HEAD || true)" != master ]]; then
        echo "Releases must be published from the master branch." >&2
        return 1
    fi
    if [[ -n "$(git status --porcelain --untracked-files=normal)" ]]; then
        echo "Working tree must be clean, including untracked files." >&2
        return 1
    fi
    if [[ "$(git rev-parse --is-shallow-repository)" != false ]]; then
        echo "A complete Git history is required." >&2
        return 1
    fi
    # Explicitly fetch master even when origin's configured fetch refspec is narrow.
    git fetch origin --tags '+refs/heads/master:refs/remotes/origin/master' || return 1
    if [[ "$(git rev-parse HEAD)" != "$(git rev-parse refs/remotes/origin/master)" ]]; then
        echo "master must match origin/master; integrate or push commits before releasing." >&2
        return 1
    fi
}
