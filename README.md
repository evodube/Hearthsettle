# Hearthsettle

An SKSE plugin that stops loose clutter (plates, cutlery, food, books...) from flying across the room when a cell loads.

One DLL for Skyrim SE 1.5.97 and AE 1.6.x (Steam and GOG). VR is not supported.

## How it works

When you load into a cell, Havok pushes apart objects that were placed slightly inside tables or each other, and sometimes launches them. Hearthsettle tracks every loose object in each newly loaded cell. For a short window after the load screen closes (2 seconds by default):

- it raises the object's damping, so anything that moves eases into place;
- it caps the object's speed every frame, so nothing can be launched;
- it never touches resting (sleeping) objects, so nothing gets woken up.

When the window ends, objects that have been calm for half a second get their original damping back and behave exactly like vanilla.

Some objects are placed slightly inside a shelf or table and get pushed out every frame; letting go of those on a timer would just launch them later. Hearthsettle keeps holding these "stuck" objects until they calm down, you pick them up, or you leave the cell. While held they stay where they are and can still be bumped gently. Nothing is saved: speed and damping are rebuilt from scratch every time a cell loads.

Objects that need correcting again and again (stuck inside a shelf or wall) get a much tighter speed limit, so they creep out instead of sliding off. Where the game's data allows it, the limits are also enforced inside each physics step, so a launch is stopped before the object moves; the plugin verifies it can read the game's values first and logs whether this is on.

Anything you grab is released immediately.

## Requirements

- SKSE64
- Address Library for SKSE Plugins

## Settings

`Data/SKSE/Plugins/Hearthsettle.ini`. See the comments in the file. Turn on `bDebugLog` to log what gets tracked per cell to `Documents/My Games/Skyrim Special Edition/SKSE/Hearthsettle.log`.

## Building

GitHub Actions builds every push to `main` (the **Hearthsettle** artifact is the ready-to-install mod folder). Pushing a `v*` tag publishes a release.

Locally (Windows, VS 2022 with C++ workload, `VCPKG_ROOT` set):

```
git clone --recursive https://github.com/evodube/Hearthsettle
cmake --preset release
cmake --build --preset release
```

The mod folder is written to `dist/Hearthsettle/`.

CommonLibSSE-NG is a submodule in `extern/CommonLibSSE-NG`, pinned to a release tag so CI can link its prebuilt library.

## License

Hearthsettle is free software under the [GNU General Public License v3.0 or later](LICENSE). It links [CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR), which is GPL-3.0-or-later with a modding exception, so the plugin carries the same license. You may share and modify it as long as you share your source under the same terms.
