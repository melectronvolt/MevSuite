#!/usr/bin/env bash
# AppImage d'un logiciel de MevSuite : un seul fichier, qui se lance sans installation sur les
# distributions Linux récentes (glibc 2.39 ou plus : Ubuntu 24.04, Debian 13, Fedora 40…) :
#
#   packaging/appimage.sh <mevpix|mevcine|mevreg|mevsono>
#
# À fabriquer sur Ubuntu 24.04. Qt (version QT_VERSION, installé par aqtinstall, ou celui de
# QT_ROOT_DIR), ses greffons et les bibliothèques sont copiés dans l'AppImage, avec pour MeVReg la
# bibliothèque standard de Python, et pour MeVCine ffmpeg et ffprobe. Les outils (linuxdeploy,
# appimagetool) sont téléchargés à des versions fixées, et leur empreinte SHA-256 est vérifiée.
# Il faut : cmake, ninja-build, g++-14, python3-dev, python3-venv, curl, file, xz-utils, et les
# bibliothèques dont Qt et ses greffons dépendent (liste dans .github/workflows/release.yml).
set -euo pipefail
usage="usage : packaging/appimage.sh <mevpix|mevcine|mevreg|mevsono>"
APP=${1:?$usage}
source "$(dirname "$0")/commun.sh"
NOM=$(nom_affiche "$APP")
VERSION=$(version_de "$APP")
QT_VERSION=${QT_VERSION:-6.10.3}
AQTINSTALL_VERSION=3.3.0

OUTILS=$CACHE/outils
telecharge https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage \
    "$OUTILS/linuxdeploy-x86_64.AppImage" c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
telecharge https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage \
    "$OUTILS/linuxdeploy-plugin-qt-x86_64.AppImage" 15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724
telecharge https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage \
    "$OUTILS/appimagetool-x86_64.AppImage" ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
telecharge https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64 \
    "$OUTILS/runtime-x86_64" 2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d
