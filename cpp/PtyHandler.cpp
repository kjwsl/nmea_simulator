// PtyHandler.cpp
#include "PtyHandler.hpp"
#include "NmeaGenerator.hpp"

#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <pty.h>
#include <signal.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

// Initialize static member
PtyHandler* PtyHandler::instance_ = nullptr;

// Constructor
PtyHandler::PtyHandler(const std::string& pipe_path,
                       const std::string& serial_port,
                       const std::string& symlink_path,
                       double interval,
                       NmeaGenerator* generator,
                       const std::string& file_path) // Updated constructor
    : pipe_path_(pipe_path)
    , serial_port_(serial_port)
    , symlink_path_(symlink_path)
    , interval_(interval)
    , shutdown_event_(false)
    , master_fd_(-1)
    , generator_(generator)
    , file_path_(file_path) // Initialize new member
{
}

// Destructor
PtyHandler::~PtyHandler()
{
    cleanup();
}

// Static signal handler
void PtyHandler::signalHandler(int signal)
{
    if (signal == SIGINT && instance_) {
        std::cout << "\nKeyboardInterrupt received. Shutting down..." << std::endl;
        instance_->shutdown_event_.store(true);
    }
}

// Setup signal handler
void PtyHandler::setupSignalHandler()
{
    instance_ = this;
    struct sigaction sa;
    sa.sa_handler = PtyHandler::signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
}

// Start the handler
void PtyHandler::start()
{
    setupSignalHandler();

    if (!serial_port_.empty()) {
        std::cout << "Using serial port: " << serial_port_ << std::endl;
        writer_thread_ = std::thread(&PtyHandler::writerSerial, this);
    } else if (!pipe_path_.empty()) {
        setupNamedPipe();
        if (shutdown_event_.load())
            return; // Exit if setup failed
        std::cout << "Connect your GNSS-consuming application to the named pipe: " << pipe_path_
                  << std::endl;
        writer_thread_ = std::thread(&PtyHandler::writerPipe, this);
    } else {
        setupPTY();
        if (shutdown_event_.load())
            return; // Exit if setup failed
        // The setupPTY now already prints the symlink path
        writer_thread_ = std::thread(&PtyHandler::writerPTY, this);
    }

    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }

    cleanup();
}

// Setup named pipe
void PtyHandler::setupNamedPipe()
{
    if (access(pipe_path_.c_str(), F_OK) == -1) {
        if (mkfifo(pipe_path_.c_str(), 0666) != 0) {
            std::cerr << "Failed to create named pipe: " << pipe_path_ << std::endl;
            shutdown_event_.store(true);
            return;
        }
        std::cout << "Named pipe created at: " << pipe_path_ << std::endl;
    } else {
        struct stat stat_buf;
        if (stat(pipe_path_.c_str(), &stat_buf) != 0 || !S_ISFIFO(stat_buf.st_mode)) {
            std::cerr << "Path exists and is not a FIFO: " << pipe_path_.c_str() << std::endl;
            shutdown_event_.store(true);
            return;
        }
        std::cout << "Using existing named pipe: " << pipe_path_.c_str() << std::endl;
    }
}

