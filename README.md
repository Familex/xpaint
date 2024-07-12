<h1 align='center'>
  xpaint
</h1>

<p align='center'>
  simple paint application written for X
</p>

## Documentation

For detailed program description and usage see [man 1 xpaint](./xpaint.1).

## Requirements

- X11 headers (libX11-devel on fedora, libx11-dev on alpine)
- Xft headers (libXft-devel on fedora, libxft-dev on alpine)
- X11 extentions headers (libXext-devel on fedora, libxext-dev on alpine)

Execute `nix-shell --pure` in project root to enter the shell with
installed dependencies.

## Build

In order to build [xpaint](./xpaint),
execute `make all` command in the project root.

Execute `make help` command to see list of all available targets.

## Configure

Change [config.h](./config.h) file to configure application.

## Install

Edit [config.mk](./config.mk) to match your local setup
(app is installed into the `/usr/local` namespace by default).

Afterwards execute `make clean install` to build and install app
(if necessary as root):
