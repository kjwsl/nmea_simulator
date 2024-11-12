// NmeaGenerator.cpp
#include "NmeaGenerator.hpp"

#include <iomanip>
#include <sstream>

// Constructor
NmeaGenerator::NmeaGenerator()
    : rng_(std::random_device {}())
{
}

// Checksum calculation
std::string NmeaGenerator::calculateChecksum(const std::string& nmea_sentence) const
{
    uint8_t checksum = 0;
    for (char ch : nmea_sentence) {
        checksum ^= static_cast<uint8_t>(ch);
    }
    std::stringstream ss;
    ss << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << int(checksum);
    return ss.str();
}

// Random uniform double
double NmeaGenerator::randomUniform(double min, double max)
{
    std::uniform_real_distribution<double> dist(min, max);
    return dist(rng_);
}

// Random integer
int NmeaGenerator::randomInt(int min, int max)
{
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng_);
}

// Generate current UTC time string in HHMMSS format
std::string NmeaGenerator::getUTCTime() const
{
    auto now        = std::chrono::system_clock::now();
    std::time_t t_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm      = *std::gmtime(&t_c);
    char utc_time[16];
    std::strftime(utc_time, sizeof(utc_time), "%H%M%S", &tm);
    return std::string(utc_time);
}

// Generate current UTC date string in DDMMYY format
std::string NmeaGenerator::getUTCDate() const
{
    auto now        = std::chrono::system_clock::now();
    std::time_t t_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm      = *std::gmtime(&t_c);
    char date_str[16];
    std::strftime(date_str, sizeof(date_str), "%d%m%y", &tm);
    return std::string(date_str);
}

// Generate shared location data
LocationData NmeaGenerator::generateLocation()
{
    LocationData loc;

    // Latitude: -90 to 90
    double latitude = randomUniform(-90.0, 90.0);
    loc.ns          = (latitude >= 0) ? 'N' : 'S';
    latitude        = std::abs(latitude);
    int lat_deg     = static_cast<int>(latitude);
    double lat_min  = (latitude - lat_deg) * 60.0;
    std::ostringstream lat_ss;
    lat_ss << std::setw(2) << std::setfill('0') << lat_deg << std::fixed << std::setprecision(4)
           << lat_min;
    loc.latitude = lat_ss.str();

    // Longitude: -180 to 180
    double longitude = randomUniform(-180.0, 180.0);
    loc.ew           = (longitude >= 0) ? 'E' : 'W';
    longitude        = std::abs(longitude);
    int lon_deg      = static_cast<int>(longitude);
    double lon_min   = (longitude - lon_deg) * 60.0;
    std::ostringstream lon_ss;
    lon_ss << std::setw(3) << std::setfill('0') << lon_deg << std::fixed << std::setprecision(4)
           << lon_min;
    loc.longitude = lon_ss.str();

    return loc;
}

// Generate satellites with different constellations
std::vector<SatelliteInfo> NmeaGenerator::generateSatellites()
{
    std::vector<SatelliteInfo> satellites;

    // GPS: PRN 1-32
    for (int i = 0; i < randomInt(4, 12); ++i) {
        satellites.push_back(SatelliteInfo { randomInt(1, 32), Constellation::GPS });
    }

    // GLONASS: PRN 65-96
    for (int i = 0; i < randomInt(4, 12); ++i) {
        satellites.push_back(SatelliteInfo { randomInt(65, 96), Constellation::GLONASS });
    }

    // Galileo: PRN 201-237
    for (int i = 0; i < randomInt(4, 12); ++i) {
        satellites.push_back(SatelliteInfo { randomInt(201, 237), Constellation::Galileo });
    }

    // Beidou: PRN 301-336
    for (int i = 0; i < randomInt(4, 12); ++i) {
        satellites.push_back(SatelliteInfo { randomInt(301, 336), Constellation::Beidou });
    }

    // QZSS: PRN 193-200
    for (int i = 0; i < randomInt(1, 4); ++i) {
        satellites.push_back(SatelliteInfo { randomInt(193, 200), Constellation::QZSS });
    }

    return satellites;
}

