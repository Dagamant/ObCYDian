#!/usr/bin/env python3
"""Assembles the web flasher site.

Fetches the merged firmware image of a GitHub release (or uses a local one), splits it
into the parts ESP Web Tools writes, and fills in the manifest and the page.

  flasher/build.py --out site                        # latest release
  flasher/build.py --out site --version v0.1.0
  flasher/build.py --out site --full dist/ObCYDian-v0.1.0-full.bin --version v0.1.0

The firmware is flashed as separate parts rather than one image from 0x0 so that the NVS
partition (0x9000-0xE000: WiFi settings, touch calibration) is left alone on an update.
ESP Web Tools' "Erase device" option still wipes everything for a clean install.
"""
import argparse
import hashlib
import json
import os
import shutil
import urllib.request

REPO = "Dagamant/ObCYDian"
HERE = os.path.dirname(os.path.abspath(__file__))

# Layout of the merged image (huge_app.csv partition table)
PARTS = [
    ("bootloader.bin", 0x1000, 0x8000),
    ("partitions.bin", 0x8000, 0x9000),
    ("boot_app0.bin", 0xE000, 0x10000),
    ("firmware.bin", 0x10000, None),
]



def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": "obcydian-flasher-build"})
    token = os.environ.get("GITHUB_TOKEN")
    if token and "api.github.com" in url:
        req.add_header("Authorization", "Bearer " + token)
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def release(version):
    path = "latest" if version in ("", "latest") else "tags/" + version
    rel = json.loads(get(f"https://api.github.com/repos/{REPO}/releases/{path}"))
    asset = next((a for a in rel["assets"] if a["name"].endswith("-full.bin")), None)
    if not asset:
        raise SystemExit(f"release {rel['tag_name']} has no *-full.bin asset")
    print(f"downloading {asset['name']} from {rel['tag_name']}")
    return rel["tag_name"], rel["published_at"][:10], rel["html_url"], get(asset["browser_download_url"])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--version", default="latest", help="release tag, or 'latest'")
    ap.add_argument("--full", help="use this local merged image instead of downloading")
    a = ap.parse_args()

    if a.full:
        tag = a.version if a.version != "latest" else "dev"
        date, url = "", f"https://github.com/{REPO}/releases"
        image = open(a.full, "rb").read()
    else:
        tag, date, url, image = release(a.version)

    # Sanity checks: ESP image magic at the bootloader and app, partition table magic
    if image[0x1000] != 0xE9 or image[0x10000] != 0xE9 or image[0x8000:0x8002] != b"\xaa\x50":
        raise SystemExit("not a merged ObCYDian image (unexpected layout)")

    out = a.out
    if os.path.exists(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, "firmware"))
    os.makedirs(os.path.join(out, "img"))

    parts = []
    for name, start, end in PARTS:
        data = image[start:end]
        with open(os.path.join(out, "firmware", name), "wb") as f:
            f.write(data)
        parts.append({"path": "firmware/" + name, "offset": start})
    version = tag.lstrip("v")
    manifest = {
        "name": "ObCYDian",
        "version": version,
        "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 0,
        "builds": [{"chipFamily": "ESP32", "parts": parts}],
    }
    with open(os.path.join(out, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
    with open(os.path.join(out, "firmware", "SHA256SUMS.txt"), "w") as f:
        for name, _, _ in PARTS:
            h = hashlib.sha256(open(os.path.join(out, "firmware", name), "rb").read()).hexdigest()
            f.write(f"{h}  {name}\n")

    page = open(os.path.join(HERE, "index.html"), encoding="utf-8").read()
    page = (page.replace("{{VERSION}}", version)
                .replace("{{TAG}}", tag)
                .replace("{{DATE}}", date or "local build")
                .replace("{{RELEASE_URL}}", url)
                .replace("{{SIZE}}", f"{len(image) / 1048576:.1f} MB"))
    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as f:
        f.write(page)
    for name in os.listdir(os.path.join(HERE, "img")):
        shutil.copy(os.path.join(HERE, "img", name), os.path.join(out, "img", name))
    shutil.copy(os.path.join(HERE, "favicon.svg"), os.path.join(out, "favicon.svg"))
    print(f"site for {tag} written to {out}")


if __name__ == "__main__":
    main()
