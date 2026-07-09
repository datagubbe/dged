{
  description = "An editor for datagubbar";

  inputs.nixpkgs.url = "nixpkgs/nixos-26.05";
  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages."${system}";
      in
      {
        packages = rec {
          grammars = pkgs.callPackage ./grammar.nix { };
          default = pkgs.callPackage ./dged.nix { grammarBundle = grammars; };
          gcc = default;
          clang = pkgs.callPackage ./dged.nix { stdenv = pkgs.clangStdenv; grammarBundle = grammars; };
          windows = pkgs.pkgsCross.mingwW64.callPackage ./dged.nix { grammarBundle = grammars; };
        };
      }
    );
}
