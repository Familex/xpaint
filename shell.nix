{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    gnumake util-linux
    xorg.libX11 xorg.libXext xorg.libXft
    clang-tools bear
  ];
}
