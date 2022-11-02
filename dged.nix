{ stdenv
, clang-tools
, gnumake
, pkg-config
, tree-sitter
, bear
, lib
}:
stdenv.mkDerivation {
  name = "dged";
  src = ./.;

  nativeBuildInputs = [
    gnumake
    pkg-config
    clang-tools
    bear
  ];

  buildInputs = [
    tree-sitter
  ];

  hardeningDisable = [ "fortify" ];
}