// Setup PTY
void PtyHandler::setupPTY()
{
    char slave_name_buffer[256];
    struct termios tty;
    int slave_fd = -1; // Variable to store slave file descriptor

    // Create PTY master and slave
    if (openpty(&master_fd_, &slave_fd, slave_name_buffer, nullptr, nullptr) == -1) {
        std::cerr << "Failed to create virtual serial port: " << strerror(errno) << std::endl;
        shutdown_event_.store(true);
        return;
    }

    slave_name_ = slave_name_buffer;
    std::cout << "Virtual serial port created at: " << slave_name_ << std::endl;

    // Configure the slave PTY as a serial port
    if (tcgetattr(slave_fd, &tty) == -1) {
        std::cerr << "Failed to get terminal attributes: " << strerror(errno) << std::endl;
        close(slave_fd);
        shutdown_event_.store(true);
        return;
    }

    // Configure serial port settings (example: 9600 baud, 8N1)
    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);

    tty.c_cflag &= ~PARENB; // No parity
    tty.c_cflag &= ~CSTOPB; // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8; // 8 data bits
    tty.c_cflag &= ~CRTSCTS; // No hardware flow control
    tty.c_cflag |= CREAD | CLOCAL; // Enable receiver, ignore modem control lines

    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // Raw input
    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // No software flow control
    tty.c_oflag &= ~OPOST; // Raw output

    if (tcsetattr(slave_fd, TCSANOW, &tty) == -1) {
        std::cerr << "Failed to set terminal attributes: " << strerror(errno) << std::endl;
        close(slave_fd);
        shutdown_event_.store(true);
        return;
    }

    close(slave_fd); // Configuration done

    // Create a symbolic link for the slave PTY
    // Remove existing symlink if it exists
    if (!symlink_path_.empty()) {
        if (unlink(symlink_path_.c_str()) != 0 && errno != ENOENT) {
            std::cerr << "Warning: Failed to remove existing symbolic link: "
                      << symlink_path_ << " (" << strerror(errno) << ")" << std::endl;
        }
    }

    // Attempt to create the symlink with retries
    int retries = 3;
    while (retries > 0) {
        if (symlink(slave_name_.c_str(), symlink_path_.c_str()) == 0) {
            std::cout << "Symbolic link created at: " << symlink_path_ << std::endl;
            break;
        } else {
            std::cerr << "Failed to create symbolic link: " << symlink_path_
                      << " (" << strerror(errno) << ")" << std::endl;
            retries--;
            if (retries > 0) {
                std::cerr << "Retrying in 1 second..." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            } else {
                std::cerr << "Exceeded maximum retries. Continuing without symlink." << std::endl;
                break;
            }
        }
    }

    // Inform the user about the symlink
    std::cout << "Connect your GNSS-consuming application to the virtual serial port: "
              << symlink_path_ << std::endl;
}

// Helper function to check if a line is an RMC sentence
bool isRmcSentence(const std::string& line)
{
    // Trim leading spaces
    size_t start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return false;

    // Check if line starts with "$GPRMC" or "$GNRMC" or "$GLRMC" etc.
    return (line.compare(start, 6, "$GPRMC") == 0 || line.compare(start, 6, "$GNRMC") == 0 || line.compare(start, 6, "$GLRMC") == 0 || line.compare(start, 6, "$GRRMC") == 0 || line.compare(start, 6, "$GGRMC") == 0);
}

// Writer to named pipe
void PtyHandler::writerPipe()
{
    if (!file_path_.empty()) {
        // Mode: Read from file with cycle consideration
        std::ifstream infile(file_path_);
        if (!infile.is_open()) {
            std::cerr << "Error opening NMEA log file: " << file_path_ << std::endl;
            shutdown_event_.store(true);
            return;
        }
        std::string line;
        std::vector<std::string> cycle_buffer;

        while (!shutdown_event_.load()) {
            while (std::getline(infile, line)) {
                if (shutdown_event_.load())
                    break;
                if (line.empty())
                    continue;

                // Check if the line is an RMC sentence
                if (isRmcSentence(line)) {
                    // If buffer has data, send it as one cycle
                    if (!cycle_buffer.empty()) {
                        // Open the pipe in append mode
                        std::ofstream pipe(pipe_path_, std::ios::app);
                        if (!pipe.is_open()) {
                            std::cerr << "Error opening pipe: " << pipe_path_ << std::endl;
                            shutdown_event_.store(true);
                            break;
                        }

                        // Send all sentences in the buffer
                        for (const auto& sentence : cycle_buffer) {
                            pipe << sentence << "\r\n";
                        }
                        pipe.flush();
                        std::cout << "Sent to pipe (Cycle):\n";
                        for (const auto& sentence : cycle_buffer) {
                            std::cout << sentence << "\n";
                        }

                        // Clear the buffer for the next cycle
                        cycle_buffer.clear();

                        // Sleep for the specified interval
                        std::this_thread::sleep_for(std::chrono::duration<double>(interval_));
                    }

                    // Start a new cycle buffer with the current RMC sentence
                    cycle_buffer.push_back(line);
                } else {
                    // Add non-RMC sentences to the current cycle buffer
                    cycle_buffer.push_back(line);
                }
            }

            // After reaching EOF, reset to the beginning for continuous simulation
            infile.clear(); // Clear EOF flag
            infile.seekg(0, std::ios::beg);
        }
        infile.close();

        // Send any remaining data in the buffer upon shutdown
        if (!cycle_buffer.empty()) {
            std::ofstream pipe(pipe_path_, std::ios::app);
            if (pipe.is_open()) {
                for (const auto& sentence : cycle_buffer) {
                    pipe << sentence << "\r\n";
                }
                pipe.flush();
                std::cout << "Sent to pipe (Final Cycle):\n";
                for (const auto& sentence : cycle_buffer) {
                    std::cout << sentence << "\n";
                }
            }
        }
    } else {
        // Mode: Generate data
        while (!shutdown_event_.load()) {
            std::ofstream pipe(pipe_path_);
            if (!pipe.is_open()) {
                std::cerr << "Error opening pipe: " << pipe_path_ << std::endl;
                break;
            }
            while (!shutdown_event_.load()) {
                std::string sentences = generator_->generateAllSentences();
                pipe << sentences;
                pipe.flush();
                std::cout << "Sent to pipe:\n"
                          << sentences;
                std::this_thread::sleep_for(std::chrono::duration<double>(interval_));
            }
        }
    }
    std::cout << "Pipe writer thread exiting." << std::endl;
}

