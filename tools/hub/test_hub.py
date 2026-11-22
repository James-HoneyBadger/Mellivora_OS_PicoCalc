#!/usr/bin/env python3
"""Tests for the Hub indexer.

Network access is replaced by a fixture table, and every generated index is
also fed to `mpkg_check`, which links the firmware's own JSON and package
parser. That makes the Python emitter and the C consumer agree by
construction rather than by convention.
"""

import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import gallery  # noqa: E402
import indexer  # noqa: E402

DIGEST_A = "a" * 64
DIGEST_B = "b" * 64

RELEASES = {
    "alice/shmup": {
        "tag_name": "v1.2.0",
        "draft": False,
        "prerelease": False,
        "body": "A tiny vertical shooter.\nMore text here.",
        "assets": [
            {"name": "shmup.mapp", "size": 2048,
             "browser_download_url": "https://example.org/alice/shmup.mapp"},
            {"name": "shmup.mapp.sha256", "size": 65,
             "browser_download_url": "https://example.org/alice/shumup.sha256"},
        ],
    },
    "bob/themes": {
        "tag_name": "0.4.1",
        "draft": False,
        "prerelease": False,
        "body": "Two palettes.",
        "assets": [
            {"name": "ocean.thm", "size": 100,
             "browser_download_url": "https://example.org/bob/ocean.thm"},
            {"name": "amber.thm", "size": 100,
             "browser_download_url": "https://example.org/bob/amber.thm"},
            {"name": "notes.txt", "size": 10,
             "browser_download_url": "https://example.org/bob/notes.txt"},
        ],
    },
    "carol/notes": {
        "tag_name": "nightly",
        "draft": False,
        "prerelease": False,
        "body": "No semver tag.",
        "assets": [{"name": "x.mapp", "size": 1,
                    "browser_download_url": "https://example.org/x.mapp"}],
    },
    "dave/empty": {
        "tag_name": "v2.0.0",
        "draft": False,
        "prerelease": False,
        "body": "Ships no artifacts.",
        "assets": [{"name": "README.md", "size": 5,
                    "browser_download_url": "https://example.org/README.md"}],
    },
}

SHA_FILES = {"https://example.org/alice/shumup.sha256": DIGEST_A}

# Artifacts the indexer may need to download-and-hash when a release does
# not publish a .sha256 sidecar.
ASSET_BYTES = {
    "https://example.org/bob/ocean.thm": b"name = ocean\nbg = 000011\n",
    "https://example.org/bob/amber.thm": b"name = amber\nbg = 110000\n",
    "https://example.org/x.mapp": b"nightly",
}


def fake_request(url, token=None):
    del token
    for repo, payload in RELEASES.items():
        if url.endswith("/repos/%s/releases/latest" % repo):
            return payload
    raise indexer.urllib.error.HTTPError(url, 404, "not found", None, None)


def fake_text(url, token=None):
    del token
    if url in SHA_FILES:
        return SHA_FILES[url] + "  shmup.mapp\n"
    raise indexer.urllib.error.HTTPError(url, 404, "not found", None, None)


def fake_bytes(url, token=None):
    del token
    if url in ASSET_BYTES:
        return ASSET_BYTES[url]
    raise indexer.urllib.error.HTTPError(url, 404, "not found", None, None)


def patch_network():
    indexer._request = fake_request
    indexer._request_text = fake_text
    indexer._request_bytes = fake_bytes


def build_mpkg_check(workdir):
    """Compile the C cross-check against the firmware's parser sources."""
    binary = os.path.join(workdir, "mpkg_check")
    sources = [
        os.path.join(HERE, "mpkg_check.c"),
        os.path.join(ROOT, "picocalc", "src", "core", "json.c"),
        os.path.join(ROOT, "picocalc", "src", "core", "mpkg.c"),
        os.path.join(ROOT, "picocalc", "src", "core", "sha256.c"),
    ]
    command = [os.environ.get("CC", "cc"), "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
               "-I", os.path.join(ROOT, "picocalc", "src"), "-o", binary] + sources
    subprocess.run(command, check=True)
    return binary


