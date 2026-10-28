cmake_minimum_required(VERSION 3.21)

project(mevpix
    VERSION 0.3.1
    DESCRIPTION "Mesures de longueurs sur n'importe quelle image : étalonnage, mesures, profil d'intensité"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Type de compilation" FORCE)
endif()

include(CTest)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Concurrent)
if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
endif()
qt_standard_project_setup()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Code sans widgets (géométrie, profil, chargement des images, réglages, traductions, notice) :
# partagé par le programme et les tests.
add_library(mevpix_core STATIC
    src/help.cpp src/help.h
    src/i18n.cpp src/i18n.h
    src/model.cpp src/model.h
    src/profile.cpp src/profile.h
    src/savefile.h
    src/session.cpp src/session.h
    src/settings.cpp src/settings.h
    src/view.cpp src/view.h
)
target_include_directories(mevpix_core PUBLIC src)
target_link_libraries(mevpix_core PUBLIC Qt6::Core Qt6::Gui)
target_compile_definitions(mevpix_core PUBLIC MEVPIX_VERSION="${PROJECT_VERSION}")
# Une traduction incomplète (texte oublié dans une table `Tr`) est une erreur de compilation.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(src/i18n.cpp PROPERTIES COMPILE_OPTIONS "-Werror=missing-field-initializers")
endif()
# Notice d'utilisation, écrite sur le disque au clic sur « Aide ».
qt_add_resources(mevpix_core help
    PREFIX /help
    BASE assets/help
    FILES
        assets/help/fr.html assets/help/en.html assets/help/help.css
        assets/help/accueil-fr.jpg assets/help/accueil-en.jpg
        assets/help/fenetre-fr.jpg assets/help/etalonnage-fr.jpg assets/help/profil-fr.jpg
        assets/help/fenetre-en.jpg assets/help/etalonnage-en.jpg assets/help/profil-en.jpg
)

# Interface (fenêtre, zone image, graphique, « À propos », thème, icônes) : partagée par le
# programme et les essais de l'interface.
add_library(mevpix_ui STATIC
    src/about.cpp src/about.h
    src/app.cpp src/app.h
    src/canvas.cpp src/canvas.h
    src/crash.cpp src/crash.h
    src/icons.cpp src/icons.h
    src/plot.cpp src/plot.h
    src/style.h
    src/theme.cpp src/theme.h
)
target_link_libraries(mevpix_ui PUBLIC mevpix_core Qt6::Widgets Qt6::Concurrent)
# Sous Linux, le thème clair ou sombre du bureau est lu auprès du portail (D-Bus).
if(UNIX AND NOT APPLE)
    find_package(Qt6 6.8 REQUIRED COMPONENTS DBus)
    target_link_libraries(mevpix_ui PRIVATE Qt6::DBus)
    target_compile_definitions(mevpix_ui PRIVATE MEVPIX_PORTAL)
endif()
qt_add_resources(mevpix_ui icons
    PREFIX /icons
    BASE assets/icons
    FILES
        assets/icons/mevpix-16.png assets/icons/mevpix-32.png assets/icons/mevpix-48.png
        assets/icons/mevpix-64.png assets/icons/mevpix-128.png assets/icons/mevpix-256.png
)

qt_add_executable(mevpix WIN32 src/main.cpp)
target_link_libraries(mevpix PRIVATE mevpix_ui)
if(WIN32)
    set(MEVPIX_ICO "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/mevpix.ico")
    configure_file(platform/mevpix.rc.in mevpix.rc @ONLY)
    target_sources(mevpix PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/mevpix.rc")
endif()

install(TARGETS mevpix RUNTIME DESTINATION bin)

# Linux : lanceur, icônes, licence ; paquets .deb et .rpm (packaging/paquets.cmake)
set(MEVSUITE_PAQUET mevpix)
set(MEVSUITE_RESUME "MeVPix : mesures de longueurs sur une image (TP de physique)")
set(MEVSUITE_ICONES "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons")
set(MEVSUITE_LICENCE "MIT")
set(MEVSUITE_LICENCES "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE")
set(MEVSUITE_DEB_DEPENDS "qt6-qpa-plugins, qt6-wayland, qt6-image-formats-plugins")
set(MEVSUITE_RPM_REQUIRES "qt6-qtwayland, qt6-qtimageformats")
include("${CMAKE_CURRENT_SOURCE_DIR}/../packaging/paquets.cmake" OPTIONAL)

if(BUILD_TESTING)
    qt_add_executable(mevpix_tests tests/tests.cpp)
    target_link_libraries(mevpix_tests PRIVATE mevpix_core Qt6::Test)
    add_test(NAME mevpix_tests COMMAND mevpix_tests)
    set_tests_properties(mevpix_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Essais de l'interface, sur la photo d'exemple : chaque étape est rendue dans `captures/`
    # (dossier de compilation), d'où viennent les images de la notice (tools/images_notice.py).
    qt_add_executable(mevpix_uitests tests/uitests.cpp)
    target_link_libraries(mevpix_uitests PRIVATE mevpix_ui Qt6::Test)
    target_compile_definitions(mevpix_uitests PRIVATE MEVPIX_SOURCES="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME mevpix_uitests COMMAND mevpix_uitests)
    set_tests_properties(mevpix_uitests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
