#!/usr/bin/env bash
# Arguments : répertoires de compilation de MeVPix, MeVCine, MeVSono puis MeVReg.
set -euo pipefail
export QT_QPA_PLATFORM=offscreen
MEVSUITE_CSV_TEST=$(mktemp -d)
export MEVSUITE_CSV_TEST
trap 'rm -rf "$MEVSUITE_CSV_TEST"' EXIT
"$1/mevpix_tests" format_csv
"$2/mevcine_tests" csv_un_objet
"$3/mevsono_tests" exports_csv
"$4/test_core" imports_de_la_suite
