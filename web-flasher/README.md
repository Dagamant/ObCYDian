# web-flasher

A static install page for the [ObCYDian](../) firmware, built on
[ESP Web Tools](https://esphome.github.io/esp-web-tools/) (Web Serial API).
It lets anyone flash the 3.5" ESP32-32E Cheap Yellow Display over USB from
Chrome or Edge, without installing PlatformIO. Below the installer, the page
covers features, screenshots, first-boot setup and troubleshooting. It is
published at <https://obcydian.dagamant.com>.

| File | What it's for |
| --- | --- |
| `index.html` | The flasher page |
| `manifest.json` | Tells ESP Web Tools which binaries to write, and where |
| `firmware/` | Bootloader, partition table, boot_app0 and app binaries |
| `img/` | Screenshots used on the page |

The installer writes the four parts separately and skips the NVS partition.
An update therefore keeps the saved WiFi network and touch calibration,
unless the user ticks "Erase device".

Web Serial (and the manifest fetch) need a real HTTP origin, not `file://`,
so the page has to be served rather than opened directly.

## Serving it

```sh
docker compose up -d
```

This serves the page at `http://localhost:3008` (nginx, see
`docker-compose.yml`). `http://localhost` counts as a secure context, but
other machines need HTTPS. Put the container behind a TLS reverse proxy for
obcydian.dagamant.com.

## Updating the bundled firmware

After any firmware change, refresh the binaries and the version shown on the
page:

```sh
./build.sh            # version from the latest git tag
./build.sh 0.2.0      # or set it explicitly
```

This builds the `cyd35` environment with PlatformIO, copies the resulting
`.bin` files into `firmware/`, and updates the version in `manifest.json` and
`index.html`. Run it before re-publishing the page so the flasher never
serves stale firmware. The running container picks the new files up
immediately, because the folder is mounted, not copied.

## License

GPL-3.0, like the rest of ObCYDian. ObCYDian is not affiliated with Obsidian.
