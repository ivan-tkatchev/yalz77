{
  pkgs ? import (fetchTarball "https://github.com/NixOS/nixpkgs/archive/22.05.tar.gz") {}
} :
with pkgs;
let
  yalz77 = callPackage ./default.nix {};
in
mkShell {
  buildInputs = [ glibcLocales yalz77 ];
  LOCALE_ARCHIVE_2_27 = "${glibcLocales}/lib/locale/locale-archive";
  passthru = { inherit yalz77; };
}


