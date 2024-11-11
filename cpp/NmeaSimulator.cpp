// NmeaSimulator.cpp
#include "NmeaSimulator.hpp"

NmeaSimulator::NmeaSimulator(const std::string& pipe_path,
                             const std::string& serial_port,
                             const std::string& file_path, // Updated constructor
                             double interval,
                             const std::string& symlink_path)
    : pipe_path_(pipe_path)
    , serial_port_(serial_port)
    , file_path_(file_path) // Initialize new member
    , interval_(interval)
    , symlink_path_(symlink_path)
    , generator_()
    , pty_handler_(pipe_path_, serial_port_, symlink_path_, interval_, &generator_, file_path_) // Pass file_path_
{
}

NmeaSimulator::~NmeaSimulator()
{
    // Destructor will automatically clean up PtyHandler
}

void NmeaSimulator::start()
{
    pty_handler_.start();
}
