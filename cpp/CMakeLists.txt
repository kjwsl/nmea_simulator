project(NmeaSimulator)
add_executable(nmea_simulator main.cpp NmeaGenerator.cpp NmeaSimulator.cpp
                              PtyHandler.cpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
target_link_libraries(nmea_simulator pthread util)

add_custom_target(
  run
  COMMAND ./nmea_simulator --link /tmp/ttyGPS
  DEPENDS nmea_simulator
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_custom_target(
  run_file
  COMMAND ./nmea_simulator --file ${CMAKE_CURRENT_SOURCE_DIR}/nmea.log
  DEPENDS nmea_simulator
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# add_custom_target( run COMMAND socat -d -d pty,raw,echo=0,link=/tmp/gps_input
# pty,raw,echo=0,link=/tmp/gps_output & COMMAND sleep 1 COMMAND ./nmea_simulator
# --serial /tmp/gps_input DEPENDS nmea_simulator WORKING_DIRECTORY
# ${CMAKE_CURRENT_BINARY_DIR})
