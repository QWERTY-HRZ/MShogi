{

  description = "MShogi - A Shogi (Japanese Chess) game";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils/main";
  };

  outputs =
    { nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (
      system:

      let

        pkgs = import nixpkgs { inherit system; };

        build-tools = with pkgs; [
          cmake
          pkg-config
        ];

        libraries = with pkgs; [
          qt6.qtbase
          qt6.qtmultimedia
        ];

      in

      {
        devShells.default = pkgs.mkShell {
          name = "mshogi-shell";

          nativeBuildInputs = build-tools;

          buildInputs = libraries;

        };
      }
    );

}