// Generate GPGGA sentence
std::string NmeaGenerator::generateGPGGA(const LocationData& loc, int numSatellites)
{
    std::string utc_time    = getUTCTime();
    std::string fix_quality = std::to_string(randomInt(0, 5));
    double horizontal_dil   = randomUniform(0.5, 2.5);
    double altitude         = randomUniform(10.0, 100.0);
    double geoid_sep        = randomUniform(-50.0, 50.0);

    std::ostringstream gpgga_body;
    gpgga_body << "GPGGA," << utc_time << "," << loc.latitude << "," << loc.ns << ","
               << loc.longitude << "," << loc.ew << "," << fix_quality << "," << numSatellites
               << "," << std::fixed << std::setprecision(1) << horizontal_dil << "," << altitude
               << ",M," << geoid_sep << ",M,,,";
    std::string checksum = calculateChecksum(gpgga_body.str());
    return "$" + gpgga_body.str() + "*" + checksum + "\r\n";
}

// Generate GPRMC sentence
std::string NmeaGenerator::generateGPRMC(const LocationData& loc)
{
    std::string utc_time      = getUTCTime();
    std::string date_str      = getUTCDate();

    double speed_over_ground  = randomUniform(0, 100);
    double course_over_ground = randomUniform(0, 360);

    std::ostringstream gprmc_body;
    gprmc_body << "GPRMC," << utc_time << ",A," << loc.latitude << "," << loc.ns << ","
               << loc.longitude << "," << loc.ew << "," << std::fixed << std::setprecision(1)
               << speed_over_ground << "," << course_over_ground << "," << date_str << ",,,";
    std::string checksum = calculateChecksum(gprmc_body.str());
    return "$" + gprmc_body.str() + "*" + checksum + "\r\n";
}

// Generate GPGLL sentence
std::string NmeaGenerator::generateGPGLL(const LocationData& loc)
{
    std::string utc_time = getUTCTime();

    std::ostringstream gpgll_body;
    gpgll_body << "GPGLL," << loc.latitude << "," << loc.ns << "," << loc.longitude << "," << loc.ew
               << "," << utc_time << ",A";
    std::string checksum = calculateChecksum(gpgll_body.str());
    return "$" + gpgll_body.str() + "*" + checksum + "\r\n";
}

// Generate GPGSA sentence
std::string NmeaGenerator::generateGPGSA(const std::vector<SatelliteInfo>& satellites)
{
    char mode            = 'A';
    std::string fix_type = std::to_string(randomInt(1, 3));

    std::vector<int> prn_list;
    // Select satellites that are used for the fix
    int satellites_used = randomInt(4, 12);
    for (int i = 0; i < satellites_used && i < satellites.size(); ++i) {
        prn_list.push_back(satellites[i].prn);
    }

    while (prn_list.size() < 12) {
        prn_list.push_back(0);
    }

    double pdop = randomUniform(1.0, 5.0);
    double hdop = randomUniform(1.0, 5.0);
    double vdop = randomUniform(1.0, 5.0);

    std::ostringstream gpgsa_body;
    gpgsa_body << "GPGSA," << mode << "," << fix_type;
    for (int prn : prn_list) {
        if (prn != 0) {
            gpgsa_body << "," << prn;
        } else {
            gpgsa_body << ",";
        }
    }
    gpgsa_body << "," << std::fixed << std::setprecision(1) << pdop << "," << hdop << "," << vdop;
    std::string checksum = calculateChecksum(gpgsa_body.str());
    return "$" + gpgsa_body.str() + "*" + checksum + "\r\n";
}

// Generate GxGSV sentences based on constellation
std::string NmeaGenerator::generateGxGSV(const std::vector<SatelliteInfo>& satellites, Constellation constellation)
{
    std::string message_id;
    switch (constellation) {
    case Constellation::GPS:
        message_id = "GPGSV";
        break;
    case Constellation::GLONASS:
        message_id = "GLGSV";
        break;
    case Constellation::Galileo:
        message_id = "GAGSV";
        break;
    case Constellation::Beidou:
        message_id = "GBGSV";
        break;
    case Constellation::QZSS:
        message_id = "GQZSV";
        break;
    default:
        message_id = "GPGSV";
        break;
    }

    // Filter satellites for the specified constellation
    std::vector<SatelliteInfo> constell_satellites;
    for (const auto& sat : satellites) {
        if (sat.constellation == constellation) {
            constell_satellites.push_back(sat);
        }
    }

    int total_sats = constell_satellites.size();
    if (total_sats == 0) {
        // If no satellites for this constellation, return empty string
        return "";
    }

    int sats_per_message = 4;
    int total_messages   = (total_sats + sats_per_message - 1) / sats_per_message;

    std::ostringstream gsv_sentences;

    for (int msg_num = 1; msg_num <= total_messages; ++msg_num) {
        std::ostringstream gsv_body;
        gsv_body << message_id << "," << total_messages << "," << msg_num << "," << total_sats;

        int start_idx = (msg_num - 1) * sats_per_message;
        int end_idx   = std::min(start_idx + sats_per_message, total_sats);

        for (int i = start_idx; i < end_idx; ++i) {
            int prn       = constell_satellites[i].prn;
            int elevation = randomInt(0, 90);
            int azimuth   = randomInt(0, 359);
            int snr       = randomInt(0, 50);
            gsv_body << "," << prn << "," << elevation << "," << azimuth << "," << snr;
        }

        // If less than 4 satellites in this message, fill with empty fields
        int sats_in_this_msg = end_idx - start_idx;
        for (int i = sats_in_this_msg; i < sats_per_message; ++i) {
            gsv_body << ",,,";
        }

        // Calculate checksum
        std::string checksum = calculateChecksum(gsv_body.str());

        // Complete sentence
        std::string sentence = "$" + gsv_body.str() + "*" + checksum + "\r\n";
        gsv_sentences << sentence;
    }

    return gsv_sentences.str();
}

