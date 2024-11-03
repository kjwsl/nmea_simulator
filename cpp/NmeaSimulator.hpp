// NmeaSimulator.h
#ifndef NMEASIMULATOR_HPP
#define NMEASIMULATOR_HPP

#include <atomic>
#include <random>
#include <string>
#include <thread>

// Forward declarations for PTY
struct termios;
struct winsize;
enum class Constellation {
    GPS,
    GLONASS,
    Galileo,
    Beidou,
    QZSS
};
struct SatelliteInfo {
    int prn;
    Constellation constellation;
};

class NmeaSimulator {
public:
    // Constructor
    NmeaSimulator(const std::string& pipe_path, const std::string& serial_port, double interval);

    // Destructor
    ~NmeaSimulator();

    // Start the simulator
    void start();

private:
    // Configuration parameters
    std::string pipe_path_;
    std::string serial_port_;
    double interval_;

    // Thread and synchronization
    std::thread writer_thread_;
    std::atomic<bool> shutdown_event_;

    // Random number generator
    std::mt19937 rng_;

    // PTY variables
    int master_fd_;
    std::string slave_name_;

    // Signal handling
    static NmeaSimulator* instance_;
    static void signalHandler(int signal);
    void setupSignalHandler();

    // Setup methods
    void setupNamedPipe();
    void setupPTY();

    // Cleanup resources
    void cleanup();

    // Checksum calculation
    std::string calculateChecksum(const std::string& nmea_sentence) const;

    // Random number generators
    double randomUniform(double min, double max);
    int randomInt(int min, int max);

    // Generate shared location data
    struct LocationData {
        std::string latitude; // ddmm.mmmm
        char ns; // 'N' or 'S'
        std::string longitude; // dddmm.mmmm
        char ew; // 'E' or 'W'
    };

    LocationData generateLocation();

    // Generate time and date strings
    std::string getUTCTime() const;
    std::string getUTCDate() const;

    // Generate NMEA sentences
    std::string generateGPGGA(const LocationData& loc, int num_satellites);
    std::string generateGPRMC(const LocationData& loc);
    std::string generateGPGLL(const LocationData& loc);
    std::string generateGPGSA(int num_satellites);
    std::string generateGPGSV();
    std::string generateNFIMU(const LocationData& loc);
    std::vector<SatelliteInfo> generateSatellites();
    std::string generateGxGSV(const std::vector<SatelliteInfo>& satellites, Constellation constellation);
    std::string generateGPGSA(const std::vector<SatelliteInfo>& satellites);

    // Aggregate all NMEA sentences
    std::string generateAllSentences();

    // Writer functions
    void serialWriterPipe(const std::string& pipe_path, double interval);
    void serialWriterSerial(const std::string& serial_port, double interval);
    void serialWriterPTY(int master_fd, double interval);
};

#endif // NMEASIMULATOR_HPP
