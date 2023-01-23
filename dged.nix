{ stdenv
, clang-tools
, gnumake
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
    gnumake
    bmake
    pkg-config
    clang-tools
    bear
    doxygen
  ];

  buildPhase = ''
    bmake dged
    bmake docs
  '';

  installPhase = ''
    bmake install
  '';

  buildInputs = [
    tree-sitter
  ];

  hardeningDisable = [ "fortify" ];
}
