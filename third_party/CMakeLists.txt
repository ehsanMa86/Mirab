# @mirab-header Fetches and builds third-party libraries used by Mirab (SQLite).
include(FetchContent)

if(MIRAB_WITH_GEOPACKAGE)
    # SQLite is in the public domain; it backs the GeoPackage exporter.
    FetchContent_Declare(mirab_sqlite_src
        URL      https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip
        URL_HASH SHA256=1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d
        SOURCE_SUBDIR do-not-add
        DOWNLOAD_EXTRACT_TIMESTAMP ON)
    FetchContent_MakeAvailable(mirab_sqlite_src)

    add_library(mirab_sqlite STATIC "${mirab_sqlite_src_SOURCE_DIR}/sqlite3.c")
    add_library(Mirab::sqlite ALIAS mirab_sqlite)
    target_include_directories(mirab_sqlite SYSTEM PUBLIC "${mirab_sqlite_src_SOURCE_DIR}")
    target_compile_definitions(mirab_sqlite PRIVATE
        SQLITE_OMIT_LOAD_EXTENSION SQLITE_THREADSAFE=1 SQLITE_DEFAULT_FOREIGN_KEYS=1)
    if(UNIX)
        find_package(Threads REQUIRED)
        target_link_libraries(mirab_sqlite PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
    endif()
endif()
