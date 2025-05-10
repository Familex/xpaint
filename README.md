<h1 align='center'>
  xpaint
</h1>

<p align='center'>
  simple paint application written for X
</p>

## Documentation

For detailed program description and usage see [xpaint(1)](./xpaint.1).

## Requirements

- X11 headers (libX11-devel on fedora, libx11-dev on alpine)
- Xft headers (libXft-devel on fedora, libxft-dev on alpine)
- X11 extentions headers (libXext-devel on fedora, libxext-dev on alpine)

## Build

In order to build [xpaint](./xpaint),
execute `make xpaint` command in the project root.

Execute `make help` command to see list of all available targets.

## Configure

Create and modify [config.h](./config.h) file to configure the application (recompilation is required).
See [config.def.h](./config.def.h) for default configuration.

## Install

Edit [Makefile](./Makefile) to match your local setup
(app is installed into the `/usr/local` namespace by default).

Afterwards execute `make clean install` to build and install app
(if necessary as root).

## Nix

This repository is a flake.

Run it using `nix run` in project root or build with `nix build`.

Execute `nix develop` in project root to enter the shell with
installed dependencies and development tools.

