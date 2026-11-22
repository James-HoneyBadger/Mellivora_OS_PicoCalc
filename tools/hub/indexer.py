#!/usr/bin/env python3
"""Build an mpkg repository index from GitHub repositories.

Any repository carrying the `mellivora-app` topic can be indexed. For each
release asset ending in `.mapp`, `.thm`, or `.sh` the indexer records the
download URL, size, and (when the release publishes it) the SHA-256 digest
taken from a sibling `<asset>.sha256` file. Nothing is uploaded; the index
is a static JSON file that `mpkg` already knows how to read.

    python3 tools/hub/indexer.py --out build/index.json
    python3 tools/hub/indexer.py --repo alice/shmup --repo bob/themes

With no --repo, the indexer queries GitHub for the `mellivora-app` topic.
Set GITHUB_TOKEN to raise the API rate limit in CI.

The output schema matches docs/COMMANDS.md exactly, so a generated index can
be dropped straight onto an SD card at /apps/repo.json.
"""

import argparse
import hashlib
import json
import os
import re
import sys
import urllib.error
import urllib.request

GITHUB_API = "https://api.github.com"

# Mirror of core/mpkg.h. An entry outside these limits is rejected on-device
# without explanation, so they are enforced here instead.
LIMIT_NAME = 32
LIMIT_VERSION = 24
LIMIT_FILE = 48
LIMIT_URL = 192
LIMIT_DESC = 64
TOPIC = "mellivora-app"
KIND_BY_EXT = {".mapp": "mapp", ".thm": "theme", ".sh": "script", ".bas": "script",
               ".fth": "script", ".c": "script"}
# Real tags vary: v1.2.3, 1.2.3, release-1.2.3. Find the first dotted triple
# that is not part of a longer number.
SEMVER = re.compile(r"(?<![0-9])v?(\d+)\.(\d+)\.(\d+)(?![0-9])")


def _request(url, token=None):
    headers = {"Accept": "application/vnd.github+json", "User-Agent": "mellivora-hub-indexer"}
    if token:
        headers["Authorization"] = "Bearer %s" % token
    request = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def _request_text(url, token=None):
    headers = {"User-Agent": "mellivora-hub-indexer"}
    if token:
        headers["Authorization"] = "Bearer %s" % token
    request = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(request, timeout=30) as response:
        return response.read().decode("utf-8", "replace")


def _request_bytes(url, token=None):
    headers = {"User-Agent": "mellivora-hub-indexer"}
    if token:
        headers["Authorization"] = "Bearer %s" % token
    request = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.read()


def discover_topic_repos(token=None):
    """Repositories tagged with the mellivora-app topic, most starred first."""
    repos = []
    page = 1
    while True:
        url = "%s/search/repositories?q=topic:%s&sort=stars&order=desc&per_page=50&page=%d" % (
            GITHUB_API, TOPIC, page)
        try:
            payload = _request(url, token)
        except urllib.error.HTTPError as error:
            if error.code in (403, 422):
                print("indexer: topic search unavailable (%d); use --repo" % error.code,
                      file=sys.stderr)
                return repos
            raise
        items = payload.get("items", [])
        repos.extend(item["full_name"] for item in items)
        if len(items) < 50:
            return repos
        page += 1


def _version_from_tag(tag):
    match = SEMVER.search(tag or "")
    if not match:
        return None
    return "%s.%s.%s" % match.groups()


def _package_name(repo):
    return repo.split("/", 1)[-1].lower().replace("_", "-").replace(" ", "-")


def _sha256_for(asset, assets, token, download=True):
    """Digest for one release asset.

    A published `<asset>.sha256` sibling wins, because the author signed off
    on it. Otherwise the artifact itself is downloaded and hashed: the
    firmware index parser rejects an entry without a digest, so an unsigned
    artifact would simply vanish from `mpkg list` if we skipped it.
    """
    asset_name = asset.get("name", "")
    for sibling in assets:
        if sibling.get("name") == asset_name + ".sha256":
            try:
                text = _request_text(sibling["browser_download_url"], token)
            except (urllib.error.URLError, urllib.error.HTTPError):
                break
            digest = text.strip().split()[0] if text.strip() else ""
            if re.fullmatch(r"[0-9a-fA-F]{64}", digest):
                return digest.lower()
            break

    if not download:
        return None
    try:
        payload = _request_bytes(asset["browser_download_url"], token)
    except (urllib.error.URLError, urllib.error.HTTPError):
        return None
    return hashlib.sha256(payload).hexdigest()


def _fit(text, limit):
    """Trim a string to the firmware's field width (leaving room for NUL)."""
    text = (text or "").replace("\n", " ").strip()
    return text[: limit - 1]


