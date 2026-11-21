#!/usr/bin/env bash
# Windows x64 : version portable (dossier dans une archive .zip, à copier sur une clé USB ou un
# poste) et installateur (Inno Setup : Program Files, menu Démarrer) d'un logiciel de MevSuite :
#
#   packaging/windows.sh <mevpix|mevcine|mevreg|mevsono>
#
# À lancer dans un terminal MSYS2 UCRT64 : le programme est compilé avec MinGW-w64 et le Qt de
# MSYS2 ; le dossier reçoit les DLL dont il dépend (copiées de /ucrt64/bin), les greffons de Qt
# utiles, les traductions de Qt, et pour MeVReg la bibliothèque standard de Python ; pour MeVCine
# et MeVSono, ffmpeg (version LGPL de BtbN). Les fichiers vont dans dist/ :
#
#   <Nom>-<version>-windows-x64.zip          version portable (dossier <Nom>-<version>)
#   <Nom>-<version>-windows-x64-setup.exe    installateur ; installation silencieuse (WAPT, GPO) :
#                                            /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
#
# Il faut, dans MSYS2 : mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,qt6-base,qt6-svg,
# qt6-multimedia,qt6-imageformats,qt6-translations,python,python-numpy}, zip, curl, unzip ; et Inno Setup 6 (ISCC.exe)
# installé sous Windows.
set -euo pipefail
usage="usage : packaging/windows.sh <mevpix|mevcine|mevreg|mevsono>"
APP=${1:?$usage}
source "$(dirname "$0")/commun.sh"
NOM=$(nom_affiche "$APP")
VERSION=$(version_de "$APP")
PREFIXE=${MINGW_PREFIX:-/ucrt64}

# identifiant de l'installateur (fixe : mises à jour et désinstallation par WAPT)
case $APP in
    mevpix) APPID=06E86AC8-63F1-4E51-BCCC-FB0E08D6A89F ;;
    mevcine) APPID=05191978-6171-4695-9BC7-83FA4217C391 ;;
    mevreg) APPID=583D2245-CC2F-44C1-978D-0120C07B0D93 ;;
    mevsono) APPID=8966E5E7-CF69-4AB7-8A76-256E720598E8 ;;
esac

BUILD=$CACHE/$APP-windows
DOSSIER=$BUILD/$NOM-$VERSION
OPTIONS=()
if [ "$APP" = mevreg ]; then
    PYTHON="$PREFIXE/bin/python.exe"
    OPTIONS+=("-DPython3_EXECUTABLE=$(cygpath -m "$PYTHON")" "-DPython3_ROOT_DIR=$(cygpath -m "$PREFIXE")")
fi
cmake -S "$RACINE/$APP" -B "$BUILD/compilation" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF -DMEVREG_TESTS=OFF "${OPTIONS[@]}"
cmake --build "$BUILD/compilation"
rm -rf "$DOSSIER"
mkdir -p "$DOSSIER"
cp "$BUILD/compilation/$APP.exe" "$DOSSIER/"
strip "$DOSSIER/$APP.exe"

