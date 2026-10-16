cmake_minimum_required(VERSION 3.21)

project(mevsono
    VERSION 0.2.0
    DESCRIPTION "Analyse spectrale des sons pour le lycée : signal, spectre, fondamentale et harmoniques"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Type de compilation" FORCE)
endif()

include(CTest)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Multimedia Concurrent)
if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
endif()
qt_standard_project_setup()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Code sans widgets (analyse, lecture des sons, écoute, réglages, traductions, notice) : partagé par
# le programme et les tests.
add_library(mevsono_core STATIC
    src/analyzer.cpp src/analyzer.h
    src/audio.cpp src/audio.h
    src/dsp.cpp src/dsp.h
    src/fft.cpp src/fft.h
    src/format.cpp src/format.h
    src/help.cpp src/help.h
    src/i18n.cpp src/i18n.h
    src/player.cpp src/player.h
    src/session.cpp src/session.h
    src/settings.cpp src/settings.h
    src/wave.cpp src/wave.h
)
target_include_directories(mevsono_core PUBLIC src)
target_link_libraries(mevsono_core PUBLIC Qt6::Core Qt6::Gui Qt6::Multimedia)
target_compile_definitions(mevsono_core PUBLIC MEVSONO_VERSION="${PROJECT_VERSION}")
# Une traduction incomplète (texte oublié dans une table `Tr`) est une erreur de compilation.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(src/i18n.cpp PROPERTIES COMPILE_OPTIONS "-Werror=missing-field-initializers")
endif()
# Notice d'utilisation, écrite sur le disque au clic sur « Aide ».
qt_add_resources(mevsono_core help
    PREFIX /help
    BASE assets/help
    FILES
        assets/help/fr.html assets/help/en.html assets/help/help.css
        assets/help/accueil-fr.jpg assets/help/fenetre-fr.jpg assets/help/mesure-fr.jpg assets/help/expert-fr.jpg
        assets/help/accueil-en.jpg assets/help/fenetre-en.jpg assets/help/mesure-en.jpg assets/help/expert-en.jpg
)

# Interface (fenêtre, graphiques, réglages, « À propos », thème, icônes) : partagée par le programme
# et les essais de l'interface.
add_library(mevsono_ui STATIC
    src/about.cpp src/about.h
    src/app.cpp src/app.h
    src/crash.cpp src/crash.h
    src/icons.cpp src/icons.h
    src/plot.cpp src/plot.h
    src/settingsdialog.cpp src/settingsdialog.h
    src/signalplot.cpp src/signalplot.h
    src/spectrumplot.cpp src/spectrumplot.h
    src/style.h
    src/theme.cpp src/theme.h
)
target_link_libraries(mevsono_ui PUBLIC mevsono_core Qt6::Widgets Qt6::Concurrent)
# Sous Linux, le thème clair ou sombre du bureau est lu auprès du portail (D-Bus).
if(UNIX AND NOT APPLE)
    find_package(Qt6 6.8 REQUIRED COMPONENTS DBus)
    target_link_libraries(mevsono_ui PRIVATE Qt6::DBus)
    target_compile_definitions(mevsono_ui PRIVATE MEVSONO_PORTAL)
endif()
qt_add_resources(mevsono_ui icons
    PREFIX /icons
    BASE assets/icons
    FILES
        assets/icons/mevsono-16.png assets/icons/mevsono-32.png assets/icons/mevsono-48.png
        assets/icons/mevsono-64.png assets/icons/mevsono-128.png assets/icons/mevsono-256.png
)

qt_add_executable(mevsono WIN32 src/main.cpp)
target_link_libraries(mevsono PRIVATE mevsono_ui)
if(WIN32)
    set(MEVSONO_ICO "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/mevsono.ico")
    configure_file(platform/mevsono.rc.in mevsono.rc @ONLY)
    target_sources(mevsono PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/mevsono.rc")
endif()

install(TARGETS mevsono RUNTIME DESTINATION bin)

# Linux : lanceur, icônes, licence ; paquets .deb et .rpm (packaging/paquets.cmake)
set(MEVSUITE_PAQUET mevsono)
set(MEVSUITE_RESUME "MeVSono : analyse spectrale des sons (TP de physique)")
set(MEVSUITE_ICONES "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons")
set(MEVSUITE_LICENCE "MIT")
set(MEVSUITE_LICENCES "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE")
set(MEVSUITE_DEB_DEPENDS "qt6-qpa-plugins, qt6-wayland, ffmpeg")
set(MEVSUITE_RPM_REQUIRES "qt6-qtwayland, /usr/bin/ffmpeg")
include("${CMAKE_CURRENT_SOURCE_DIR}/../packaging/paquets.cmake" OPTIONAL)

if(BUILD_TESTING)
    qt_add_executable(mevsono_tests tests/tests.cpp)
    target_link_libraries(mevsono_tests PRIVATE mevsono_core Qt6::Test)
    add_test(NAME mevsono_tests COMMAND mevsono_tests)
    set_tests_properties(mevsono_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Essais de l'interface : chaque étape est rendue dans `captures/` (dossier de compilation), ce
    # qui sert aussi aux images de la notice.
    qt_add_executable(mevsono_uitests tests/uitests.cpp)
    target_link_libraries(mevsono_uitests PRIVATE mevsono_ui Qt6::Test)
    add_test(NAME mevsono_uitests COMMAND mevsono_uitests)
    set_tests_properties(mevsono_uitests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
