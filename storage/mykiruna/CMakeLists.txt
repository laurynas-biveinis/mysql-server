# Copyright 2022-2023 Laurynas Biveinis

SET(CMAKE_CXX_STANDARD 17)

# Change to system-packaged corrosion after it makes a release with
# corrosion_add_cxx_bridge
include(FetchContent)

FetchContent_Declare(
  Corrosion
  GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git)
FetchContent_MakeAvailable(Corrosion)

corrosion_import_crate(MANIFEST_PATH kirunadb/Cargo.toml)
corrosion_add_cxxbridge(kirunadb_cxx CRATE kirunadb FILES ffi_cxx.rs)

add_subdirectory(fmt)

MYSQL_ADD_PLUGIN(mykiruna ha_mykiruna.cc STORAGE_ENGINE)

target_link_libraries(mykiruna kirunadb_cxx)
target_link_libraries(mykiruna fmt)
# Should not be needed to the best of my CMake knowledge but oh well
target_include_directories(mykiruna PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/kirunadb/target/cxxbridge)
