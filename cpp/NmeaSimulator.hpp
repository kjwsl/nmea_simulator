// NmeaSimulator.hpp
#ifndef NMEA_SIMULATOR_HPP
#define NMEA_SIMULATOR_HPP

#include "NmeaGenerator.hpp"
#include "PtyHandler.hpp"
#include <string>

class NmeaSimulator {
public:
    NmeaSimulator(const std::string& pipe_path,
                  const std::string& serial_port,
                  const std::string& file_path, // New parameter
                  double interval,
                  const std::string& symlink_path);
    ~NmeaSimulator();

    // Start the simulator
    void start();

private:
    std::string pipe_path_;
    std::string serial_port_;
    std::string file_path_; // New member variable
    double interval_;
    std::string symlink_path_;

    NmeaGenerator generator_;
    PtyHandler pty_handler_;
};

#endif // NMEA_SIMULATOR_HPP
