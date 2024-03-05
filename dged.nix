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
, fetchFromGitHub
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
      "bash" = tree-sitter-bash;
      "c" = tree-sitter-c;
      "rust" = tree-sitter-rust;
      "nix" = tree-sitter-nix;
      "python" = tree-sitter-python;
      "make" = tree-sitter-make;
      "qmljs" = tree-sitter.buildGrammar {
        language = "qmljs";
        version = "0.1.2";
        src = fetchFromGitHub {
          owner = "yuja";
          repo = "tree-sitter-qmljs";
          rev = "master";
          hash = "sha256-q20gLVLs0LpqRpgo/qNRDfExbWXhICWZjM1ux4+AT6M=";
        };
        # remove broken symlinks
        postInstall = ''
          unlink "$out/queries/highlights-javascript.scm"
          unlink "$out/queries/highlights-typescript.scm"
        '';
      };
      "gitcommit" = tree-sitter.buildGrammar {
        language = "gitcommit";
        version = "0.3.3";
        src = fetchFromGitHub {
          owner = "gbprod";
          repo = "tree-sitter-gitcommit";
          rev = "v0.3.3";
          hash = "sha256-L3v+dQZhwC+kBOHf3YVbZjuCU+idbUDByEdUBmeGAlo=";
        };
      };
    };

  installPhase = ''
    bmake install
  '';

  buildInputs = [
    tree-sitter
  ];
}
