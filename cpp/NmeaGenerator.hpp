// NmeaGenerator.hpp
#ifndef NMEA_GENERATOR_HPP
#define NMEA_GENERATOR_HPP

#include <chrono>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <vector>

// Enum for satellite constellations
enum class Constellation {
    GPS,
    GLONASS,
    Galileo,
    Beidou,
    QZSS
};

// Structure to hold satellite information
struct SatelliteInfo {
    int prn;
    Constellation constellation;
};

// Structure to hold location data
struct LocationData {
    std::string latitude;
    char ns;
    std::string longitude;
    char ew;
};

class NmeaGenerator {
public:
    NmeaGenerator();
    ~NmeaGenerator() = default;

    // Generate all NMEA sentences
    std::string generateAllSentences();

private:
    // Random number generation
    double randomUniform(double min, double max);
    int randomInt(int min, int max);

    // Checksum calculation
    std::string calculateChecksum(const std::string& nmea_sentence) const;

    // Time and date retrieval
    std::string getUTCTime() const;
    std::string getUTCDate() const;

    // Location generation
    LocationData generateLocation();

    // Satellite generation
    std::vector<SatelliteInfo> generateSatellites();

    // NMEA sentence generation
    std::string generateGPGGA(const LocationData& loc, int numSatellites);
    std::string generateGPRMC(const LocationData& loc);
    std::string generateGPGLL(const LocationData& loc);
    std::string generateGPGSA(const std::vector<SatelliteInfo>& satellites);
    std::string generateGxGSV(const std::vector<SatelliteInfo>& satellites, Constellation constellation);
    std::string generateNFIMU(const LocationData& loc);

    // Generate multiple GSV sentences for all constellations
    std::string generateGPGSV(const std::vector<SatelliteInfo>& satellites);

    // Random device and generator
    std::mt19937 rng_;
};

#endif // NMEA_GENERATOR_HPP
