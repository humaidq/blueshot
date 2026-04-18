# Blueshot Editor

Blueshot is a cross-platform port of Greenshot's editor. It brings in the powerful
photo annotation features.

![Blueshot screenshot](./screenshot.png)

Being easy to understand, Blueshot is an efficient tool for project managers,
software developers, technical writers, testers and anyone else creating
screenshots.

Blueshot is made to be used alongside other tools, such as `grim` and `slurp`.
This project currently only builds for Linux using Nix.

This project is **not endorsed by the Greenshot team**, this is not a
fully-fledged port, it only ports the editor feature and is licensed under the
same GPL (v3) license. All Greenshot branding is removed.

**Note:** This port was done with the use of LLMs. Please report bugs.

## Changes from Greenshot

- 2026-04-13: Ported Greenshot's editor to C++ with Qt. Preserving most
  features of the editor only. Builds with Nix, and all Greenshot branding
  removed (as per their requirements).
- 2026-04-14: Updated obfuscate feature to be robust, creating a mask over the
  photo of all obfuscated areas, and re-drawing all obfuscate blocks using the
  closest un-obfuscated pixel for the colour palette.

## Usage

### With Nix

You may import this project as a flake, or run it directly:

```
nix run github:humaidq/blueshot
```

### Development

A Nix devshell is provided. If you have nix-direnv setup on your system, it should load automatically. Otherwise:

```
nix develop
```

### Qt build

The desktop editor in this repository lives in `src/Blueshot.Editor.Qt` and can be built with CMake and Qt 6:

```sh
cmake -S src/Blueshot.Editor.Qt -B build -GNinja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

On Windows, use a matching Qt + compiler toolchain. The GitHub Actions build uses MSYS2 `MINGW64` for x64 and MSVC + Qt for arm64, so local Windows builds should follow the same pairing instead of mixing MinGW with an MSVC Qt installation.

### GitHub Actions

GitHub Actions builds the Qt app on Linux, Windows, and macOS.

- Pushes to `main` and pull requests run the full build matrix and upload artifacts.
- Tags matching `v*` also publish the packaged artifacts to the corresponding GitHub Release.

Release artifacts currently use these formats:

- Linux: `.tar.gz`
- Windows: `.zip` and WiX v4 `.msi`
- macOS: `.zip` containing the `.app` bundle