chmod +x "$OUTILS"/*.AppImage
export PATH=$OUTILS:$PATH
# sans FUSE (conteneur, machine d'intégration continue), les outils s'extraient à chaque lancement
export APPIMAGE_EXTRACT_AND_RUN=1

# Qt : celui de QT_ROOT_DIR (install-qt-action), sinon installé ici par aqtinstall
QT_DIR=${QT_ROOT_DIR:-}
if [ -z "$QT_DIR" ]; then
    QT_DIR=$CACHE/qt/$QT_VERSION/gcc_64
    if [ ! -x "$QT_DIR/bin/qmake" ]; then
        python3 -m venv "$CACHE/aqt"
        "$CACHE/aqt/bin/pip" install -q "aqtinstall==$AQTINSTALL_VERSION"
        (cd "$CACHE" && "$CACHE/aqt/bin/aqt" install-qt linux desktop "$QT_VERSION" linux_gcc_64 \
            -m qtmultimedia qtimageformats -O "$CACHE/qt")
    fi
fi
echo "== $NOM $VERSION, Qt de $QT_DIR"

# compilation et installation dans l'AppDir
if command -v g++-14 >/dev/null; then
    export CC=gcc-14 CXX=g++-14
fi
BUILD=$CACHE/$APP-appimage
APPDIR=$BUILD/AppDir
rm -rf "$APPDIR"
cmake -S "$RACINE/$APP" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_PREFIX_PATH="$QT_DIR" -DBUILD_TESTING=OFF -DMEVREG_TESTS=OFF
cmake --build "$BUILD"
DESTDIR=$APPDIR cmake --install "$BUILD" --strip

AUTRES=()
case $APP in
    mevreg)
        # bibliothèque standard de Python, cherchée par le programme dans usr/bin/python
        stdlib=$(python3 -c 'import sysconfig; print(sysconfig.get_paths()["stdlib"])')
        copie_python "$stdlib" "$APPDIR/usr/bin/python/lib/$(basename "$stdlib")"
        AUTRES+=(--deploy-deps-only "$APPDIR/usr/bin/python/lib/$(basename "$stdlib")/lib-dynload")
        ;;
    mevcine)
        # ffmpeg et ffprobe, cherchés par le programme dans usr/bin/ffmpeg
        archive=$(telecharge_ffmpeg "$FFMPEG_LINUX.tar.xz")
        rm -rf "$CACHE/ffmpeg/linux" && mkdir -p "$CACHE/ffmpeg/linux" "$APPDIR/usr/bin/ffmpeg"
        tar -xJf "$archive" -C "$CACHE/ffmpeg/linux"
        install -m 755 "$CACHE/ffmpeg/linux/$FFMPEG_LINUX/bin/ffmpeg" "$CACHE/ffmpeg/linux/$FFMPEG_LINUX/bin/ffprobe" \
            "$APPDIR/usr/bin/ffmpeg/"
        cp "$CACHE/ffmpeg/linux/$FFMPEG_LINUX/LICENSE.txt" "$APPDIR/usr/bin/ffmpeg/LICENSE.txt"
        ;;
esac

# émoji des boutons : ce Qt ne dessine pas les polices émoji en couleur au format COLR (celle de
# Fedora) ; Noto Color Emoji en images (CBDT), réduite aux émoji des sources, est jointe (chargée
# par le logiciel au démarrage, voir packaging/police-emoji.py)
NOTO=https://raw.githubusercontent.com/googlefonts/noto-emoji/v2.051/fonts
telecharge "$NOTO/NotoColorEmoji.ttf" "$CACHE/noto-emoji/NotoColorEmoji.ttf" \
    72a635cb3d2f3524c51620cdde406b217204e8a6a06c6a096ff8ed4b5fd6e27b
telecharge "$NOTO/LICENSE" "$CACHE/noto-emoji/LICENSE" 6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2
if [ ! -x "$CACHE/fonttools/bin/python" ]; then
    python3 -m venv "$CACHE/fonttools"
    "$CACHE/fonttools/bin/pip" install -q fonttools==4.68.0
fi
mkdir -p "$APPDIR/usr/share/mevsuite"
SOURCES=("$RACINE/$APP/src")
[ -d "$RACINE/$APP/po" ] && SOURCES+=("$RACINE/$APP/po")
"$CACHE/fonttools/bin/python" -I "$RACINE/packaging/police-emoji.py" "$CACHE/noto-emoji/NotoColorEmoji.ttf" \
    "$APPDIR/usr/share/mevsuite/mevsuite-emoji.ttf" "${SOURCES[@]}"
cp "$CACHE/noto-emoji/LICENSE" "$APPDIR/usr/share/mevsuite/mevsuite-emoji-LICENSE.txt"

# Qt, ses greffons (dont Wayland) et les bibliothèques, sauf celles du système (D-Bus, systemd),
# présentes sur tout bureau et qui doivent être celles du système ; le greffon TIFF de Qt est mis de côté le
# temps de linuxdeploy : il attend libtiff.so.5, absente des distributions récentes
TIFF=$QT_DIR/plugins/imageformats/libqtiff.so
if [ -f "$TIFF" ]; then
    mv "$TIFF" "$QT_DIR/libqtiff.so.de-cote"
    trap 'mv "$QT_DIR/libqtiff.so.de-cote" "$TIFF"' EXIT
fi
export QMAKE=$QT_DIR/bin/qmake
export LD_LIBRARY_PATH=$QT_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
# plateformes : X11 (par défaut), Wayland, et « offscreen » pour les essais sans écran ; les
# greffons d'intégration de Wayland (fenêtres xdg-shell, décorations) ne sont pas copiés par
# linuxdeploy-plugin-qt : ils le sont ici, avec leurs dépendances
export EXTRA_PLATFORM_PLUGINS="libqwayland.so;libqoffscreen.so"
for d in wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client; do
    if [ -d "$QT_DIR/plugins/$d" ]; then
        mkdir -p "$APPDIR/usr/plugins/$d"
        cp "$QT_DIR/plugins/$d"/*.so "$APPDIR/usr/plugins/$d/"
        AUTRES+=(--deploy-deps-only "$APPDIR/usr/plugins/$d")
    fi
done
linuxdeploy-x86_64.AppImage --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/$APP" \
    --desktop-file "$APPDIR/usr/share/applications/$APP.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/$APP.png" \
    "${AUTRES[@]}" --plugin qt
# linuxdeploy-plugin-qt ne connaît pas les exclusions : elles sont retirées après coup
rm -f "$APPDIR"/usr/lib/libsystemd.so* "$APPDIR"/usr/lib/libgcrypt.so* "$APPDIR"/usr/lib/libgpg-error.so* \
    "$APPDIR"/usr/lib/libdbus-1.so* "$APPDIR"/usr/lib/libcap.so*
# Kerberos (demandé par Qt Network) est copié, mais pas libcom_err, que linuxdeploy croit présente
# partout : elle manque sur un Debian minimal
if [ -f "$APPDIR/usr/lib/libkrb5.so.3" ] && [ ! -f "$APPDIR/usr/lib/libcom_err.so.2" ]; then
    cp -L "$(ldconfig -p | awk '/libcom_err\.so\.2 .*x86-64/ { print $NF; exit }')" "$APPDIR/usr/lib/"
fi

SORTIE=$DIST/$NOM-$VERSION-x86_64.AppImage
rm -f "$SORTIE"
ARCH=x86_64 appimagetool-x86_64.AppImage --no-appstream --runtime-file "$OUTILS/runtime-x86_64" "$APPDIR" "$SORTIE"
echo "== $(basename "$SORTIE") ($(du -h "$SORTIE" | cut -f1))"
