#!/usr/bin/env bash
# Paquet .deb ou .rpm d'un logiciel de MevSuite, fabriqué sur le système visé avec le Qt de ce
# système (Ubuntu 26.04 pour le .deb, Fedora 45 pour le .rpm) :
#
#   packaging/paquet-linux.sh <mevpix|mevcine|mevreg|mevsono> <deb|rpm>
#
# Le paquet va dans dist/ ; packaging/essai-linux.sh l'essaie sur un système neuf. Il faut cmake, ninja, le compilateur, les paquets de développement de Qt (et de Python pour
# MeVReg), et dpkg-dev (.deb) ou rpm-build (.rpm).
set -euo pipefail
usage="usage : packaging/paquet-linux.sh <mevpix|mevcine|mevreg|mevsono> <deb|rpm>"
APP=${1:?$usage}
FORMAT=${2:?$usage}
source "$(dirname "$0")/commun.sh"
nom_affiche "$APP" >/dev/null
case $FORMAT in
    deb) GENERATEUR=DEB ;;
    rpm) GENERATEUR=RPM ;;
    *) echo "$usage" >&2; exit 2 ;;
esac

BUILD=$CACHE/$APP-$FORMAT
rm -rf "$BUILD/_CPack_Packages"
cmake -S "$RACINE/$APP" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_TESTING=OFF -DMEVREG_TESTS=OFF
cmake --build "$BUILD"
rm -f "$BUILD"/*."$FORMAT"
(cd "$BUILD" && cpack -G "$GENERATEUR")
PAQUET=$(ls "$BUILD"/*."$FORMAT")
cp "$PAQUET" "$DIST/"
echo "== $(basename "$PAQUET")"

