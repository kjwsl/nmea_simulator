// PtyHandler.hpp
#ifndef PTY_HANDLER_HPP
#define PTY_HANDLER_HPP

#include <atomic>
#include <functional>
#include <string>
#include <thread>

// Forward declaration of NmeaGenerator
class NmeaGenerator;

class PtyHandler {
public:
    PtyHandler(const std::string& pipe_path,
               const std::string& serial_port,
               const std::string& symlink_path,
               double interval,
               NmeaGenerator* generator,
               const std::string& file_path = ""); // New parameter with default value
    ~PtyHandler();

    // Start the PTY or named pipe or serial port writer
    void start();

    // Signal handler to initiate shutdown
    void signalShutdown();

private:
    // Setup methods
    void setupSignalHandler();
    void setupNamedPipe();
    void setupPTY();

    // Cleanup resources
    void cleanup();

    // Writer methods
    void writerPipe();
    void writerSerial();
    void writerPTY();

    // Member variables
    std::string pipe_path_;
    std::string serial_port_;
    std::string symlink_path_;
    double interval_;
    std::atomic<bool> shutdown_event_;
    std::thread writer_thread_;
    int master_fd_;
    std::string slave_name_;
    std::string file_path_; // New member variable

    // Pointer to NmeaGenerator
    NmeaGenerator* generator_;

    // Static instance pointer for signal handling
    static PtyHandler* instance_;

    // Static signal handler
    static void signalHandler(int signal);
};

#endif // PTY_HANDLER_HPP
