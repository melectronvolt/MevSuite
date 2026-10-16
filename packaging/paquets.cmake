# Installation sous Linux (lanceur, icônes, licence) et paquets .deb et .rpm (CPack), communs aux
# quatre logiciels. Le CMakeLists.txt du logiciel donne, avant d'inclure ce fichier :
#
#   MEVSUITE_PAQUET        nom du paquet, du programme, du lanceur et de l'icône (mevpix…)
#   MEVSUITE_RESUME        description en une ligne
#   MEVSUITE_ICONES        dossier des icônes <paquet>-<taille>.png et <paquet>.svg
#   MEVSUITE_LICENCE       licence (identifiant SPDX)
#   MEVSUITE_LICENCES      fichiers de licence installés dans share/doc/<paquet>
#   MEVSUITE_DEB_DEPENDS   paquets .deb nécessaires en plus des bibliothèques (greffons de Qt…)
#   MEVSUITE_RPM_REQUIRES  de même pour le .rpm
#
# Les bibliothèques (Qt, Python…) sont ajoutées aux dépendances par dpkg-shlibdeps et rpmbuild.
# Fabrication (packaging/paquet-linux.sh) :
#
#   cmake -S <logiciel> -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
#   cmake --build build && cd build && cpack -G DEB      (ou -G RPM)

if(NOT UNIX OR APPLE)
    return()
endif()

install(FILES platform/${MEVSUITE_PAQUET}.desktop DESTINATION share/applications)
set(_mevsuite_dossiers_icones /usr/share/icons /usr/share/icons/hicolor /usr/share/icons/hicolor/scalable
    /usr/share/icons/hicolor/scalable/apps)
foreach(taille 16 32 48 64 128 256)
    set(_icone "${MEVSUITE_ICONES}/${MEVSUITE_PAQUET}-${taille}.png")
    if(EXISTS "${_icone}")
        install(FILES "${_icone}" DESTINATION share/icons/hicolor/${taille}x${taille}/apps RENAME ${MEVSUITE_PAQUET}.png)
        list(APPEND _mevsuite_dossiers_icones /usr/share/icons/hicolor/${taille}x${taille}
            /usr/share/icons/hicolor/${taille}x${taille}/apps)
    endif()
endforeach()
install(FILES "${MEVSUITE_ICONES}/${MEVSUITE_PAQUET}.svg" DESTINATION share/icons/hicolor/scalable/apps)
install(FILES ${MEVSUITE_LICENCES} DESTINATION share/doc/${MEVSUITE_PAQUET})

set(CPACK_PACKAGE_NAME ${MEVSUITE_PAQUET})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "Rémi MEVAERE")
set(CPACK_PACKAGE_CONTACT "Rémi MEVAERE <suite@sciences-physiques.net>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${MEVSUITE_RESUME}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/melectronvolt/MevSuite")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_STRIP_FILES ON)

# .deb (Ubuntu, Debian) : <paquet>_<version>_amd64.deb
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION science)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "${MEVSUITE_DEB_DEPENDS}")

# .rpm (Fedora) : <paquet>-<version>-1.<fcNN>.x86_64.rpm
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_RELEASE_DIST ON)
set(CPACK_RPM_PACKAGE_LICENSE "${MEVSUITE_LICENCE}")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Engineering")
set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_RPM_PACKAGE_REQUIRES "${MEVSUITE_RPM_REQUIRES}")
# dossiers du système : à ne pas réclamer (conflit avec le paquet « filesystem »)
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION /usr/share/applications /usr/share/doc ${_mevsuite_dossiers_icones})

include(CPack)
