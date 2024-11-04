// NmeaSimulator.cpp
#include "NmeaSimulator.hpp"

#include <chrono>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <pty.h>
#include <signal.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

// Initialize static member
NmeaSimulator* NmeaSimulator::instance_ = nullptr;

// Constructor
NmeaSimulator::NmeaSimulator(const std::string& pipe_path, const std::string& serial_port,
                             double interval, const std::string& symlink_path)
    : pipe_path_(pipe_path)
    , serial_port_(serial_port)
    , interval_(interval)
    , shutdown_event_(false)
    , rng_(std::random_device {}())
    , master_fd_(-1)
    , symlink_path_(symlink_path)
{
}

// Destructor
NmeaSimulator::~NmeaSimulator()
{
    // Ensure cleanup is called
    cleanup();
}

// Static signal handler
void NmeaSimulator::signalHandler(int signal)
{
    if (signal == SIGINT && instance_) {
        std::cout << "\nKeyboardInterrupt received. Shutting down..." << std::endl;
        instance_->shutdown_event_.store(true);
    }
}

// Setup signal handler
void NmeaSimulator::setupSignalHandler()
{
    instance_ = this;
    struct sigaction sa;
    sa.sa_handler = NmeaSimulator::signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
}

// Start the simulator
void NmeaSimulator::start()
{
    setupSignalHandler();

    if (!serial_port_.empty()) {
        std::cout << "Using serial port: " << serial_port_ << std::endl;
        writer_thread_ = std::thread(&NmeaSimulator::serialWriterSerial, this, serial_port_, interval_);
    } else if (!pipe_path_.empty()) {
        setupNamedPipe();
        if (shutdown_event_.load())
            return; // Exit if setup failed
        std::cout << "Connect your GNSS-consuming application to the named pipe: " << pipe_path_
                  << std::endl;
        writer_thread_ = std::thread(&NmeaSimulator::serialWriterPipe, this, pipe_path_, interval_);
    } else {
        setupPTY();
        if (shutdown_event_.load())
            return; // Exit if setup failed
        // The setupPTY now already prints the symlink path
        writer_thread_ = std::thread(&NmeaSimulator::serialWriterPTY, this, master_fd_, interval_);
    }

    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }

    cleanup();
}

// Setup named pipe
void NmeaSimulator::setupNamedPipe()
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
            std::cerr << "Path exists and is not a FIFO: " << pipe_path_ << std::endl;
            shutdown_event_.store(true);
            return;
        }
        std::cout << "Using existing named pipe: " << pipe_path_ << std::endl;
    }
}

// Cleanup resources
void NmeaSimulator::cleanup()
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

    std::cout << "NmeaSimulator exited gracefully." << std::endl;
}

// Checksum calculation
std::string NmeaSimulator::calculateChecksum(const std::string& nmea_sentence) const
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
double NmeaSimulator::randomUniform(double min, double max)
{
    std::uniform_real_distribution<double> dist(min, max);
    return dist(rng_);
}

// Random integer
int NmeaSimulator::randomInt(int min, int max)
{
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng_);
}

// Generate shared location data
NmeaSimulator::LocationData NmeaSimulator::generateLocation()
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

// Generate current UTC time string in HHMMSS format
std::string NmeaSimulator::getUTCTime() const
{
    auto now        = std::chrono::system_clock::now();
    std::time_t t_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm      = *std::gmtime(&t_c);
    char utc_time[16];
    std::strftime(utc_time, sizeof(utc_time), "%H%M%S", &tm);
    return std::string(utc_time);
}

// Generate current UTC date string in DDMMYY format
std::string NmeaSimulator::getUTCDate() const
{
    auto now        = std::chrono::system_clock::now();
    std::time_t t_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm      = *std::gmtime(&t_c);
    char date_str[16];
    std::strftime(date_str, sizeof(date_str), "%d%m%y", &tm);
    return std::string(date_str);
}

