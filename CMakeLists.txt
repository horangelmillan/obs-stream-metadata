cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

# T-047: distributed Twitch Client ID (public per Twitch docs; DCF needs
# no secret). Empty default = today's behavior (user pastes BYO ID).
# Release builds set it; the VALUE never belongs in the repo. Only
# [A-Za-z0-9] accepted (fail-fast against injection via build flags).
set(OBS_TWITCH_CLIENT_ID "" CACHE STRING
    "Distributed Twitch Client ID for Independent DCF (public, no secret)")
if(OBS_TWITCH_CLIENT_ID AND NOT OBS_TWITCH_CLIENT_ID MATCHES "^[A-Za-z0-9]+$")
  message(FATAL_ERROR "OBS_TWITCH_CLIENT_ID must be alphanumeric or empty")
endif()

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core Network)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
  if(WIN32)
    # T-032: DPAPI (CryptProtectData) for the encrypted account store.
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE crypt32)
  endif()
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  # T-047: distributed ID visible to metadata.cpp (empty string when unset).
  target_compile_definitions(
    ${CMAKE_PROJECT_NAME}
    PRIVATE OBS_TWITCH_CLIENT_ID="${OBS_TWITCH_CLIENT_ID}"
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.c src/dock-poc.cpp src/metadata.cpp
                                                     src/secure_store.cpp src/metadata_dock.cpp
                                                     src/backend_auth.cpp)

if(ENABLE_QT)
  add_subdirectory(poc/t030)

  # T-031 product selfcheck (QtCore only, on demand).
  # T-032: also covers backoff/revoke policy + DPAPI store round-trip.
  add_executable(metadata-selfcheck EXCLUDE_FROM_ALL src/metadata_selfcheck.cpp src/metadata.cpp
                                                     src/secure_store.cpp)
  target_link_libraries(metadata-selfcheck PRIVATE Qt6::Core)
  # T-047: deterministic fake distributed ID for twitchClientId tests
  # (never a real credential; the plugin default stays empty).
  target_compile_definitions(metadata-selfcheck PRIVATE
    OBS_TWITCH_CLIENT_ID="fk-dist-tw-id-9z")
  if(WIN32)
    target_link_libraries(metadata-selfcheck PRIVATE crypt32)
  endif()
  target_include_directories(metadata-selfcheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

  # T-048: console live test for the C++ Managed wiring (no OBS/Widgets).
  # Exercises the real backend_auth client against a real backend + provider.
  # Dev-only, headless-capable; never part of the plugin or the installer.
  add_executable(managed-link-test EXCLUDE_FROM_ALL src/managed_link_test.cpp src/backend_auth.cpp
                                                    src/secure_store.cpp src/metadata.cpp)
  target_link_libraries(managed-link-test PRIVATE Qt6::Core Qt6::Network)
  set_target_properties(managed-link-test PROPERTIES AUTOMOC ON)
  if(WIN32)
    target_link_libraries(managed-link-test PRIVATE crypt32)
  endif()
  target_include_directories(managed-link-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()

if(WIN32 AND ENABLE_QT)
  # OBS's own Qt runtime ships no TLS backend plugin: HTTPS from the dock
  # fails without it (F-027). Bundle Schannel's backend from our pinned
  # obs-deps Qt next to our data dir; the dock adds it via addLibraryPath.
  get_target_property(_qt_core_loc Qt6::Core IMPORTED_LOCATION_RELEASE)
  if(NOT _qt_core_loc)
    get_target_property(_qt_core_loc Qt6::Core IMPORTED_LOCATION)
  endif()
  if(_qt_core_loc)
    get_filename_component(_qt_bin "${_qt_core_loc}" DIRECTORY)
    set(_qt_tls_dll "${_qt_bin}/../plugins/tls/qschannelbackend.dll")
    if(EXISTS "${_qt_tls_dll}")
      install(FILES "${_qt_tls_dll}" DESTINATION "${CMAKE_PROJECT_NAME}/data/tls")
      message(STATUS "Bundling Qt TLS backend: ${_qt_tls_dll}")
    else()
      message(WARNING "Qt TLS backend not found at ${_qt_tls_dll}, HTTPS will fail at runtime")
    endif()
  endif()
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# T-057: OBS-root-relative packaging layout for the NSIS installer.
# Separate COMPONENT (obs-package) so the template dev flow (bin/64bit +
# OBS_PLUGINS_PATH) stays untouched:
#   cmake --install <build> --config <cfg> --prefix <staging> --component obs-package
# produces:
#   <staging>/obs-stream-metadata/obs-plugins/64bit/obs-stream-metadata.dll
#   <staging>/obs-stream-metadata/data/obs-plugins/obs-stream-metadata/locale/en-US.ini
#   <staging>/obs-stream-metadata/data/obs-plugins/obs-stream-metadata/tls/qschannelbackend.dll
# Only these three files; no PDBs, no dev tools, no secrets.
if(WIN32 AND ENABLE_QT)
  install(TARGETS ${CMAKE_PROJECT_NAME}
    RUNTIME DESTINATION "obs-stream-metadata/obs-plugins/64bit"
    LIBRARY DESTINATION "obs-stream-metadata/obs-plugins/64bit"
    COMPONENT obs-package)
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/locale/en-US.ini"
    DESTINATION "obs-stream-metadata/data/obs-plugins/obs-stream-metadata/locale"
    COMPONENT obs-package)
  if(_qt_tls_dll AND EXISTS "${_qt_tls_dll}")
    install(FILES "${_qt_tls_dll}"
      DESTINATION "obs-stream-metadata/data/obs-plugins/obs-stream-metadata/tls"
      COMPONENT obs-package)
  endif()
  # T-057: generate the NSIS script with the buildspec version so the
  # installer filename/registry never drift from the project version.
  set(PLUGIN_NAME "${CMAKE_PROJECT_NAME}")
  set(PLUGIN_VERSION "${_version}")
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows-installer.nsi.in"
    "${CMAKE_CURRENT_BINARY_DIR}/windows-installer.nsi" @ONLY)
endif()
