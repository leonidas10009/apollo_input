cmake_minimum_required(VERSION 3.20)

project(apollo-input VERSION 0.2.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(APOLLO_INPUT_BUILD_TESTS "Build apollo-input tests" ON)
option(APOLLO_INPUT_BUILD_EXAMPLES "Build apollo-input examples" ON)
option(APOLLO_INPUT_BUILD_TOOLS "Build apollo-input diagnostic tools" ON)
option(APOLLO_INPUT_WITH_VIGEM "Enable the ViGEmBus backend on Windows" ON)

set(APOLLO_INPUT_VIGEMCLIENT_DIR "" CACHE PATH
    "Path to the ViGEmClient source tree. Leave empty to use ../../third-party/ViGEmClient when present.")

set(APOLLO_INPUT_SOURCES
    src/core/backend.cpp
    src/core/gamepad.cpp
    src/core/null_backend.cpp
)

if(WIN32)
    list(APPEND APOLLO_INPUT_SOURCES src/backends/windows/vigem_backend.cpp)
elseif(UNIX AND NOT APPLE)
    list(APPEND APOLLO_INPUT_SOURCES src/backends/linux/uinput_backend.cpp)
endif()

add_library(apollo-input ${APOLLO_INPUT_SOURCES})
add_library(apollo::input ALIAS apollo-input)
set_target_properties(apollo-input PROPERTIES EXPORT_NAME input)

target_compile_features(apollo-input PUBLIC cxx_std_20)
target_include_directories(apollo-input
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(WIN32 AND APOLLO_INPUT_WITH_VIGEM)
    set(_apollo_input_vigem_ready FALSE)

    if(TARGET ViGEmClient::ViGEmClient)
        set(_apollo_input_vigem_ready TRUE)
    else()
        if(NOT APOLLO_INPUT_VIGEMCLIENT_DIR)
            # Standalone checkout: third-party/ViGEmClient at the repository root.
            # Nested Apollo checkout: libs/apollo-input -> ../../third-party/ViGEmClient.
            set(_apollo_input_default_vigem_candidates
                "${CMAKE_CURRENT_SOURCE_DIR}/third-party/ViGEmClient"
                "${CMAKE_CURRENT_SOURCE_DIR}/../../third-party/ViGEmClient")
            foreach(_candidate IN LISTS _apollo_input_default_vigem_candidates)
                if(EXISTS "${_candidate}/CMakeLists.txt")
                    set(APOLLO_INPUT_VIGEMCLIENT_DIR "${_candidate}")
                    break()
                endif()
            endforeach()
        endif()

        if(APOLLO_INPUT_VIGEMCLIENT_DIR AND EXISTS "${APOLLO_INPUT_VIGEMCLIENT_DIR}/CMakeLists.txt")
            add_subdirectory("${APOLLO_INPUT_VIGEMCLIENT_DIR}"
                "${CMAKE_CURRENT_BINARY_DIR}/vigemclient")
            set(_apollo_input_vigem_ready TRUE)
        endif()
    endif()

    if(_apollo_input_vigem_ready)
        target_compile_definitions(apollo-input PRIVATE APOLLO_INPUT_WITH_VIGEM=1)
        target_link_libraries(apollo-input PRIVATE ViGEmClient::ViGEmClient setupapi)
    else()
        message(WARNING "apollo-input: ViGEmClient not found; the ViGEmBus backend will be disabled")
    endif()
endif()

if(APOLLO_INPUT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(APOLLO_INPUT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(APOLLO_INPUT_BUILD_TOOLS AND WIN32)
    add_subdirectory(tools)
endif()

install(TARGETS apollo-input
    EXPORT ApolloInputTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

set(APOLLO_INPUT_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/apollo-input")
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/apollo-input-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/apollo-input-config.cmake"
    INSTALL_DESTINATION "${APOLLO_INPUT_CMAKE_DIR}"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/apollo-input-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/apollo-input-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/apollo-input-config-version.cmake"
    DESTINATION "${APOLLO_INPUT_CMAKE_DIR}"
)
install(EXPORT ApolloInputTargets
    NAMESPACE apollo::
    FILE apollo-input-targets.cmake
    DESTINATION "${APOLLO_INPUT_CMAKE_DIR}"
)
