cmake_minimum_required(VERSION 3.16)

project(gps_tracker_host_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()
find_program(NODE_EXECUTABLE node REQUIRED)

set(CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/GpsTrackerCore")

function(configure_gps_test target)
  target_include_directories(${target} PRIVATE "${CORE_DIR}")
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME ${target} COMMAND ${target})
endfunction()

add_executable(
  test_nmea_parser
  "${CMAKE_CURRENT_LIST_DIR}/../test/test_nmea_parser/test_main.cpp"
  "${CORE_DIR}/NmeaParser.cpp"
)
configure_gps_test(test_nmea_parser)

add_executable(
  test_activity_tracker
  "${CMAKE_CURRENT_LIST_DIR}/../test/test_activity_tracker/test_main.cpp"
  "${CORE_DIR}/ActivityTracker.cpp"
  "${CORE_DIR}/NmeaParser.cpp"
)
configure_gps_test(test_activity_tracker)

add_executable(
  test_oled_layout
  "${CMAKE_CURRENT_LIST_DIR}/../test/test_oled_layout/test_main.cpp"
  "${CORE_DIR}/ActivityTracker.cpp"
  "${CORE_DIR}/NmeaParser.cpp"
  "${CORE_DIR}/OledFont.cpp"
  "${CORE_DIR}/OledLayout.cpp"
)
configure_gps_test(test_oled_layout)

add_executable(
  test_status_json
  "${CMAKE_CURRENT_LIST_DIR}/../test/test_status_json/test_main.cpp"
  "${CORE_DIR}/ActivityTracker.cpp"
  "${CORE_DIR}/NmeaParser.cpp"
  "${CORE_DIR}/StatusJson.cpp"
)
configure_gps_test(test_status_json)

add_test(
  NAME test_web_page
  COMMAND ${NODE_EXECUTABLE}
          "${CMAKE_CURRENT_LIST_DIR}/../test/test_web_page/test_main.mjs"
)
