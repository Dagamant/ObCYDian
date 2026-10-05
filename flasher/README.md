# ObCYDian web flasher

The install page served at <https://obcydian.dagamant.com>. It uses [ESP Web Tools](https://esphome.github.io/esp-web-tools/) to flash the board from Chrome or Edge over Web Serial.

## Run it

```sh
cd flasher
docker compose up -d --build                           # latest GitHub release
FIRMWARE_VERSION=v0.1.0 docker compose up -d --build   # a specific release
FLASHER_PORT=9000 docker compose up -d --build         # another host port (default 8080)
```

The `flasher` folder is self-contained: you can copy just this folder to a server. The image build downloads the release's `ObCYDian-*-full.bin` from GitHub. After you publish a new release, run `docker compose up -d --build` again and the page picks it up.

**HTTPS is required.** Browsers only allow Web Serial on secure pages (or `localhost`). Put the container behind your TLS reverse proxy, for example Caddy:

```
obcydian.dagamant.com {
    reverse_proxy localhost:8080
}
```

## How it works

`build.py` assembles the static site: it splits the merged release image into the bootloader (`0x1000`), partition table (`0x8000`), OTA data (`0xE000`) and app (`0x10000`), writes `manifest.json`, and fills the version into `index.html`. The installer writes those four parts and skips the NVS partition (`0x9000`). An update therefore keeps the saved WiFi network and touch calibration, unless the user ticks "Erase device".

To try the page without Docker:

```sh
python3 flasher/build.py --out /tmp/flasher-site
python3 -m http.server -d /tmp/flasher-site 8000      # then open http://localhost:8000
```

`--full dist/ObCYDian-vX.Y.Z-full.bin --version vX.Y.Z` builds from a local image instead of downloading it.