# greffons et traductions de Qt ; qt.conf leur donne leur place, à côté du programme
QMAKE=$(command -v qmake6 || command -v qmake-qt6 || echo "$PREFIXE/share/qt6/bin/qmake")
GREFFONS=$(cygpath -u "$("$QMAKE" -query QT_INSTALL_PLUGINS | tr -d '\r')")
TRADUCTIONS=$(cygpath -u "$("$QMAKE" -query QT_INSTALL_TRANSLATIONS | tr -d '\r')")
greffon() {
    local f=$GREFFONS/$1
    if [ -f "$f" ]; then
        mkdir -p "$DOSSIER/plugins/$(dirname "$1")"
        cp "$f" "$DOSSIER/plugins/$1"
    fi
}
greffon platforms/qwindows.dll
greffon platforms/qoffscreen.dll
for f in "$GREFFONS"/imageformats/*.dll "$GREFFONS"/iconengines/*.dll; do
    [ -f "$f" ] && greffon "${f#"$GREFFONS"/}"
done
case $APP in
    mevreg) greffon printsupport/windowsprintersupport.dll ;;
    # lecture des sons par Windows Media Foundation ; les autres formats passent par ffmpeg
    mevsono) greffon multimedia/windowsmediaplugin.dll ;;
esac
mkdir -p "$DOSSIER/translations"
for lang in fr en; do
    for f in "$TRADUCTIONS"/qtbase_$lang.qm "$TRADUCTIONS"/qt_$lang.qm; do
        [ -f "$f" ] && cp "$f" "$DOSSIER/translations/"
    done
done
printf '[Paths]\r\nPrefix = .\r\nPlugins = plugins\r\nTranslations = translations\r\n' > "$DOSSIER/qt.conf"

case $APP in
    mevreg)
        # bibliothèque standard de Python, cherchée par le programme dans python/
        stdlib=$(cygpath -u "$("$PYTHON" -I -c 'import sysconfig; print(sysconfig.get_paths()["stdlib"])' | tr -d '\r')")
        version_py=$("$PYTHON" -I -c 'import sys; print("python%d.%d" % sys.version_info[:2])' | tr -d '\r')
        dest="$DOSSIER/python/lib/$version_py"
        copie_python "$stdlib" "$dest"
        dynload=$(cygpath -u "$("$PYTHON" -I -c 'import math, pathlib; print(pathlib.Path(math.__file__).parent.as_posix())' | tr -d '\r')")
        if [ "$dynload" != "$stdlib/lib-dynload" ]; then
            mkdir -p "$dest/lib-dynload"
            cp "$dynload"/*.pyd "$dest/lib-dynload/"
        fi
        copie_numpy "$PYTHON" "$DOSSIER/python/packages"
        ;;
esac
case $APP in
    mevcine|mevsono)
        # ffmpeg (et ffprobe) avec ses DLL, cherchés par le programme dans ffmpeg/
        archive=$(telecharge_ffmpeg "$FFMPEG_WINDOWS.zip")
        rm -rf "$CACHE/ffmpeg/windows" && mkdir -p "$CACHE/ffmpeg/windows" "$DOSSIER/ffmpeg"
        unzip -q "$archive" -d "$CACHE/ffmpeg/windows"
        cp "$CACHE/ffmpeg/windows/$FFMPEG_WINDOWS"/bin/*.dll "$CACHE/ffmpeg/windows/$FFMPEG_WINDOWS/bin/ffmpeg.exe" \
            "$DOSSIER/ffmpeg/"
        if [ "$APP" = mevcine ]; then
            cp "$CACHE/ffmpeg/windows/$FFMPEG_WINDOWS/bin/ffprobe.exe" "$DOSSIER/ffmpeg/"
        fi
        cp "$CACHE/ffmpeg/windows/$FFMPEG_WINDOWS/LICENSE.txt" "$DOSSIER/ffmpeg/LICENSE.txt"
        ;;
esac
if [ "$APP" = mevcine ]; then
    mkdir -p "$DOSSIER/data"
    cp -r "$RACINE/mevcine/data/help" "$DOSSIER/data/"
fi

# licences et mode d'emploi, avec des fins de ligne Windows (Bloc-notes)
crlf() { sed 's/\r*$/\r/' "$1" > "$2"; }
if [ "$APP" = mevcine ]; then
    crlf "$RACINE/mevcine/COPYING" "$DOSSIER/COPYING.txt"
    crlf "$RACINE/mevcine/NOTICE" "$DOSSIER/NOTICE.txt"
else
    crlf "$RACINE/LICENSE" "$DOSSIER/LICENSE.txt"
fi
REGLAGES='Les réglages sont rangés dans %APPDATA%\MeVSuite.'
if [ "$APP" = mevreg ]; then
    REGLAGES='Le ZIP conserve les réglages et la récupération dans profil, à côté de mevreg.exe. En installation ou si le dossier est en lecture seule, ils sont dans %APPDATA%\MeVSuite.'
fi
cat > "$BUILD/LISEZMOI.txt" <<FIN
$NOM $VERSION — Rémi MEVAERE — suite@sciences-physiques.net

Version portable : ce dossier se copie tel quel (clé USB, réseau, disque) ; aucune installation.
Lancer $APP.exe. La notice s'ouvre par le menu Aide (F1).

Au premier lancement, Windows peut afficher « Windows a protégé votre ordinateur » : le programme
n'est pas signé numériquement. Cliquer sur « Informations complémentaires », puis « Exécuter quand
même ».

$REGLAGES Logiciel libre : voir le fichier de licence.
Bibliothèques livrées (Qt sous licence LGPL version 3…) : leurs licences et leurs versions sont dans
le dossier licences ; leurs sources sont celles des paquets MSYS2 indiqués (https://packages.msys2.org).
FIN
crlf "$BUILD/LISEZMOI.txt" "$DOSSIER/LISEZMOI.txt"

# DLL : toutes celles dont dépendent le programme, les greffons, ffmpeg et les modules de Python,
# copiées de MSYS2 à côté du programme ; une DLL introuvable (ni dans MSYS2, ni dans Windows) arrête
# la fabrication
declare -A presentes=()
while IFS= read -r -d '' f; do
    n=$(basename "$f")
    presentes[${n,,}]=1
done < <(find "$DOSSIER" -iname '*.dll' -print0)
mapfile -d '' -t binaires < <(find "$DOSSIER" \( -iname '*.exe' -o -iname '*.dll' -o -iname '*.pyd' \) -print0)
for ((i = 0; i < ${#binaires[@]}; i++)); do
    f=${binaires[i]}
    imports=$(objdump -p "$f" | sed -n 's/^[[:space:]]*DLL Name: //p' | tr -d '\r')
    while IFS= read -r dll; do
        [ -n "$dll" ] || continue
        cle=${dll,,}
        [ -z "${presentes[$cle]:-}" ] || continue
        if [ -f "$PREFIXE/bin/$dll" ]; then
            cp "$PREFIXE/bin/$dll" "$DOSSIER/"
            presentes[$cle]=1
            binaires+=("$DOSSIER/$dll")
        elif [ -f "/c/Windows/System32/$dll" ] || [[ $cle == api-ms-win-* ]] || [[ $cle == ext-ms-* ]]; then
            presentes[$cle]=systeme
        else
            echo "DLL introuvable : $dll (demandée par ${f#"$DOSSIER"/})" >&2
            exit 1
        fi
    done <<< "$imports"
done

# Notices des bibliothèques livrées (Qt, Python, compilateur…) : pour chaque DLL venue de MSYS2, la
# licence de son paquet, dans licences/<paquet>, et la liste des paquets avec leur version (leurs
# sources : https://packages.msys2.org). ffmpeg a la sienne, dans ffmpeg/.
mkdir -p "$DOSSIER/licences"
declare -A paquets=()
while IFS= read -r -d '' f; do
    case $f in
        "$DOSSIER"/plugins/*) origine=$GREFFONS/${f#"$DOSSIER"/plugins/} ;;
        *) origine=$PREFIXE/bin/$(basename "$f") ;;
    esac
    [ -f "$origine" ] || continue
    paquet=$(pacman -Qqo "$origine" 2>/dev/null) || continue
    paquets[$paquet]=1
done < <(find "$DOSSIER" -iname '*.dll' -not -path "$DOSSIER/ffmpeg/*" -print0)
: > "$DOSSIER/licences/PAQUETS.txt"
for paquet in "${!paquets[@]}"; do
    court=${paquet#"${MINGW_PACKAGE_PREFIX:-mingw-w64-ucrt-x86_64}-"}
    if [ -d "$PREFIXE/share/licenses/$court" ]; then
        mkdir -p "$DOSSIER/licences/$court"
        cp -r "$PREFIXE/share/licenses/$court/." "$DOSSIER/licences/$court/"
    fi
    pacman -Q "$paquet" >> "$DOSSIER/licences/PAQUETS.txt"
done
sort -o "$DOSSIER/licences/PAQUETS.txt" "$DOSSIER/licences/PAQUETS.txt"
sed -i 's/\r*$/\r/' "$DOSSIER/licences/PAQUETS.txt"
echo "== $NOM $VERSION : $(find "$DOSSIER" -type f | wc -l) fichiers, $(du -sh "$DOSSIER" | cut -f1)"

# essai : le programme démarre sans écran (et, pour MeVReg, Python trouve sa bibliothèque), sans
# rien prendre dans MSYS2
ESSAI_PATH=/c/Windows/System32:/c/Windows:/usr/bin
if [ "$APP" = mevpix ]; then
    PATH=$ESSAI_PATH QT_QPA_PLATFORM=offscreen "$DOSSIER/mevpix.exe" --selftest-formats
fi
if [ "$APP" = mevreg ]; then
    PATH=/c/Windows/System32:/c/Windows "$DOSSIER/mevreg.exe" --selftest
fi
statut=0
PATH=$ESSAI_PATH QT_QPA_PLATFORM=offscreen /usr/bin/timeout 5 "$DOSSIER/$APP.exe" || statut=$?
if [ "$statut" != 124 ]; then
    echo "$APP.exe s'est arrêté au démarrage (code $statut)" >&2
    exit 1
fi

# version portable
if [ "$APP" = mevreg ]; then
    # Seule la copie distribuée en ZIP emporte son profil sur la clé.
    touch "$DOSSIER/mevreg.portable"
fi
ZIP=$DIST/$NOM-$VERSION-windows-x64.zip
rm -f "$ZIP"
(cd "$BUILD" && zip -qr9 "$ZIP" "$NOM-$VERSION")
echo "== $(basename "$ZIP") ($(du -h "$ZIP" | cut -f1))"
if [ "$APP" = mevreg ]; then
    # Un autre chemin et un autre dossier courant détectent les dépendances au poste de compilation.
    essai=$(mktemp -d "$BUILD/TP électricité.XXXXXX")
    unzip -q "$ZIP" -d "$essai"
    (cd "$BUILD" && PATH=/c/Windows/System32:/c/Windows PYTHONHOME=Z:/absent PYTHONPATH=Z:/absent \
        "$essai/$NOM-$VERSION/mevreg.exe" --selftest)
    rm -rf "$essai"
    rm "$DOSSIER/mevreg.portable"
fi

# installateur
ISCC=$(command -v iscc || command -v ISCC || echo "/c/Program Files (x86)/Inno Setup 6/ISCC.exe")
case $APP in
    mevcine) ICONE=$RACINE/mevcine/data/icones/mevcine.ico ;;
    *) ICONE=$RACINE/$APP/assets/icons/$APP.ico ;;
esac
EXTENSION=
[ "$APP" = mevreg ] && EXTENSION=phy
[ "$APP" = mevcine ] && EXTENSION=mevcine
[ "$APP" = mevpix ] && EXTENSION=mevpix
# MSYS2 ne doit pas prendre les options /D… pour des chemins
MSYS2_ARG_CONV_EXCL='*' "$ISCC" /Q \
    "/DAppNom=$NOM" "/DAppExe=$APP.exe" "/DAppVersion=$VERSION" "/DAppId=$APPID" \
    "/DSource=$(cygpath -w "$DOSSIER")" "/DSortie=$(cygpath -w "$DIST")" "/DIcone=$(cygpath -w "$ICONE")" \
    "/DExtension=$EXTENSION" "$(cygpath -w "$RACINE/packaging/windows/installateur.iss")"
SETUP=$DIST/$NOM-$VERSION-windows-x64-setup.exe
test -f "$SETUP"
echo "== $(basename "$SETUP") ($(du -h "$SETUP" | cut -f1))"
