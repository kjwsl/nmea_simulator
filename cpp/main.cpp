// main.cpp
#include "NmeaSimulator.hpp"

#include <iostream>
#include <getopt.h>
#include <cstdlib>
#include <cerrno>
#include <cstring>

int main(int argc, char *argv[])
{
    std::string pipe_path;
    std::string serial_port;
    double interval = 1.0;

    struct option long_options[] = {{"pipe", required_argument, nullptr, 'p'},
                                    {"serial", required_argument, nullptr, 's'},
                                    {"interval", required_argument, nullptr, 'i'},
                                    {nullptr, 0, nullptr, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "p:s:i:", long_options, nullptr)) != -1)
    {
        switch (opt)
        {
        case 'p':
            pipe_path = optarg;
            break;
        case 's':
            serial_port = optarg;
            break;
        case 'i':
        {
            char *endptr = nullptr;
            errno = 0; // Reset errno before the call
            double val = std::strtod(optarg, &endptr);

            // Check for conversion errors
            if (endptr == optarg)
            {
                std::cerr << "Invalid interval value: " << optarg << std::endl;
                return 1;
            }
            if (errno == ERANGE)
            {
                std::cerr << "Interval value out of range: " << optarg << std::endl;
                return 1;
            }
            // Optionally, check for trailing characters
            while (*endptr != '\0')
            {
                if (!std::isspace(*endptr))
                {
                    std::cerr << "Invalid characters in interval value: " << optarg << std::endl;
                    return 1;
                }
                ++endptr;
            }

            interval = val;
            break;
        }
        default:
            std::cerr << "Usage: " << argv[0]
                      << " [--pipe PATH] [--serial PORT] [--interval SECONDS]" << std::endl;
            return 1;
        }
    }

    NmeaSimulator simulator(pipe_path, serial_port, interval);
    simulator.start();

    return 0;
}
