#!/usr/bin/env bash
# Essaie un paquet .deb, .rpm ou une AppImage sur un système neuf (conteneur, en root) :
#
#   packaging/essai-linux.sh <mevpix|mevcine|mevreg|mevsono> <fichier>
#
# Le paquet est installé avec apt ou dnf (ses dépendances viennent du système), puis le programme
# est vérifié : bibliothèques trouvées, lanceur et icône installés, Python intégré de MeVReg en état
# de marche, démarrage sans écran (plateforme « offscreen »). Une AppImage est lancée telle quelle,
# avec les seules bibliothèques d'un bureau (OpenGL, X11, Wayland, polices, D-Bus) installées à côté.
set -euo pipefail
usage="usage : packaging/essai-linux.sh <mevpix|mevcine|mevreg|mevsono> <fichier>"
APP=${1:?$usage}
FICHIER=$(realpath "${2:?$usage}")

case $FICHIER in
    *.deb)
        apt-get update -qq
        DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "$FICHIER"
        PROGRAMME=/usr/bin/$APP ;;
    *.rpm)
        dnf install -y -q "$FICHIER"
        PROGRAMME=/usr/bin/$APP ;;
    *.AppImage)
        # bibliothèques de tout bureau, que l'AppImage ne contient pas (OpenGL, X11, Wayland, polices, D-Bus)
        if command -v apt-get >/dev/null; then
            apt-get update -qq
            DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends libgl1 libegl1 \
                libxcb1 libx11-6 libx11-xcb1 libwayland-client0 libfreetype6 libfontconfig1 libexpat1 libdbus-1-3 >/dev/null
        elif command -v dnf >/dev/null; then
            dnf install -y -q mesa-libGL mesa-libEGL libxcb libX11 libX11-xcb libwayland-client freetype dbus-libs \
                fontconfig expat >/dev/null
        fi
        chmod +x "$FICHIER"
        # pas de FUSE dans un conteneur : l'AppImage est extraite à chaque lancement
        export APPIMAGE_EXTRACT_AND_RUN=1
        PROGRAMME=$FICHIER ;;
    *) echo "$usage" >&2; exit 2 ;;
esac

if [ "$PROGRAMME" = "/usr/bin/$APP" ]; then
    test -f "/usr/share/applications/$APP.desktop"
    test -f "/usr/share/icons/hicolor/256x256/apps/$APP.png"
    if ldd "$PROGRAMME" | grep "not found"; then
        echo "bibliothèques manquantes" >&2
        exit 1
    fi
fi
if [ "$APP" = mevreg ]; then
    "$PROGRAMME" --selftest
fi
# le programme démarre sans écran et tient trois secondes
statut=0
QT_QPA_PLATFORM=offscreen timeout 3 "$PROGRAMME" || statut=$?
if [ "$statut" != 124 ]; then
    echo "$APP s'est arrêté au démarrage (code $statut)" >&2
    exit 1
fi
echo "== $(basename "$FICHIER") : essai réussi"
