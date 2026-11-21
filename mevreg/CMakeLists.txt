cmake_minimum_required(VERSION 3.21)

project(MeVReg VERSION 0.4.0 DESCRIPTION "Traitement de données pour les TP de physique-chimie" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Type de compilation" FORCE)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Svg PrintSupport Concurrent)
if(UNIX AND NOT APPLE)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
endif()
# Le programme embarque CPython. NumPy doit être importable en mode isolé (-I).
if(MINGW)
    # le Python de MSYS2 (dans le PATH), pas un Python inscrit au registre de Windows (python.org,
    # celui des machines de GitHub) : il est compilé pour MSVC et sa DLL n'est pas dans le PATH
    set(Python3_FIND_REGISTRY NEVER)
endif()
if(NOT Python3_EXECUTABLE AND NOT Python3_ROOT_DIR AND NOT DEFINED ENV{Python3_ROOT_DIR})
    # Plusieurs versions peuvent cohabiter. Essayer d'abord la commande usuelle
    # du poste si elle dispose déjà du NumPy qui servira aux élèves.
    find_program(MEVREG_PYTHON NAMES python3 python)
    if(MEVREG_PYTHON)
        execute_process(COMMAND "${MEVREG_PYTHON}" -I -c
            "import sys, numpy; sys.exit(sys.version_info < (3, 10))"
            RESULT_VARIABLE python_incompatible OUTPUT_QUIET ERROR_QUIET)
        if(python_incompatible STREQUAL "0")
            set(Python3_EXECUTABLE "${MEVREG_PYTHON}")
            find_package(Python3 3.10 QUIET COMPONENTS Interpreter Development.Embed)
            if(NOT Python3_FOUND)
                unset(Python3_EXECUTABLE)
            endif()
        endif()
    endif()
endif()
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter Development.Embed)
execute_process(COMMAND "${Python3_EXECUTABLE}" -I -c
    "from pathlib import Path; import numpy; print(Path(numpy.__file__).parent.as_posix())"
    OUTPUT_VARIABLE MEVREG_NUMPY_DIR OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE echec_numpy ERROR_VARIABLE erreur_numpy)
if(NOT echec_numpy STREQUAL "0")
    message(FATAL_ERROR "NumPy est inaccessible avec ${Python3_EXECUTABLE} en mode isolé. "
        "Installez NumPy pour cet interpréteur ou relancez CMake avec "
        "-DPython3_EXECUTABLE=/chemin/vers/python3.\n${erreur_numpy}")
endif()

if(WIN32)
    get_filename_component(MEVREG_PYTHON_BIN "${Python3_EXECUTABLE}" DIRECTORY)
    set_source_files_properties(src/python/engine.cpp PROPERTIES COMPILE_DEFINITIONS
        "MEVREG_PYTHON_BIN=\"${MEVREG_PYTHON_BIN}\"")
endif()

option(MEVREG_TESTS "Compiler les tests" ON)

# Cœur : données, formules, calculs, modélisation, graphes et exports, Python intégré.
# Sans fenêtre : il sert à l'application et aux tests.
add_library(mevregcore STATIC
    ../commun/ecriture_image.cpp
    src/core/format.cpp
    src/core/expr.cpp
    src/core/formules.cpp
    src/core/numeric.cpp
    src/core/series.cpp
    src/core/linalg.cpp
    src/core/spline.cpp
    src/core/fft.cpp
    src/core/stats.cpp
    src/core/titration.cpp
    src/core/fit.cpp
    src/core/document.cpp
    src/core/csv.cpp
    src/core/plotspec.cpp
    src/core/workspace.cpp
    src/core/fichier_seance.cpp
    src/core/pyformule.cpp
    src/core/pyexport.cpp
    src/core/drawing.cpp
    src/core/resources.cpp
    src/plot/ticks.cpp
    src/plot/resolve.cpp
    src/plot/renderer.cpp
    src/plot/export.cpp
    src/plot/report.cpp
    src/python/native.cpp
    src/python/engine.cpp
    src/python/identifiants.cpp
    src/python/figure.cpp
    resources.qrc
)
target_compile_definitions(mevregcore PRIVATE MEVREG_NUMPY_DIR="${MEVREG_NUMPY_DIR}")
target_include_directories(mevregcore PUBLIC src ../commun)
target_compile_definitions(mevregcore PUBLIC MEVREG_VERSION="${PROJECT_VERSION}" QT_NO_KEYWORDS)
target_link_libraries(mevregcore PUBLIC Qt6::Core Qt6::Gui Qt6::Svg Qt6::PrintSupport Qt6::Concurrent Python3::Python)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mevregcore PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

