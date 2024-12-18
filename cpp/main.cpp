// main.cpp
#include "NmeaSimulator.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
    std::string pipe_path    = "";
    std::string serial_port  = "";
    std::string file_path    = ""; // New variable for the NMEA log file
    double interval          = 1.0; // Default interval in seconds
    std::string symlink_path = "/tmp/ttySIMULATOR"; // Default symlink path

    // Simple command-line argument parsing
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-p" || arg == "--pipe") && i + 1 < argc) {
            pipe_path = argv[++i];
        } else if ((arg == "-s" || arg == "--serial") && i + 1 < argc) {
            serial_port = argv[++i];
        } else if ((arg == "-f" || arg == "--file") && i + 1 < argc) { // New option
            file_path = argv[++i];
        } else if ((arg == "-i" || arg == "--interval") && i + 1 < argc) {
            interval = std::stod(argv[++i]);
        } else if ((arg == "-l" || arg == "--link") && i + 1 < argc) {
            symlink_path = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  -p, --pipe <path>       Specify named pipe path\n"
                      << "  -s, --serial <port>     Specify serial port\n"
                      << "  -f, --file <path>       Specify NMEA log file path\n" // Help for new option
                      << "  -i, --interval <sec>    Specify interval between sentences (default: 1.0)\n"
                      << "  -l, --link <symlink>    Specify symbolic link path for PTY (default: /tmp/ttySIMULATOR)\n"
                      << "  -h, --help              Show this help message\n";
            return 0;
        }
    }

    // Validate that either generator or file is specified, but not both
    if (!file_path.empty() && (!pipe_path.empty() || !serial_port.empty())) {
        std::cerr << "Error: When using --file, do not specify --pipe or --serial options.\n";
        return 1;
    }

    // Initialize the simulator with the provided arguments
    NmeaSimulator simulator(pipe_path, serial_port, file_path, interval, symlink_path);
    simulator.start();

    return 0;
}
