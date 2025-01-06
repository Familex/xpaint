{
  description = "simple paint application written for X";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
  };

  outputs = inputs:
    inputs.flake-parts.lib.mkFlake {inherit inputs;} {
      systems = ["x86_64-linux" "aarch64-linux"];

      perSystem = {
        pkgs,
        self',
        ...
      }: {
        packages = rec {
          default = xpaint;
          xpaint = let
            make = "${pkgs.gnumake}/bin/make";
          in
            pkgs.stdenv.mkDerivation rec {
              name = "xpaint";
              src = pkgs.lib.cleanSource ./.;

              nativeBuildInputs = with pkgs; [
                xorg.libX11
                xorg.libXft
                xorg.libXext
              ];

              buildPhase = "${make} ${name}";
              installPhase = "${make} PREFIX=$out install";

              meta = {
                description = "simple paint application written for X";
                homepage = "https://github.com/Familex/xpaint";
                meta.license = pkgs.lib.licenses.mit;
                mainProgram = "xpaint";
              };
            };
        };

        devShells = {
          default = pkgs.mkShell.override {} {
            inputsFrom = [self'.packages.default];
            packages = with pkgs; [
              util-linux
              gnumake
              clang-tools
              bear
            ];
          };

          debug = pkgs.mkShell.override {} {
            inputsFrom = [self'.packages.default self'.devShells.default];
            packages = with pkgs; [
              cargo-flamegraph
              heaptrack
              valgrind
              gdb
            ];
          };
        };

        formatter = pkgs.alejandra;
      };
    };
}
