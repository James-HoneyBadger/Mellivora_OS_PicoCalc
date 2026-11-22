# An app for Mellivora OS on the PicoCalc

This repository is a ready-to-use Mellivora app: it builds an installable
`.mapp`, runs the same source on your desktop, and publishes a release that
the package manager can find.

## Build and try it

```sh
git clone https://github.com/James-HoneyBadger/Mellivora_OS_PicoCalc.git
make MELLIVORA=Mellivora_OS_PicoCalc sim     # see it run here
make MELLIVORA=Mellivora_OS_PicoCalc         # build/myapp.mapp
```

Copy `build/myapp.mapp` to `/apps/` on the PicoCalc's microSD card and it
appears in the launcher.

## Make it yours

| File | What to change |
| --- | --- |
| `mellivora.json` | App name, title, description, entry source |
| `src/myapp.c` | The app itself - start with `mapp_main` |

The ABI reference, including the graphics and audio calls this template
uses, is [`tools/sdk/README.md`](https://github.com/James-HoneyBadger/Mellivora_OS_PicoCalc/blob/main/tools/sdk/README.md)
in the Mellivora repository.

## Publish

1. Add the `mellivora-app` topic to this repository (Settings → Topics).
2. Tag a release: `git tag v1.0.0 && git push origin v1.0.0`.

The included workflow builds the `.mapp`, renders a screenshot with the
simulator, and attaches both to the release. On the PicoCalc 2 W:

```sh
mpkg update
mpkg list
mpkg install myapp
```

## Notes

- `.mapp` files are native code with the same privileges as the firmware.
  Only install apps you trust; the package manager verifies the SHA-256
  digest from the release published by this workflow.
- Keep the surface at 160×160 or smaller so the app also runs on the
  RP2040 build, which has far less free RAM.