def checks():
    patch_network()

    # 1. Tags are normalised to a plain dotted version, or rejected.
    assert indexer._version_from_tag("v1.2.0") == "1.2.0"
    assert indexer._version_from_tag("0.4.1") == "0.4.1"
    assert indexer._version_from_tag("release-3.0.0") == "3.0.0"
    assert indexer._version_from_tag("nightly") is None
    assert indexer._version_from_tag(None) is None

    # 2. Asset extensions map to the three kinds mpkg installs.
    assert indexer.KIND_BY_EXT[".mapp"] == "mapp"
    assert indexer.KIND_BY_EXT[".thm"] == "theme"
    assert indexer.KIND_BY_EXT[".sh"] == "script"
    assert ".txt" not in indexer.KIND_BY_EXT

    # 3. One .mapp release becomes one package, with the published digest
    #    and the first line of the release body as its description.
    entries = indexer.packages_for_repo("alice/shmup")
    assert len(entries) == 1, entries
    entry = entries[0]
    assert entry["name"] == "shmup"
    assert entry["version"] == "1.2.0"
    assert entry["kind"] == "mapp"
    assert entry["file"] == "shmup.mapp"
    assert entry["sha256"] == DIGEST_A
    assert entry["size"] == 2048
    assert entry["description"] == "A tiny vertical shooter."

    # 4. Several artifacts of one kind get distinct names; non-artifacts are
    #    dropped.
    entries = indexer.packages_for_repo("bob/themes")
    assert len(entries) == 2, entries
    assert {e["name"] for e in entries} == {"themes-ocean", "themes-amber"}
    assert all(e["kind"] == "theme" for e in entries)
    # No sidecar was published, so the artifact itself was hashed: the
    # firmware rejects an entry without a digest, so this is mandatory.
    import hashlib
    for entry in entries:
        assert entry["sha256"] == hashlib.sha256(ASSET_BYTES[entry["url"]]).hexdigest()

    # 5. A release without a semver tag contributes nothing.
    assert indexer.packages_for_repo("carol/notes") == []

    # 6. A release with no installable asset contributes nothing.
    assert indexer.packages_for_repo("dave/empty") == []

    # 7. An unknown repository is skipped rather than failing the run.
    assert indexer.packages_for_repo("nobody/nothing") == []

    # 8. The assembled index is sorted, schema 1, and skips quietly.
    index = indexer.build_index(list(RELEASES))
    assert index["schema"] == 1
    names = [entry["name"] for entry in index["packages"]]
    assert names == sorted(names), names
    assert len(names) == 3, names
    assert indexer.validate(index) == []

    # 9. validate() catches the mistakes an author actually makes, one
    #    problem per entry so the count is meaningful.
    good_sha = "c" * 64
    bad = {
        "schema": 2,
        "packages": [
            {"name": "kind", "version": "1.0.0", "kind": "firmware",
             "file": "a.mapp", "url": "https://x/a.mapp", "sha256": good_sha},
            {"name": "path", "version": "1.0.0", "kind": "mapp",
             "file": "../b.mapp", "url": "https://x/b.mapp", "sha256": good_sha},
            {"name": "plain", "version": "1.0.0", "kind": "mapp",
             "file": "c.mapp", "url": "http://x/c.mapp", "sha256": good_sha},
            {"name": "nothex", "version": "1.0.0", "kind": "mapp",
             "file": "d.mapp", "url": "https://x/d.mapp", "sha256": "nothex"},
            {"name": "unsigned", "version": "1.0.0", "kind": "mapp",
             "file": "e.mapp", "url": "https://x/e.mapp"},
            {"name": "n" * 40, "version": "1.0.0", "kind": "mapp",
             "file": "f.mapp", "url": "https://x/f.mapp", "sha256": good_sha},
            {"name": "dup", "version": "1.0.0", "kind": "mapp",
             "file": "g.mapp", "url": "https://x/g.mapp", "sha256": good_sha},
            {"name": "dup", "version": "1.0.0", "kind": "mapp",
             "file": "g.mapp", "url": "https://x/g.mapp", "sha256": good_sha},
        ],
    }
    problems = indexer.validate(bad)
    assert len(problems) == 8, problems
    assert any("schema" in p for p in problems)
    assert any("bad kind" in p for p in problems)
    assert any("bare name" in p for p in problems)
    assert any("HTTPS" in p for p in problems)
    assert any("64 hex" in p for p in problems)
    assert any("sha256 is required" in p for p in problems)
    assert any("characters (max 31)" in p for p in problems)
    assert any("duplicate" in p for p in problems)

    # 10. The command line writes the file and reports the counts.
    with tempfile.TemporaryDirectory() as workdir:
        out = os.path.join(workdir, "index.json")
        rc = indexer.main(["--out", out, "--repo", "alice/shmup", "--repo", "bob/themes"])
        assert rc == 0
        with open(out, encoding="utf-8") as handle:
            written = json.load(handle)
        assert len(written["packages"]) == 3

        # 11. The firmware's own parser accepts what the indexer emitted.
        binary = build_mpkg_check(workdir)
        result = subprocess.run([binary, out], capture_output=True, text=True)
        assert result.returncode == 0, result.stdout + result.stderr
        assert "ok: 3 package(s)" in result.stdout
        assert "/apps/shmup.mapp" in result.stdout
        assert "/themes/ocean.thm" in result.stdout
        # Every entry now carries a digest, so all three report as signed.
        assert result.stdout.count("signed") == 3, result.stdout

        # 12. --allow-empty lets a brand new hub publish an empty index,
        #     while the default refuses so CI notices a broken release.
        empty = os.path.join(workdir, "empty.json")
        try:
            indexer.main(["--out", empty, "--repo", "dave/empty"])
            raise AssertionError("empty index was accepted without --allow-empty")
        except SystemExit as exit_error:
            assert exit_error.code not in (0, None)

    # 13. The gallery renders from the same index, escapes user text, and
    #     distinguishes verified from unverified entries.
    index = indexer.build_index(["alice/shmup", "bob/themes"])
    page = gallery.render(index)
    assert "shmup" in page and "themes-ocean" in page
    assert "mpkg install shmup" in page
    assert page.count("verified") >= 3

    hostile = {"schema": 1, "name": "<b>x</b>", "packages": [
        {"name": "evil", "version": "1.0.0", "kind": "mapp", "file": "evil.mapp",
         "url": "https://x/evil.mapp", "sha256": "a" * 64, "size": 1,
         "description": "<script>alert(1)</script>"},
    ]}
    escaped = gallery.render(hostile)
    assert "<script>" not in escaped, "gallery must escape package text"
    assert "&lt;script&gt;" in escaped
    assert "&lt;b&gt;x&lt;/b&gt;" in escaped

    empty_page = gallery.render({"schema": 1, "packages": []})
    assert "No packages yet" in empty_page

    print("hub tests: 13 scenarios passed")


if __name__ == "__main__":
    checks()