def packages_for_repo(repo, token=None, include_prerelease=False, download=True):
    """Turn one repository's latest release into mpkg package entries."""
    try:
        release = _request("%s/repos/%s/releases/latest" % (GITHUB_API, repo), token)
    except urllib.error.HTTPError as error:
        if error.code == 404:
            return []
        raise
    if not release or release.get("draft"):
        return []
    if release.get("prerelease") and not include_prerelease:
        return []

    version = _version_from_tag(release.get("tag_name"))
    if not version:
        return []

    assets = release.get("assets", [])
    name = _fit(_package_name(repo), LIMIT_NAME)
    body = (release.get("body") or "").strip().splitlines()
    description = _fit(body[0] if body else repo, LIMIT_DESC)

    # Only installable assets count towards naming: a release that ships one
    # artifact plus its .sha256 sibling keeps the plain package name.
    installable = [
        asset for asset in assets
        if os.path.splitext(asset.get("name", ""))[1].lower() in KIND_BY_EXT
    ]

    entries = []
    for asset in installable:
        asset_name = asset.get("name", "")
        kind = KIND_BY_EXT[os.path.splitext(asset_name)[1].lower()]
        entry_name = name if len(installable) == 1 else _fit(
            "%s-%s" % (name, os.path.splitext(asset_name)[0].lower()), LIMIT_NAME)
        digest = _sha256_for(asset, assets, token, download)
        if not digest:
            # The on-device parser requires a digest, so an entry without one
            # would be silently dropped from the package list.
            print("indexer: no digest for %s/%s; skipping" % (repo, asset_name),
                  file=sys.stderr)
            continue
        entries.append({
            "name": entry_name,
            "version": _fit(version, LIMIT_VERSION),
            "kind": kind,
            "file": _fit(asset_name, LIMIT_FILE),
            "url": _fit(asset["browser_download_url"], LIMIT_URL),
            "sha256": digest,
            "size": asset.get("size", 0),
            "description": description,
        })
    return entries


def build_index(repos, token=None, include_prerelease=False, title=None, download=True):
    packages = []
    skipped = []
    for repo in repos:
        try:
            entries = packages_for_repo(repo, token, include_prerelease, download)
        except (urllib.error.URLError, urllib.error.HTTPError, KeyError) as error:
            skipped.append("%s (%s)" % (repo, error))
            continue
        if not entries:
            skipped.append("%s (no release with a recognisable artifact)" % repo)
            continue
        packages.extend(entries)

    packages.sort(key=lambda entry: (entry["name"], entry["version"]))
    for message in skipped:
        print("indexer: skipped %s" % message, file=sys.stderr)

    return {
        "schema": 1,
        "name": title or "Mellivora community repository",
        "packages": packages,
    }


def validate(index):
    """Reject an index that mpkg would refuse, before it reaches the card.

    Every rule mirrors a check in core/mpkg.c: an entry that fails one is
    dropped on the device with no message, which looks like a missing
    package rather than a broken index.
    """
    problems = []
    if index.get("schema") != 1:
        problems.append("schema must be 1")
    seen = set()
    for entry in index.get("packages", []):
        label = entry.get("name", "<unnamed>")
        if entry.get("kind") not in ("mapp", "theme", "script"):
            problems.append("%s: bad kind %r" % (label, entry.get("kind")))
        if "/" in entry.get("file", "") or ".." in entry.get("file", ""):
            problems.append("%s: file must be a bare name" % label)
        if not entry.get("url", "").startswith("https://"):
            problems.append("%s: url must be HTTPS" % label)

        # A missing digest is fatal on-device, not merely suspicious.
        digest = entry.get("sha256")
        if not digest:
            problems.append("%s: sha256 is required" % label)
        elif not re.fullmatch(r"[0-9a-fA-F]{64}", digest):
            problems.append("%s: sha256 is not 64 hex characters" % label)

        for field, limit in (("name", LIMIT_NAME), ("version", LIMIT_VERSION),
                             ("file", LIMIT_FILE), ("url", LIMIT_URL),
                             ("description", LIMIT_DESC)):
            value = entry.get(field, "")
            if len(value) >= limit:
                problems.append("%s: %s is %d characters (max %d)"
                                % (label, field, len(value), limit - 1))

        key = (entry.get("name"), entry.get("version"))
        if key in seen:
            problems.append("%s %s: duplicate entry" % key)
        seen.add(key)
    return problems


def main(argv=None):
    parser = argparse.ArgumentParser(prog="indexer", description=__doc__)
    parser.add_argument("--out", default="build/index.json")
    parser.add_argument("--repo", action="append", default=[],
                        help="owner/name to index (repeatable)")
    parser.add_argument("--title", help="repository title written to the index")
    parser.add_argument("--prerelease", action="store_true",
                        help="include prerelease tags")
    parser.add_argument("--allow-empty", action="store_true",
                        help="succeed even when no packages were found")
    parser.add_argument("--no-download", action="store_true",
                        help="trust only published .sha256 sidecars (skips "
                             "artifacts without one instead of hashing them)")
    args = parser.parse_args(argv)

    token = os.environ.get("GITHUB_TOKEN")
    repos = args.repo or discover_topic_repos(token)
    if not repos:
        sys.exit("indexer: no repositories to index")

    index = build_index(repos, token, args.prerelease, args.title,
                        download=not args.no_download)
    problems = validate(index)
    if problems:
        for problem in problems:
            print("indexer: %s" % problem, file=sys.stderr)
        return 1

    if not index["packages"]:
        message = "indexer: no packages found in %d repositories" % len(repos)
        if not args.allow_empty:
            sys.exit(message)
        print(message, file=sys.stderr)

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(index, handle, indent=2, sort_keys=False)
        handle.write("\n")
    print("indexer: %d package(s) from %d repositories -> %s"
          % (len(index["packages"]), len(repos), args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
