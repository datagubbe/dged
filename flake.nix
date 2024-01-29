{
  description = "An editor for datagubbar";

  inputs.nixpkgs.url = "nixpkgs/nixos-23.11";
  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages."${system}";
      in
      {
        packages.default = pkgs.callPackage ./dged.nix { };
        packages.clang = pkgs.callPackage ./dged.nix { stdenv = pkgs.clangStdenv; };
      }
    );
}
