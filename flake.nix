{
  description = "An editor for datagubbar";

  inputs.nixpkgs.url = "nixpkgs/nixos-24.05";
  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages."${system}";
      in
      {
        packages = rec {
          default = pkgs.callPackage ./dged.nix { };
          gcc = default;
          clang = pkgs.callPackage ./dged.nix { stdenv = pkgs.clangStdenv; };
        };
      }
    );
}
