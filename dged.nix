{ stdenv
, clang-tools
, bmake
, pkg-config
, tree-sitter
, bear
, lib
, doxygen
, valgrind
, glibcLocalesUtf8
, gdb
, grammarBundle
}:
stdenv.mkDerivation {
  name = "dged";
  src = ./.;

  doCheck = true;
  separateDebugInfo = true;

  nativeBuildInputs = [
    bmake
    pkg-config
    clang-tools
    bear
    doxygen
    valgrind
    gdb
  ];

  buildInputs = [
    tree-sitter
  ];

  buildPhase = ''
    bmake build
    CFLAGS="-O2" bmake dged
    bmake docs
  '';

  # needed for tests to work in sandboxed builds
  LOCALE_ARCHIVE = if stdenv.isLinux then "${glibcLocalesUtf8}/lib/locale/locale-archive" else null;

  # find the grammars in the shell
  TREESITTER_GRAMMARS = grammarBundle;

  installPhase = ''
    bmake install
    mkdir -p "$out"/share/dged/grammars
    cp -a $TREESITTER_GRAMMARS/. "$out"/share/dged/grammars/.
  '';
}