// Generate GPGGA sentence
std::string NmeaSimulator::generateGPGGA(const LocationData& loc, int numSatellites)
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
std::string NmeaSimulator::generateGPRMC(const LocationData& loc)
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
std::string NmeaSimulator::generateGPGLL(const LocationData& loc)
{
    std::string utc_time = getUTCTime();

    std::ostringstream gpgll_body;
    gpgll_body << "GPGLL," << loc.latitude << "," << loc.ns << "," << loc.longitude << "," << loc.ew
               << "," << utc_time << ",A";
    std::string checksum = calculateChecksum(gpgll_body.str());
    return "$" + gpgll_body.str() + "*" + checksum + "\r\n";
}

// Generate GPGSA sentence
// // Modify GSA to handle multiple constellations if necessary
std::string NmeaSimulator::generateGPGSA(const std::vector<SatelliteInfo>& satellites)
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
// Generate GPGSV sentence
//
std::string NmeaSimulator::generateGPGSV()
{
    int numMessages   = 1;
    int message_num   = 1;
    int numSatellites = 12;

    std::ostringstream gpgsv_body;
    gpgsv_body << "GPGSV," << numMessages << "," << message_num << "," << numSatellites;

    // Note: Since this function is marked as const, and it calls randomInt,
    // which is non-const, we need to remove the const qualifier.
    // However, to minimize changes, we'll mark this function as non-const.
    // Alternatively, you can remove const from the function signature.

    // For now, remove 'const' from the function signature and update header accordingly.

    // Loop through satellites
    for (int i = 0; i < numSatellites; ++i) {
        int prn       = randomInt(1, 32);
        int elevation = randomInt(0, 90);
        int azimuth   = randomInt(0, 359);
        int snr       = randomInt(0, 50);
        gpgsv_body << "," << prn << "," << elevation << "," << azimuth << "," << snr;
    }

    std::string checksum = calculateChecksum(gpgsv_body.str());
    return "$" + gpgsv_body.str() + "*" + checksum + "\r\n";
}

// Generate NFIMU sentence
std::string NmeaSimulator::generateNFIMU(const LocationData& loc)
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

// Aggregate all NMEA sentences
std::string NmeaSimulator::generateAllSentences()
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
// Writer to named pipe
void NmeaSimulator::serialWriterPipe(const std::string& pipe_path, double interval)
{
    while (!shutdown_event_.load()) {
        std::ofstream pipe(pipe_path);
        if (!pipe.is_open()) {
            std::cerr << "Error opening pipe: " << pipe_path_ << std::endl;
            break;
        }
        while (!shutdown_event_.load()) {
            std::string sentences = generateAllSentences();
            pipe << sentences;
            pipe.flush();
            std::cout << "Sent to pipe:\n"
                      << sentences;
            std::this_thread::sleep_for(std::chrono::duration<double>(interval_));
        }
    }
    std::cout << "Pipe writer thread exiting." << std::endl;
}

// Writer to serial port
void NmeaSimulator::serialWriterSerial(const std::string& serial_port, double interval)
{
    int fd = open(serial_port.c_str(), O_WRONLY | O_NOCTTY);
    if (fd == -1) {
        std::cerr << "Error opening serial port: " << serial_port_ << std::endl;
        return;
    }

    while (!shutdown_event_.load()) {
        std::string sentences = generateAllSentences();
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

// Writer to PTY
void NmeaSimulator::serialWriterPTY(int master_fd, double interval)
{
    while (!shutdown_event_.load()) {
        std::string sentences = generateAllSentences();
        ssize_t bytes_written = write(master_fd, sentences.c_str(), sentences.size());
        if (bytes_written == -1) {
            std::cerr << "Error writing to PTY" << std::endl;
            shutdown_event_.store(true);
            break;
        }
        std::cout << "Sent to PTY:\n"
                  << sentences;
        std::this_thread::sleep_for(std::chrono::duration<double>(interval_));
    }
    close(master_fd_);
    std::cout << "PTY writer thread exiting." << std::endl;
}

// Generate satellites with different constellations
std::vector<SatelliteInfo> NmeaSimulator::generateSatellites()
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

// Generate GxGSV sentences based on constellation
std::string NmeaSimulator::generateGxGSV(const std::vector<SatelliteInfo>& satellites, Constellation constellation)
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

void NmeaSimulator::setupPTY()
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
