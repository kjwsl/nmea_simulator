project(NmeaSimulator)
add_executable(nmea_simulator NmeaSimulator.cpp main.cpp)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_custom_target(
  run
  COMMAND socat -d -d pty,raw,echo=0,link=/tmp/gps_input
          pty,raw,echo=0,link=/tmp/gps_output &
  COMMAND sleep 1
  COMMAND ./nmea_simulator --serial /tmp/gps_input
  DEPENDS nmea_simulator
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
