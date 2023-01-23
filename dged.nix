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

  buildPhase = ''
    bmake dged
  '';

  installPhase = ''
    bmake install
  '';

  buildInputs = [
    tree-sitter
  ];

  hardeningDisable = [ "fortify" ];
}