// Writer to serial port
void PtyHandler::writerSerial()
{
    if (!file_path_.empty()) {
        // Mode: Read from file with cycle consideration
        std::ifstream infile(file_path_);
        if (!infile.is_open()) {
            std::cerr << "Error opening NMEA log file: " << file_path_ << std::endl;
            shutdown_event_.store(true);
            return;
        }
        std::string line;
        std::vector<std::string> cycle_buffer;

        while (!shutdown_event_.load()) {
            while (std::getline(infile, line)) {
                if (shutdown_event_.load())
                    break;
                if (line.empty())
                    continue;

                // Check if the line is an RMC sentence
                if (isRmcSentence(line)) {
                    // If buffer has data, send it as one cycle
                    if (!cycle_buffer.empty()) {
                        int fd = open(serial_port_.c_str(), O_WRONLY | O_NOCTTY);
                        if (fd == -1) {
                            std::cerr << "Error opening serial port: " << serial_port_ << std::endl;
                            shutdown_event_.store(true);
                            break;
                        }

                        // Send all sentences in the buffer
                        for (const auto& sentence : cycle_buffer) {
                            std::string full_sentence = sentence + "\r\n";
                            ssize_t bytes_written     = write(fd, full_sentence.c_str(), full_sentence.size());
                            if (bytes_written == -1) {
                                std::cerr << "Error writing to serial port: " << serial_port_ << std::endl;
                                close(fd);
                                shutdown_event_.store(true);
                                break;
                            }
                        }
                        fsync(fd);
                        std::cout << "Sent to serial port (Cycle):\n";
                        for (const auto& sentence : cycle_buffer) {
                            std::cout << sentence << "\n";
                        }

                        close(fd);

                        // Clear the buffer for the next cycle
                        cycle_buffer.clear();

                        // Sleep for the specified interval
                        std::this_thread::sleep_for(std::chrono::duration<double>(interval_));
                    }

                    // Start a new cycle buffer with the current RMC sentence
                    cycle_buffer.push_back(line);
                } else {
                    // Add non-RMC sentences to the current cycle buffer
                    cycle_buffer.push_back(line);
                }
            }

            // After reaching EOF, reset to the beginning for continuous simulation
            infile.clear(); // Clear EOF flag
            infile.seekg(0, std::ios::beg);
        }
        infile.close();

        // Send any remaining data in the buffer upon shutdown
        if (!cycle_buffer.empty()) {
            int fd = open(serial_port_.c_str(), O_WRONLY | O_NOCTTY);
            if (fd != -1) {
                for (const auto& sentence : cycle_buffer) {
                    std::string full_sentence = sentence + "\r\n";
                    ssize_t bytes_written     = write(fd, full_sentence.c_str(), full_sentence.size());
                    if (bytes_written == -1) {
                        std::cerr << "Error writing to serial port: " << serial_port_ << std::endl;
                        break;
                    }
                }
                fsync(fd);
                std::cout << "Sent to serial port (Final Cycle):\n";
                for (const auto& sentence : cycle_buffer) {
                    std::cout << sentence << "\n";
                }
                close(fd);
            }
        }
    } else {
        // Mode: Generate data
        int fd = open(serial_port_.c_str(), O_WRONLY | O_NOCTTY);
        if (fd == -1) {
            std::cerr << "Error opening serial port: " << serial_port_ << std::endl;
            return;
        }

        while (!shutdown_event_.load()) {
            std::string sentences = generator_->generateAllSentences();
            ssize_t bytes_written = write(fd, sentences.c_str(), sentences.size());
            if (bytes_written == -1) {
                std::cerr << "Error writing to serial port: " << serial_port_ << std::endl;
                break;
            }
            fsync(fd);
            std::cout << "Sent to serial port:\n"
                      << sentences;
            std::this_thread::sleep_for(std::chrono::duration<double>(interval_));
        }
        close(fd);
        std::cout << "Serial port writer thread exiting." << std::endl;
    }
}

