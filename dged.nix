{ stdenv
, clang-tools
, bmake
, pkg-config
, tree-sitter
, bear
, lib
, doxygen
}:
stdenv.mkDerivation {
  name = "dged";
  src = ./.;

  doCheck = true;

  nativeBuildInputs = [
    bmake
    pkg-config
    clang-tools
    bear
    doxygen
  ];

  buildPhase = ''
    bmake build
    CFLAGS=-O2 bmake dged
    bmake docs
  '';

  installPhase = ''
    bmake install
  '';

  buildInputs = [
    tree-sitter
  ];
}
