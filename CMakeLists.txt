cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.cmake)
logalizer_resolve_version("${CMAKE_CURRENT_SOURCE_DIR}")
project(Logalizer VERSION ${LOGALIZER_VERSION} LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Refresh Git metadata on every build, including after commits and tag changes.
add_custom_target(logalizer_version ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DOUTPUT_FILE=${CMAKE_BINARY_DIR}/generated/version.h
        -P ${CMAKE_SOURCE_DIR}/cmake/GenerateVersion.cmake
    BYPRODUCTS ${CMAKE_BINARY_DIR}/generated/version.h
    VERBATIM)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Sql)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(Logalizer
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        appsettings.h appsettings.cpp
        fieldextractor.h fieldextractor.cpp
        logformat.h logformat.cpp
        formatdetector.h formatdetector.cpp
        settingsdialog.h settingsdialog.cpp
        logwidget.h logwidget.cpp
        linerecord.h
        fileworker.h fileworker.cpp
        processworker.h processworker.cpp
        streamworker.h streamworker.cpp
        loglinestore.h loglinestore.cpp
        loglineparser.h loglineparser.cpp
        metadataconfig.h metadataconfig.cpp
        metadatapipeline.h metadatapipeline.cpp
        logdatabase.h logdatabase.cpp
    )

else()
    if(ANDROID)
        add_library(Logalizer SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(Logalizer
            ${PROJECT_SOURCES}
        )
    endif()

endif()

target_link_libraries(Logalizer PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
add_dependencies(Logalizer logalizer_version)
target_link_libraries(Logalizer PRIVATE Qt${QT_VERSION_MAJOR}::Sql)

# Make #include "version.h" resolve to the generated file
target_include_directories(Logalizer PRIVATE ${CMAKE_BINARY_DIR}/generated)

include(CTest)
if(BUILD_TESTING AND UNIX)
    add_test(NAME version_metadata COMMAND bash ${CMAKE_SOURCE_DIR}/tests/version_tests.sh)
    add_test(NAME release_validation COMMAND bash ${CMAKE_SOURCE_DIR}/tests/release_tests.sh)
endif()
if(BUILD_TESTING AND QT_VERSION_MAJOR EQUAL 6)
    find_package(Qt6 QUIET COMPONENTS Test)
    if(TARGET Qt6::Test)
    get_target_property(LOGALIZER_TEST_SOURCES Logalizer SOURCES)
    list(REMOVE_ITEM LOGALIZER_TEST_SOURCES main.cpp mainwindow.cpp mainwindow.h)
    qt_add_executable(logalizer_live_tests tests/live_tests.cpp ${LOGALIZER_TEST_SOURCES})
    add_dependencies(logalizer_live_tests logalizer_version)
    target_include_directories(logalizer_live_tests PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
    target_link_libraries(logalizer_live_tests PRIVATE Qt6::Widgets Qt6::Sql Qt6::Test)
    add_test(NAME live_ingestion COMMAND logalizer_live_tests $<TARGET_FILE:Logalizer>)
    set_tests_properties(live_ingestion PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.luispichio.logalizer)
endif()
set_target_properties(Logalizer PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS Logalizer
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(Logalizer)
endif()

# ── Install: desktop integration ─────────────────────────────────────────────
install(FILES
    ${CMAKE_SOURCE_DIR}/pkg/logalizer.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
install(FILES
    ${CMAKE_SOURCE_DIR}/pkg/logalizer.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
)

# ── CPack packaging ───────────────────────────────────────────────────────────
# Package versions use the numeric version from the nearest Git tag.
set(CPACK_PACKAGE_NAME              "logalizer")
set(CPACK_PACKAGE_VERSION           "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR            "Luis Pichio")
set(CPACK_PACKAGE_CONTACT           "luispichio@github")
set(CPACK_PACKAGE_HOMEPAGE_URL      "https://github.com/luispichio/Logalizer")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "High-performance JSON Lines log analyzer — desktop alternative to lnav")
set(CPACK_PACKAGE_DESCRIPTION
    "Logalizer is a desktop application for analysing JSONL log files.\n"
    "It uses one in-memory SQLite FTS5 table per file for fast text search\n"
    "and pointer-based row navigation without configuration.")
set(CPACK_RESOURCE_FILE_LICENSE     "${CMAKE_SOURCE_DIR}/LICENSE")

# .deb (Debian / Ubuntu)
set(CPACK_DEBIAN_PACKAGE_SECTION    "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY   "optional")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libqt6core6 (>= 6.0), libqt6gui6 (>= 6.0), libqt6widgets6 (>= 6.0), libqt6sql6 (>= 6.0), libqt6sql6-sqlite (>= 6.0)")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS  ON)   # auto-detect remaining shared libs

# .rpm (Fedora / openSUSE)
set(CPACK_RPM_PACKAGE_LICENSE       "MIT")
set(CPACK_RPM_PACKAGE_GROUP         "Applications/Productivity")
set(CPACK_RPM_PACKAGE_REQUIRES      "qt6-qtbase >= 6.0")

# Output filename pattern:  logalizer_0.2.47_amd64.deb
set(CPACK_DEBIAN_FILE_NAME          "DEB-DEFAULT")
set(CPACK_RPM_FILE_NAME             "RPM-DEFAULT")

include(CPack)

# ── Qt Creator: package targets ───────────────────────────────────────────────
# Appear in Qt Creator → target selector (bottom-left) or Build menu.
find_program(CPACK_EXECUTABLE cpack)

if(CPACK_EXECUTABLE)
    add_custom_target(package-deb
        COMMAND ${CPACK_EXECUTABLE} -G DEB
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DEPENDS Logalizer
        COMMENT "Generating .deb package..."
        VERBATIM
    )
    add_custom_target(package-rpm
        COMMAND ${CPACK_EXECUTABLE} -G RPM
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DEPENDS Logalizer
        COMMENT "Generating .rpm package..."
        VERBATIM
    )
    add_custom_target(package-tgz
        COMMAND ${CPACK_EXECUTABLE} -G TGZ
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DEPENDS Logalizer
        COMMENT "Generating .tar.gz package..."
        VERBATIM
    )
else()
    message(WARNING "cpack not found — package-deb/rpm/tgz targets unavailable")
endif()

# ── AppImage target ───────────────────────────────────────────────────────────
# Uses the SAME qmake that Qt Creator used to configure the project,
# so linuxdeployqt bundles exactly Qt 6.9 (not the system Qt).
get_target_property(QT_QMAKE_EXECUTABLE Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)

add_custom_target(package-appimage
    COMMAND bash
        ${CMAKE_SOURCE_DIR}/scripts/make-appimage.sh
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}
        ${QT_QMAKE_EXECUTABLE}
        ${CPACK_PACKAGE_VERSION}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS Logalizer
    COMMENT "Generating AppImage (bundles Qt ${Qt${QT_VERSION_MAJOR}_VERSION})..."
    VERBATIM
)
