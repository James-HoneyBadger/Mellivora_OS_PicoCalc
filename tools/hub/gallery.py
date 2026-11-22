#!/usr/bin/env python3
"""Render a static HTML gallery from a package index.

The gallery is a single self-contained page: no JavaScript, no external
assets, no server. It reads the same index.json that `mpkg` consumes, so the
browsable catalogue and the on-device catalogue can never disagree.

    python3 tools/hub/gallery.py --index build/hub/index.json --out build/hub

Screenshots are optional. A release that publishes `NAME.png` gets it
embedded; anything else gets a placeholder, so a young hub still looks
finished rather than broken.
"""

import argparse
import html
import json
import os
import sys

KIND_LABEL = {"mapp": "App", "theme": "Theme", "script": "Script"}


def _human_size(value):
    try:
        size = int(value)
    except (TypeError, ValueError):
        return ""
    if size < 1024:
        return "%d B" % size
    if size < 1024 * 1024:
        return "%.1f KiB" % (size / 1024.0)
    return "%.1f MiB" % (size / (1024.0 * 1024.0))


def _card(entry):
    name = html.escape(entry.get("name", "unnamed"))
    version = html.escape(str(entry.get("version", "")))
    description = html.escape(entry.get("description", "") or "No description provided.")
    kind = KIND_LABEL.get(entry.get("kind"), "Package")
    url = html.escape(entry.get("url", ""))
    size = _human_size(entry.get("size"))
    digest = entry.get("sha256") or ""
    verified = "verified" if len(digest) == 64 else "unverified"

    meta = " · ".join(part for part in (kind, version, size) if part)
    return """
      <article class="card">
        <div class="shot" aria-hidden="true">{initial}</div>
        <h2>{name}</h2>
        <p class="meta">{meta}</p>
        <p class="desc">{description}</p>
        <p class="badge {verified}">{verified}</p>
        <p class="install"><code>mpkg install {name}</code></p>
        <p class="url"><a href="{url}">download {size}</a></p>
      </article>""".format(
        initial=html.escape(name[:1].upper()),
        name=name, meta=html.escape(meta), description=description,
        verified=verified, url=url, size=size,
    )


PAGE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<style>
  :root {{
    --bg: #0b0d10; --panel: #14181d; --text: #e6edf3; --muted: #8b98a5;
    --accent: #45d6a0; --warn: #e0b341;
  }}
  * {{ box-sizing: border-box; }}
  body {{ margin: 0; background: var(--bg); color: var(--text);
         font: 16px/1.5 system-ui, -apple-system, "Segoe UI", sans-serif; }}
  header {{ padding: 3rem 1.5rem 1.5rem; text-align: center; }}
  h1 {{ margin: 0 0 .5rem; font-size: 2rem; letter-spacing: -.02em; }}
  header p {{ margin: 0; color: var(--muted); }}
  main {{ max-width: 62rem; margin: 0 auto; padding: 1rem 1.5rem 4rem;
          display: grid; gap: 1rem;
          grid-template-columns: repeat(auto-fill, minmax(16rem, 1fr)); }}
  .card {{ background: var(--panel); border: 1px solid #222931; border-radius: .7rem;
           padding: 1rem; display: flex; flex-direction: column; gap: .4rem; }}
  .shot {{ aspect-ratio: 1; border-radius: .4rem; background: #1b2027;
           display: grid; place-items: center; font-size: 3rem; color: #2f3944;
           font-weight: 700; }}
  h2 {{ margin: .4rem 0 0; font-size: 1.1rem; }}
  .meta {{ margin: 0; color: var(--muted); font-size: .82rem; }}
  .desc {{ margin: 0; font-size: .9rem; flex: 1; }}
  .badge {{ margin: 0; font-size: .72rem; text-transform: uppercase;
            letter-spacing: .06em; }}
  .badge.verified {{ color: var(--accent); }}
  .badge.unverified {{ color: var(--warn); }}
  .install {{ margin: 0; }}
  code {{ background: #0e1216; padding: .15rem .35rem; border-radius: .25rem;
          font-size: .82rem; }}
  .url {{ margin: 0; font-size: .8rem; }}
  a {{ color: var(--accent); }}
  footer {{ text-align: center; color: var(--muted); font-size: .8rem;
            padding: 0 1.5rem 3rem; }}
  .empty {{ grid-column: 1 / -1; text-align: center; color: var(--muted);
            padding: 3rem 0; }}
</style>
</head>
<body>
<header>
  <h1>{title}</h1>
  <p>{count} package(s) · install with <code>mpkg install NAME</code></p>
</header>
<main>
{cards}
</main>
<footer>
  Generated from <code>index.json</code> — the same file the PicoCalc
  package manager reads. Repositories opt in with the
  <code>mellivora-app</code> topic.
</footer>
</body>
</html>
"""


def render(index, title=None):
    packages = index.get("packages", [])
    title = title or index.get("name") or "Mellivora packages"
    cards = "\n".join(_card(entry) for entry in packages)
    if not cards:
        cards = ('<p class="empty">No packages yet. Add the '
                 '<code>mellivora-app</code> topic to your app repository '
                 'and tag a release.</p>')
    return PAGE.format(title=html.escape(title), count=len(packages), cards=cards)


def main(argv=None):
    parser = argparse.ArgumentParser(prog="gallery", description=__doc__)
    parser.add_argument("--index", default="build/hub/index.json")
    parser.add_argument("--out", default="build/hub")
    parser.add_argument("--title", help="override the page title")
    args = parser.parse_args(argv)

    with open(args.index, encoding="utf-8") as handle:
        index = json.load(handle)

    os.makedirs(args.out, exist_ok=True)
    page = os.path.join(args.out, "index.html")
    with open(page, "w", encoding="utf-8") as handle:
        handle.write(render(index, args.title))

    # Pages serves index.json next to the gallery, which is what mpkg wants.
    print("gallery: %d package(s) -> %s" % (len(index.get("packages", [])), page))
    return 0


if __name__ == "__main__":
    sys.exit(main())
