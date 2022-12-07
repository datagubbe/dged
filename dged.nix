{ stdenv
, clang-tools
, gnumake
, bmake
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
    bmake
    pkg-config
    clang-tools
    bear
  ];

  buildInputs = [
    tree-sitter
  ];

  hardeningDisable = [ "fortify" ];
}