option(MEVREG_APP "Compiler l'interface" ON)

if(MEVREG_APP)
    # Interface : fenêtre principale et panneaux ; bibliothèque partagée par le programme et ses tests
    add_library(mevregui STATIC
        ../commun/coordonnees_auteur.cpp
        ../commun/presentation_demarrage.cpp
        src/app/theme.cpp
        src/app/icons.cpp
        src/app/store.cpp
        src/app/mainwindow.cpp
        src/app/mainwindow_fenetres.cpp
        src/app/mainwindow_fichiers.cpp
        src/app/mainwindow_exports.cpp
        src/app/guide.cpp
        src/app/quantitiespanel.cpp
        src/app/calculationspanel.cpp
        src/app/tableview.cpp
        src/app/graphwidget.cpp
        src/app/graphsettings.cpp
        src/app/modelingpanel.cpp
        src/app/statspanel.cpp
        src/app/scriptpanel.cpp
        src/app/drawingview.cpp
        src/app/dialogs.cpp
        src/app/csvimport.cpp
        src/app/widgets.cpp
        src/app/docking.cpp
        src/app/scripting.cpp
        src/app/about.cpp
        src/app/crash.cpp
        src/app/help.cpp
    )
    target_include_directories(mevregui PUBLIC ../commun)
    target_link_libraries(mevregui PUBLIC mevregcore Qt6::Widgets)
    # notice d'utilisation, ouverte dans le navigateur (src/app/help.cpp)
    qt_add_resources(mevregui help
        PREFIX /help
        BASE assets/help
        FILES
            assets/help/notice.html assets/help/python.html assets/help/help.css
            assets/help/residus.png assets/help/import-csv.png assets/help/tableur.png assets/help/resistance.csv
    )
    # thème du bureau suivi par le portail freedesktop (Linux)
    if(UNIX AND NOT APPLE)
        target_link_libraries(mevregui PUBLIC Qt6::DBus)
        target_compile_definitions(mevregui PRIVATE MEVREG_PORTAL)
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(mevregui PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
    endif()

    add_executable(mevreg WIN32 src/app/main.cpp)
    target_link_libraries(mevreg PRIVATE mevregui)
    if(WIN32)
        set(MEVREG_ICO "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/mevreg.ico")
        configure_file(platform/mevreg.rc.in mevreg.rc @ONLY)
        target_sources(mevreg PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/mevreg.rc)
    endif()
    install(TARGETS mevreg RUNTIME DESTINATION bin)
    # Linux : lanceur, icônes, licence ; paquets .deb et .rpm (packaging/paquets.cmake)
    set(MEVSUITE_PAQUET mevreg)
    set(MEVSUITE_RESUME "MeVReg : traitement des données des TP de physique-chimie")
    set(MEVSUITE_ICONES "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons")
    set(MEVSUITE_LICENCE "MIT")
    set(MEVSUITE_LICENCES "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE" "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/LICENSE-DejaVu.txt")
    set(MEVSUITE_DEB_DEPENDS "qt6-qpa-plugins, qt6-wayland, qt6-svg-plugins, python3-numpy")
    set(MEVSUITE_RPM_REQUIRES "qt6-qtwayland, python3-numpy")
    include("${CMAKE_CURRENT_SOURCE_DIR}/../packaging/paquets.cmake" OPTIONAL)
endif()

if(MEVREG_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    set(tests core plot python)
    if(MEVREG_APP)
        list(APPEND tests app seances)
    endif()
    foreach(t ${tests})
        add_executable(test_${t} tests/test_${t}.cpp)
        target_link_libraries(test_${t} PRIVATE mevregcore Qt6::Test)
        target_compile_definitions(test_${t} PRIVATE TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
        add_test(NAME ${t} COMMAND test_${t})
        # MALLOC_PERTURB_ (glibc) : la mémoire libérée est écrasée, un objet détruit puis encore
        # utilisé fait planter l'essai au lieu de passer par chance
        set_tests_properties(${t} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MALLOC_PERTURB_=165")
    endforeach()
    target_compile_definitions(test_core PRIVATE MEVREG_TEST_PYTHON="${Python3_EXECUTABLE}")
    if(MEVREG_APP)
        target_link_libraries(test_app PRIVATE mevregui)
        target_link_libraries(test_seances PRIVATE mevregui)
        # Les deux parcours utilisent la même configuration Qt réservée aux essais.
        set_tests_properties(app seances PROPERTIES RESOURCE_LOCK poste_mevreg)
        add_test(NAME autonomie COMMAND mevreg --selftest)
    endif()
endif()
