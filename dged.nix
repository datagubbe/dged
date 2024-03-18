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
    linkFarm "tree-sitter-grammars" rec {
      "bash" = tree-sitter-bash;
      "c" = tree-sitter-c;
      "cpp" = tree-sitter-cpp.overrideAttrs (_: {
        # TODO: better, this works kinda ok but maybe should be more flexible
        postInstall = ''
          echo "" >> "$out"/queries/highlights.scm
          echo ";; Inserted from C" >> "$out"/queries/highlights.scm
          cat "${tree-sitter-c}"/queries/highlights.scm >> "$out"/queries/highlights.scm
        '';
      });
      "rust" = tree-sitter-rust;
      "nix" = tree-sitter-nix;
      "python" = tree-sitter-python;
      "make" = tree-sitter-make;
      "markdown" = tree-sitter-markdown;
      "javascript" = tree-sitter.buildGrammar {
        language = "javascript";
        version = "0.20.4";
        src = fetchFromGitHub {
          owner = "tree-sitter";
          repo = "tree-sitter-javascript";
          rev = "v0.20.4";
          hash = "sha256-HhqYqU1CwPxXMHp21unRekFDzpGVedlgh/4bsplhe9c=";
        };
      };
      "typescript" = tree-sitter.buildGrammar {
        language = "typescript";
        version = "0.20.6";
        location = "typescript";
         src = fetchFromGitHub {
          owner = "tree-sitter";
          repo = "tree-sitter-typescript";
          rev = "v0.20.6";
          hash = "sha256-uGuwE1eTVEkuosMfTeY2akHB+bJ5npWEwUv+23nhY9M=";
        };

        postInstall = ''
          cd ..
          cp -r queries $out
        '';
      };
      "qmljs" = tree-sitter.buildGrammar {
        language = "qmljs";
        version = "0.1.2";
        src = fetchFromGitHub {
          owner = "yuja";
          repo = "tree-sitter-qmljs";
          rev = "9fa49ff3315987f715ce5666ff979a7742fa8a98";
          hash = "sha256-q20gLVLs0LpqRpgo/qNRDfExbWXhICWZjM1ux4+AT6M=";
        };

        # remove and fix broken symlinks
        postInstall = ''
          unlink "$out/queries/highlights-javascript.scm"
          unlink "$out/queries/highlights-typescript.scm"

          echo "" >> "$out"/queries/highlights.scm
          echo ";; Inserted from javascript" >> "$out"/queries/highlights.scm
          cat "${javascript}"/queries/highlights.scm >> "$out"/queries/highlights.scm

          echo "" >> "$out"/queries/highlights.scm
          echo ";; Inserted from typescript" >> "$out"/queries/highlights.scm
          cat "${typescript}"/queries/highlights.scm >> "$out"/queries/highlights.scm
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