// Writer to PTY
void PtyHandler::writerPTY()
{
    if (!file_path_.empty()) {
        // Mode: Read from file with cycle consideration
        std::ifstream infile(file_path_);
        if (!infile.is_open()) {
            std::cerr << "Error opening NMEA log file: " << file_path_ << std::endl;
            shutdown_event_.store(true);
            return;
        }
        std::string line;
        std::vector<std::string> cycle_buffer;

        while (!shutdown_event_.load()) {
            while (std::getline(infile, line)) {
                if (shutdown_event_.load())
                    break;
                if (line.empty())
                    continue;

                // Check if the line is an RMC sentence
                if (isRmcSentence(line)) {
                    // If buffer has data, send it as one cycle
                    if (!cycle_buffer.empty()) {
                        // Send all sentences in the buffer
                        for (const auto& sentence : cycle_buffer) {
                            std::string full_sentence = sentence + "\r\n";
                            ssize_t bytes_written     = write(master_fd_, full_sentence.c_str(), full_sentence.size());
                            if (bytes_written == -1) {
                                std::cerr << "Error writing to PTY" << std::endl;
                                shutdown_event_.store(true);
                                break;
                            }
                        }
                        std::cout << "Sent to PTY (Cycle):\n";
                        for (const auto& sentence : cycle_buffer) {
                            std::cout << sentence << "\n";
                        }

                        // Clear the buffer for the next cycle
                        cycle_buffer.clear();

                        // Sleep for the specified interval
                        std::this_thread::sleep_for(std::chrono::duration<double>(interval_));
                    }

                    // Start a new cycle buffer with the current RMC sentence
                    cycle_buffer.push_back(line);
                } else {
                    // Add non-RMC sentences to the current cycle buffer
                    cycle_buffer.push_back(line);
                }
            }

            // After reaching EOF, reset to the beginning for continuous simulation
            infile.clear(); // Clear EOF flag
            infile.seekg(0, std::ios::beg);
        }
        infile.close();

        // Send any remaining data in the buffer upon shutdown
        if (!cycle_buffer.empty()) {
            for (const auto& sentence : cycle_buffer) {
                std::string full_sentence = sentence + "\r\n";
                ssize_t bytes_written     = write(master_fd_, full_sentence.c_str(), full_sentence.size());
                if (bytes_written == -1) {
                    std::cerr << "Error writing to PTY" << std::endl;
                    break;
                }
            }
            std::cout << "Sent to PTY (Final Cycle):\n";
            for (const auto& sentence : cycle_buffer) {
                std::cout << sentence << "\n";
            }
        }
    } else {
        // Mode: Generate data
        while (!shutdown_event_.load()) {
            std::string sentences = generator_->generateAllSentences();
            ssize_t bytes_written = write(master_fd_, sentences.c_str(), sentences.size());
            if (bytes_written == -1) {
                std::cerr << "Error writing to PTY" << std::endl;
                shutdown_event_.store(true);
                break;
            }
            std::cout << "Sent to PTY:\n"
                      << sentences;
            std::this_thread::sleep_for(std::chrono::duration<double>(interval_));
        }
    }
    close(master_fd_);
    std::cout << "PTY writer thread exiting." << std::endl;
}

// Cleanup resources
void PtyHandler::cleanup()
{
    if (!pipe_path_.empty() && access(pipe_path_.c_str(), F_OK) != -1) {
        if (unlink(pipe_path_.c_str()) != 0) {
            std::cerr << "Error removing named pipe: " << pipe_path_.c_str() << std::endl;
        } else {
            std::cout << "Named pipe removed: " << pipe_path_.c_str() << std::endl;
        }
    }

    if (!symlink_path_.empty()) {
        // Remove the symbolic link
        if (unlink(symlink_path_.c_str()) != 0) {
            std::cerr << "Error removing symbolic link: " << symlink_path_.c_str() << std::endl;
        } else {
            std::cout << "Symbolic link removed: " << symlink_path_.c_str() << std::endl;
        }
    }

    if (master_fd_ != -1) {
        close(master_fd_);
        std::cout << "PTY writer thread exiting." << std::endl;
    }

    std::cout << "PtyHandler exited gracefully." << std::endl;
}

// Signal shutdown
void PtyHandler::signalShutdown()
{
    shutdown_event_.store(true);
}
