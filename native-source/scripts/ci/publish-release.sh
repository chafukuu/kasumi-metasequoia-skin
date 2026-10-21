#!/usr/bin/env bash
# Attach the installer to the selected release, append the artifact details to its notes and publish.
#
# The release-please tag is the stable version selected for a future manual release. An automatic
# build gets its own SemVer prerelease tag so that the prerelease and the eventual stable release
# can coexist: v0.6.2-beta is the automatic build, while v0.6.2 remains the stable draft.
#
# The prerelease flag means "automatic and uncurated" here, not "beta". The product being 内测 is
# stated in the notes and in docs/installation.md.
#
# Requires GH_TOKEN, GH_REPO, TAG_NAME, ASSET_PATH, ASSET_NAME, ASSET_SHA256, SIGNING_ENABLED,
# DICTIONARY_TAG, PRODUCT_MANIFEST, RELEASE_TRIGGER and TARGET_SHA.
set -euo pipefail

if [[ "$RELEASE_TRIGGER" == push ]]; then
    release_tag="${TAG_NAME}-beta"
    title="${release_tag}（自动构建）"
    banner='本版本由 CI 在合并到 `main` 后自动构建发布。'

    if release_json=$(gh release view "$release_tag" --repo "$GH_REPO" --json isDraft,isPrerelease,targetCommitish 2>/dev/null); then
        test "$(jq -er .isDraft <<< "$release_json")" = false
        test "$(jq -er .isPrerelease <<< "$release_json")" = true
        test "$(jq -er .targetCommitish <<< "$release_json")" = "$TARGET_SHA"
    else
        gh release create "$release_tag" \
            --repo "$GH_REPO" \
            --target "$TARGET_SHA" \
            --title "$title" \
            --notes "$banner" \
            --prerelease
    fi

    channel=(--prerelease)
else
    release_tag="$TAG_NAME"
    title="$TAG_NAME"
    banner=''
    channel=(--prerelease=false --latest)
fi

gh release upload "$release_tag" "$ASSET_PATH" "$PRODUCT_MANIFEST" --repo "$GH_REPO" --clobber

gh release view "$release_tag" --repo "$GH_REPO" --json body --jq .body > notes.md
{
    if [[ -n "$banner" ]]; then
        printf '\n> %s\n' "$banner"
    fi
    printf '\n---\n\n'
    printf '| Field | Value |\n| --- | --- |\n'
    printf '| Installer | `%s` |\n' "$ASSET_NAME"
    printf '| SHA256 | `%s` |\n' "$ASSET_SHA256"
    printf '| Dictionaries | `%s` |\n' "$DICTIONARY_TAG"
    printf '| Build inputs | `product-manifest.json` (attached and installed) |\n'
    printf '\n[Code signing policy](https://github.com/%s/blob/develop/docs/code-signing-policy.md)\n' "$GH_REPO"
    if [[ "$SIGNING_ENABLED" != true ]]; then
        printf '\nThis build is **unsigned**. Windows warns on launch, and `uiAccess` does not take effect, so the candidate window cannot float over elevated applications.\n'
    fi
} >> notes.md

gh release edit "$release_tag" --repo "$GH_REPO" --draft=false "${channel[@]}" --title "$title" --notes-file notes.md
echo "Published $release_tag as '$title' (${channel[*]}) with $ASSET_NAME"
