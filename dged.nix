{ stdenv
, clang-tools
, bmake
, pkg-config
, tree-sitter
, tree-sitter-grammars
, bear
, lib
, doxygen
, valgrind
, linkFarm
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
    valgrind
  ];

  buildPhase = ''
    bmake build
    CFLAGS=-O2 bmake dged
    bmake docs
  '';

  TREESITTER_GRAMMARS = with tree-sitter-grammars;
    linkFarm "tree-sitter-grammars" {
      "c" = tree-sitter-c;
      "rust" = tree-sitter-rust;
      "nix" = tree-sitter-nix;
      "python" = tree-sitter-python;
      "make" = tree-sitter-make;
    };

  installPhase = ''
    bmake install
  '';

  buildInputs = [
    tree-sitter
  ];
}
