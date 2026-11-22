# Mellivora Hub

A package catalogue that runs on GitHub, with no server to operate.

```
indexer.py   discover app repositories, read their releases, emit index.json
gallery.py   render a static HTML catalogue from that index
mpkg_check.c feeds an index to the firmware's own parser (cross-check)
template/    a starter repository that builds, screenshots, and releases
test_hub.py  indexer + gallery tests, including the C cross-check
```

## How publishing works

1. A developer creates a repository from [`template/`](template) and adds
   the **`mellivora-app`** topic.
2. Tagging `v1.0.0` runs that repository's own workflow: it builds
   `NAME.mapp`, writes `NAME.mapp.sha256`, renders a screenshot with the
   simulator, and attaches all three to a GitHub release.
3. Our scheduled workflow discovers the topic, indexes every release asset,
   and publishes `index.json` plus a gallery to Pages.

Nothing is uploaded to us and no account is needed beyond GitHub.

## Installing from a hub

On a Pico 2 W with a card and Wi-Fi configured:

```sh
mpkg repo https://<owner>.github.io/<repo>/index.json
mpkg update
mpkg list
mpkg install shmup
```

Other builds keep the shell-only stub; `mpkg` is compiled out of them
entirely, so the non-Wi-Fi images gain nothing in size from this.

## Building an index locally

```sh
python3 tools/hub/indexer.py --repo alice/shmup --repo bob/themes --out build/index.json
python3 tools/hub/gallery.py --index build/index.json --out build/hub
python3 tools/hub/test_hub.py
```

`GITHUB_TOKEN` raises the API rate limit. `--no-download` trusts only
published `.sha256` sidecars; the default hashes the artifact itself,
because the firmware rejects an index entry that has no digest.

## Why a digest is mandatory

`core/mpkg.c` refuses any package whose `sha256` is missing or malformed, so
an unsigned entry disappears from `mpkg list` with no error message. The
indexer and `validate()` enforce the same rules, and `test_hub.py` proves it
by loading generated output through the firmware's own C parser via
`mpkg_check`.
