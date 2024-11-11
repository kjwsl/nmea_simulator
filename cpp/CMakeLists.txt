project(NmeaSimulator)
add_executable(nmea_simulator main.cpp NmeaGenerator.cpp NmeaSimulator.cpp
                              PtyHandler.cpp)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
target_link_libraries(nmea_simulator pthread util)

add_custom_target(
  run
  COMMAND ./nmea_simulator --link /tmp/ttyGPS
  DEPENDS nmea_simulator
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# add_custom_target( run COMMAND socat -d -d pty,raw,echo=0,link=/tmp/gps_input
# pty,raw,echo=0,link=/tmp/gps_output & COMMAND sleep 1 COMMAND ./nmea_simulator
# --serial /tmp/gps_input DEPENDS nmea_simulator WORKING_DIRECTORY
# ${CMAKE_CURRENT_BINARY_DIR})