// Generate NFIMU sentence
std::string NmeaGenerator::generateNFIMU()
{
    int calibration_status = randomInt(0, 1);
    double temperature     = randomUniform(10, 80);
    double acc_x           = randomUniform(-100, 100);
    double acc_y           = randomUniform(-100, 100);
    double acc_z           = randomUniform(-100, 100);
    double gyro_x          = randomUniform(-2 * 3.14, 2 * 3.14);
    double gyro_y          = randomUniform(-2 * 3.14, 2 * 3.14);
    double gyro_z          = randomUniform(-2 * 3.14, 2 * 3.14);

    std::string veh_acc_x, veh_acc_y, veh_acc_z;
    std::string veh_gyro_x, veh_gyro_y, veh_gyro_z;

    if (calibration_status == 1) {
        veh_acc_x  = std::to_string(acc_x + randomUniform(-10, 10));
        veh_acc_y  = std::to_string(acc_y + randomUniform(-10, 10));
        veh_acc_z  = std::to_string(acc_z + randomUniform(-10, 10));
        veh_gyro_x = std::to_string(gyro_x + randomUniform(-2 * 3.14 * 0.1, 2 * 3.14 * 0.1));
        veh_gyro_y = std::to_string(gyro_y + randomUniform(-2 * 3.14 * 0.1, 2 * 3.14 * 0.1));
        veh_gyro_z = std::to_string(gyro_z + randomUniform(-2 * 3.14 * 0.1, 2 * 3.14 * 0.1));
    } else {
        veh_acc_x = veh_acc_y = veh_acc_z = veh_gyro_x = veh_gyro_y = veh_gyro_z = "";
    }

    std::ostringstream nfimu_body;
    nfimu_body << "NFIMU," << calibration_status << "," << std::fixed << std::setprecision(4)
               << temperature << "," << acc_x << "," << acc_y << "," << acc_z << "," << gyro_x
               << "," << gyro_y << "," << gyro_z;

    // Only append veh_acc and veh_gyro if calibration_status == 1
    if (calibration_status == 1) {
        nfimu_body << "," << veh_acc_x << "," << veh_acc_y << "," << veh_acc_z << "," << veh_gyro_x
                   << "," << veh_gyro_y << "," << veh_gyro_z;
    } else {
        nfimu_body << ",,,,,"; // Placeholder commas for missing data
    }

    std::string checksum = calculateChecksum(nfimu_body.str());
    return "$" + nfimu_body.str() + "*" + checksum + "\r\n";
}

// Generate all NMEA sentences
std::string NmeaGenerator::generateAllSentences()
{
    LocationData loc                      = generateLocation();
    std::vector<SatelliteInfo> satellites = generateSatellites();

    std::ostringstream all_sentences;
    all_sentences << generateGPRMC(loc);
    all_sentences << generateGPGGA(loc, randomInt(4, 12));
    all_sentences << generateGPGSA(satellites);

    // Generate GSV sentences for each constellation
    std::vector<Constellation> constellations = {
        Constellation::GPS,
        Constellation::GLONASS,
        Constellation::Galileo,
        Constellation::Beidou,
        Constellation::QZSS
    };

    for (const auto& constell : constellations) {
        std::string gsv = generateGxGSV(satellites, constell);
        if (!gsv.empty()) {
            all_sentences << gsv;
        }
    }

    all_sentences << generateGPGLL(loc);
    all_sentences << generateNFIMU(loc);
    return all_sentences.str();
}
