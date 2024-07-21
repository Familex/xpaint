{
  description = "simple paint application written for X";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    systems.url = "github:nix-systems/default-linux";
  };

  outputs = {
    self,
    nixpkgs,
    systems,
  }: let
    eachSys = nixpkgs.lib.genAttrs (import systems);
  in {
    packages = eachSys (sys: rec {
      default = xpaint;
      xpaint = let
        pkgs = nixpkgs.legacyPackages.${sys};
        make = "${pkgs.gnumake}/bin/make";
      in
        pkgs.stdenv.mkDerivation rec {
          name = "xpaint";
          src = pkgs.lib.cleanSource ./.;

          nativeBuildInputs = with nixpkgs.legacyPackages.${sys}; [
            xorg.libX11
            xorg.libXft
            xorg.libXext
          ];

          buildPhase = "${make} ${name}";
          installPhase = "${make} PREFIX=$out install";
        };
    });

    devShells = eachSys (sys: {
      default = nixpkgs.legacyPackages.${sys}.mkShell.override {} {
        inputsFrom = [self.packages.${sys}.default];
        packages = with nixpkgs.legacyPackages.${sys}; [
          util-linux
          gnumake
          clang-tools
          bear
        ];
      };
    });

    formatter = eachSys (sys: nixpkgs.legacyPackages.${sys}.alejandra);
  };
}
