{ stdenv
} :
stdenv.mkDerivation {
  name = "yalz77";
  src = ./.;
  buildInputs = [ ];
  buildPhase = "make";
  installPhase = ''
    mkdir -p $out/{include,bin}
    cp lz77.h $out/include
    cp yalz $out/bin
  '';
}

