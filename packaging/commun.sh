# Fonctions communes aux scripts d'empaquetage (à charger avec « source »).
#
# Variables facultatives : DIST (dossier des fichiers produits, dist/ par défaut), CACHE
# (téléchargements et dossiers intermédiaires, build-paquet/ par défaut).

RACINE=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
DIST=${DIST:-$RACINE/dist}
CACHE=${CACHE:-$RACINE/build-paquet}
mkdir -p "$DIST" "$CACHE"
DIST=$(cd "$DIST" && pwd)
CACHE=$(cd "$CACHE" && pwd)

# ffmpeg LGPL de BtbN (https://github.com/BtbN/FFmpeg-Builds) : statique pour Linux, avec ses DLL
# pour Windows. L'empreinte est vérifiée avec le fichier checksums.sha256 publié à côté.
FFMPEG_URL=https://github.com/BtbN/FFmpeg-Builds/releases/download/latest
FFMPEG_LINUX=ffmpeg-n9.0-latest-linux64-lgpl-9.0
FFMPEG_WINDOWS=ffmpeg-n9.0-latest-win64-lgpl-shared-9.0

# nom affiché du logiciel $1 (mevpix → MeVPix)
nom_affiche() {
    case $1 in
        mevpix) echo MeVPix ;;
        mevcine) echo MeVCine ;;
        mevreg) echo MeVReg ;;
        mevsono) echo MeVSono ;;
        *) echo "logiciel inconnu : $1" >&2; return 1 ;;
    esac
}

# version du logiciel $1, lue dans son CMakeLists.txt (project(… VERSION x.y.z))
version_de() {
    grep -oE 'VERSION [0-9]+\.[0-9]+\.[0-9]+' "$RACINE/$1/CMakeLists.txt" | head -n 1 | cut -d' ' -f2
}

# télécharge $1 dans $2 et vérifie son empreinte SHA-256 $3
telecharge() {
    local url=$1 fichier=$2 empreinte=$3
    if [ ! -f "$fichier" ] || ! echo "$empreinte  $fichier" | sha256sum -c --quiet - 2>/dev/null; then
        mkdir -p "$(dirname "$fichier")"
        curl -fsSL --retry 3 -o "$fichier.part" "$url"
        echo "$empreinte  $fichier.part" | sha256sum -c --quiet - \
            || { echo "empreinte SHA-256 incorrecte : $url" >&2; return 1; }
        mv "$fichier.part" "$fichier"
    fi
}

# télécharge l'archive ffmpeg $1 de BtbN, vérifiée par son checksums.sha256 ; affiche son chemin
telecharge_ffmpeg() {
    local archive=$1 dossier=$CACHE/ffmpeg
    mkdir -p "$dossier"
    if [ ! -f "$dossier/$archive" ]; then
        curl -fsSL --retry 3 -o "$dossier/checksums.sha256" "$FFMPEG_URL/checksums.sha256"
        curl -fsSL --retry 3 -o "$dossier/$archive.part" "$FFMPEG_URL/$archive"
        local attendu
        attendu=$(awk -v f="$archive" '$2 == f { print $1 }' "$dossier/checksums.sha256")
        if [ -z "$attendu" ] || ! echo "$attendu  $dossier/$archive.part" | sha256sum -c --quiet -; then
            echo "empreinte SHA-256 incorrecte : $archive" >&2
            return 1
        fi
        mv "$dossier/$archive.part" "$dossier/$archive"
    fi
    echo "$dossier/$archive"
}

# bibliothèque standard de Python ($1 : dossier lib/python3.X) copiée dans $2, sans les tests, les
# interfaces graphiques, les paquets installés ni les fichiers compilés
copie_python() {
    local source=$1 dest=$2
    mkdir -p "$dest"
    (cd "$source" && tar -cf - \
        --exclude='./test' --exclude='./idlelib' --exclude='./tkinter' --exclude='./turtledemo' \
        --exclude='./ensurepip' --exclude='./lib2to3' --exclude='./site-packages' --exclude='./dist-packages' \
        --exclude='./config-*' --exclude='__pycache__' --exclude='*.pyc' --exclude='./pydoc_data' \
        --exclude='./venv' --exclude='./unittest/test' --exclude='./sqlite3/test' --exclude='./ctypes/test' \
        --exclude='./idle_test' .) | (cd "$dest" && tar -xf -)
}
