cmake_minimum_required(VERSION 3.21)

project(mevcine
    VERSION 0.2.0
    DESCRIPTION "MeVCine : étude cinématique de vidéos pour le lycée (pointage, repère, échelle, export CSV et listes Python)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Type de compilation" FORCE)
endif()

include(CTest)

# Qt 6.8 : choix du thème clair ou sombre (QStyleHints::setColorScheme)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Concurrent)
if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
endif()
qt_standard_project_setup()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Code sans widgets (pointages, calculs, exports, projets, lecture des vidéos, suivi automatique,
# traductions) : partagé par le programme et les tests.
add_library(mevcine_core STATIC
    src/calculs.cpp src/calculs.h
    src/chemins.cpp src/chemins.h
    src/debogage.h
    src/detect.cpp src/detect.h
    src/echelle.h
    src/export.cpp src/export.h
    src/fichier.h
    src/format.cpp src/format.h
    src/hdr.cpp src/hdr.h
    src/i18n.cpp src/i18n.h
    src/pointage.cpp src/pointage.h
    src/prefs.cpp src/prefs.h
    src/vecteur.h
    src/video.cpp src/video.h
)
target_include_directories(mevcine_core PUBLIC src)
target_link_libraries(mevcine_core PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent)
target_compile_definitions(mevcine_core PUBLIC
    MEVCINE_VERSION="${PROJECT_VERSION}"
    # dossiers de secours, quand le programme est lancé depuis le dossier de compilation
    MEVCINE_DONNEES_SOURCES="${CMAKE_CURRENT_SOURCE_DIR}/data"
)
# Traductions (fichiers gettext), incluses dans l'exécutable.
qt_add_resources(mevcine_core langues
    PREFIX /lang
    BASE data/lang
    FILES data/lang/en.po
)

# L'interface : la fenêtre principale et ses parties, partagées par le programme et les tests de
# l'interface.
add_library(mevcine_interface STATIC
    src/app/a_propos.cpp src/app/a_propos.h
    src/app/accueil.cpp src/app/accueil.h
    src/app/app.cpp src/app/app.h
    src/app/demarrage.cpp src/app/demarrage.h
    src/app/dessin.cpp src/app/dessin.h
    src/app/dialogues.cpp src/app/dialogues.h
    src/app/guide.cpp src/app/guide.h
    src/app/icones.cpp src/app/icones.h
    src/app/onglet_coord.cpp src/app/onglet_coord.h
    src/app/onglet_graph.cpp src/app/onglet_graph.h
    src/app/onglet_pointage.cpp src/app/onglet_pointage.h
    src/app/plein_ecran.cpp src/app/plein_ecran.h
    src/app/onglet_trajectoire.cpp src/app/onglet_trajectoire.h
    src/app/tableau.cpp src/app/tableau.h
    src/app/theme.cpp src/app/theme.h
)
target_link_libraries(mevcine_interface PUBLIC mevcine_core Qt6::Widgets)
# Sous Linux, le thème clair ou sombre du bureau est lu auprès du portail (D-Bus).
if(UNIX AND NOT APPLE)
    find_package(Qt6 6.8 REQUIRED COMPONENTS DBus)
    target_link_libraries(mevcine_interface PRIVATE Qt6::DBus)
    target_compile_definitions(mevcine_interface PRIVATE MEVCINE_PORTAIL)
endif()
qt_add_resources(mevcine_interface icones
    PREFIX /icones
    BASE data/icones
    FILES
        data/icones/mevcine-16.png data/icones/mevcine-32.png data/icones/mevcine-48.png
        data/icones/mevcine-64.png data/icones/mevcine-256.png
)

qt_add_executable(mevcine WIN32
    src/erreurs.cpp src/erreurs.h
    src/main.cpp
)
target_link_libraries(mevcine PRIVATE mevcine_interface)
if(WIN32)
    set(MEVCINE_ICO "${CMAKE_CURRENT_SOURCE_DIR}/data/icones/mevcine.ico")
    configure_file(platform/mevcine.rc.in mevcine.rc @ONLY)
    target_sources(mevcine PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/mevcine.rc")
endif()

install(TARGETS mevcine RUNTIME DESTINATION bin)
install(DIRECTORY data/help DESTINATION share/mevcine/data)

# Linux : lanceur, icônes, licence ; paquets .deb et .rpm (packaging/paquets.cmake)
set(MEVSUITE_PAQUET mevcine)
set(MEVSUITE_RESUME "MeVCine : étude cinématique de vidéos (TP de physique)")
set(MEVSUITE_ICONES "${CMAKE_CURRENT_SOURCE_DIR}/data/icones")
set(MEVSUITE_LICENCE "GPL-3.0-or-later")
set(MEVSUITE_LICENCES "${CMAKE_CURRENT_SOURCE_DIR}/COPYING" "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE")
set(MEVSUITE_DEB_DEPENDS "qt6-qpa-plugins, qt6-wayland, ffmpeg")
set(MEVSUITE_RPM_REQUIRES "qt6-qtwayland, /usr/bin/ffmpeg, /usr/bin/ffprobe")
include("${CMAKE_CURRENT_SOURCE_DIR}/../packaging/paquets.cmake" OPTIONAL)
# data/video : vidéos des essais automatiques et des captures de la notice, pas livrées (le logiciel
# ne propose pas de vidéos d'exemple : chacun ouvre les siennes)

if(BUILD_TESTING)
    # vidéos d'essai fabriquées par ffmpeg, une fois pour toutes, dans le dossier de compilation
    set(MEVCINE_VIDEOS_TESTS "${CMAKE_CURRENT_BINARY_DIR}/videos-tests")

    qt_add_executable(mevcine_tests tests/tests.cpp tests/outils.h)
    target_link_libraries(mevcine_tests PRIVATE mevcine_core Qt6::Test)
    target_compile_definitions(mevcine_tests PRIVATE
        MEVCINE_SOURCES="${CMAKE_CURRENT_SOURCE_DIR}" MEVCINE_VIDEOS_TESTS="${MEVCINE_VIDEOS_TESTS}")
    add_test(NAME mevcine_tests COMMAND mevcine_tests)
    set_tests_properties(mevcine_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(mevcine_tests_interface tests/tests_interface.cpp tests/outils.h)
    target_link_libraries(mevcine_tests_interface PRIVATE mevcine_interface Qt6::Test)
    target_compile_definitions(mevcine_tests_interface PRIVATE
        MEVCINE_SOURCES="${CMAKE_CURRENT_SOURCE_DIR}" MEVCINE_VIDEOS_TESTS="${MEVCINE_VIDEOS_TESTS}"
        MEVCINE_RENDUS_TESTS="${CMAKE_CURRENT_BINARY_DIR}/rendus-tests")
    add_test(NAME mevcine_tests_interface COMMAND mevcine_tests_interface)
    set_tests_properties(mevcine_tests_interface PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
