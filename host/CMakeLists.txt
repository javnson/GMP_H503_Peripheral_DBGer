cmake_minimum_required(VERSION 3.16)
project(gmp_peripheral_iso_host VERSION 0.1.0 LANGUAGES C CXX)
if(MSVC)
  # Ship the CLI/DLL without a separate Visual C++ redistributable install.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(gmp_host SHARED src/gmp_host.cpp)
target_include_directories(gmp_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(gmp_host PRIVATE GMP_HOST_BUILD)
if(WIN32)
  target_link_libraries(gmp_host PRIVATE setupapi)
endif()
add_library(gmp_host_static STATIC src/gmp_host.cpp)
target_include_directories(gmp_host_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(gmp_host_static PUBLIC GMP_HOST_STATIC)
set_target_properties(gmp_host_static PROPERTIES OUTPUT_NAME gmp_host_static)
add_executable(gmpctl src/gmpctl.cpp src/gmp_standard_cli.cpp)
target_include_directories(gmpctl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(gmpctl PRIVATE gmp_host)
if(UNIX AND NOT APPLE)
  set_target_properties(gmpctl PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
endif()
install(TARGETS gmp_host gmp_host_static gmpctl
  RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
install(FILES include/gmp_host.h DESTINATION include)
include(CTest)
if(BUILD_TESTING)
  add_executable(bridge_registers_test ../tests/bridge_registers_test.c
    ../H503_Peripheral_DBGer/Core/Src/bridge_registers.c)
  target_include_directories(bridge_registers_test PRIVATE
    ../H503_Peripheral_DBGer/Core/Inc)
  add_test(NAME bridge_registers COMMAND bridge_registers_test)
  add_executable(bridge_i2c_target_test ../tests/bridge_i2c_target_test.c
    ../H503_Peripheral_DBGer/Core/Src/bridge_i2c_target.c
    ../H503_Peripheral_DBGer/Core/Src/bridge_eeprom.c
    ../H503_Peripheral_DBGer/Core/Src/bridge_registers.c)
  target_include_directories(bridge_i2c_target_test PRIVATE
    ../tests/mock ../H503_Peripheral_DBGer/Core/Inc)
  add_test(NAME bridge_i2c_target COMMAND bridge_i2c_target_test)
  add_executable(bridge_eeprom_test ../tests/bridge_eeprom_test.c
    ../H503_Peripheral_DBGer/Core/Src/bridge_eeprom.c)
  target_include_directories(bridge_eeprom_test PRIVATE
    ../tests/mock ../H503_Peripheral_DBGer/Core/Inc)
  add_test(NAME bridge_eeprom COMMAND bridge_eeprom_test)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME cli_standard COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/../tests/cli_standard_test.py
      $<TARGET_FILE:gmpctl>)
  endif()
  if(UNIX)
    if(Python3_Interpreter_FOUND)
      add_test(NAME host_loopback COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/host_loopback.py
        $<TARGET_FILE:gmpctl>)
    endif()
  endif()
endif()
