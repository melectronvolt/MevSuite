cmake_minimum_required(VERSION 3.21)

project(MeVReg VERSION 0.2.0 DESCRIPTION "Traitement de données pour les TP de physique-chimie" LANGUAGES CXX)

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
# Python est intégré au programme : interpréteur CPython, sans site-packages
if(MINGW)
    # le Python de MSYS2 (dans le PATH), pas un Python inscrit au registre de Windows (python.org,
    # celui des machines de GitHub) : il est compilé pour MSVC et sa DLL n'est pas dans le PATH
    set(Python3_FIND_REGISTRY NEVER)
endif()
find_package(Python3 3.10 REQUIRED COMPONENTS Development.Embed)

option(MEVREG_TESTS "Compiler les tests" ON)

# Cœur : données, formules, calculs, modélisation, graphes et exports, Python intégré.
# Sans fenêtre : il sert à l'application et aux tests.
add_library(mevregcore STATIC
    src/core/format.cpp
    src/core/expr.cpp
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
    src/core/pyexport.cpp
    src/core/drawing.cpp
    src/core/resources.cpp
    src/plot/ticks.cpp
    src/plot/resolve.cpp
    src/plot/renderer.cpp
    src/plot/export.cpp
    src/plot/report.cpp
    src/python/arr.cpp
    src/python/npformat.cpp
    src/python/native.cpp
    src/python/engine.cpp
    src/python/figure.cpp
    resources.qrc
)
target_include_directories(mevregcore PUBLIC src)
target_compile_definitions(mevregcore PUBLIC MEVREG_VERSION="${PROJECT_VERSION}" QT_NO_KEYWORDS)
target_link_libraries(mevregcore PUBLIC Qt6::Core Qt6::Gui Qt6::Svg Qt6::PrintSupport Qt6::Concurrent Python3::Python)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mevregcore PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

option(MEVREG_APP "Compiler l'interface" ON)

if(MEVREG_APP)
    # Interface : fenêtre principale et panneaux ; bibliothèque partagée par le programme et ses tests
    add_library(mevregui STATIC
        src/app/theme.cpp
        src/app/icons.cpp
        src/app/store.cpp
        src/app/mainwindow.cpp
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
        src/app/pyexamples.cpp
        src/app/widgets.cpp
        src/app/docking.cpp
        src/app/scripting.cpp
        src/app/examples.cpp
        src/app/about.cpp
        src/app/help.cpp
    )
    target_link_libraries(mevregui PUBLIC mevregcore Qt6::Widgets)
    # notice d'utilisation, ouverte dans le navigateur (src/app/help.cpp)
    qt_add_resources(mevregui help
        PREFIX /help
        BASE assets/help
        FILES
            assets/help/notice.html assets/help/python.html assets/help/help.css
            assets/help/accueil.jpg assets/help/fenetre.jpg assets/help/tableur.jpg assets/help/import-csv.jpg
            assets/help/graphe.jpg assets/help/modele.jpg assets/help/python.jpg
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
    set(MEVSUITE_DEB_DEPENDS "qt6-qpa-plugins, qt6-wayland, qt6-svg-plugins")
    set(MEVSUITE_RPM_REQUIRES "qt6-qtwayland")
    include("${CMAKE_CURRENT_SOURCE_DIR}/../packaging/paquets.cmake" OPTIONAL)
endif()

if(MEVREG_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    set(tests core plot python)
    if(MEVREG_APP)
        list(APPEND tests app)
    endif()
    foreach(t ${tests})
        add_executable(test_${t} tests/test_${t}.cpp)
        target_link_libraries(test_${t} PRIVATE mevregcore Qt6::Test)
        target_compile_definitions(test_${t} PRIVATE TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
        add_test(NAME ${t} COMMAND test_${t})
        set_tests_properties(${t} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
    if(MEVREG_APP)
        target_link_libraries(test_app PRIVATE mevregui)
    endif()
endif()
