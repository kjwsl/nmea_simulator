#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <random>
#include <thread>
#include <chrono>
#include <ctime>
#include <vector>
#include <functional>
#include <atomic>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pty.h>
#include <signal.h>
#include <getopt.h>

std::atomic<bool> shutdown_event(false);

// Function to calculate NMEA checksum
std::string calculate_checksum(const std::string &nmea_sentence)
{
    uint8_t checksum = 0;
    for (char ch : nmea_sentence)
    {
        checksum ^= static_cast<uint8_t>(ch);
    }
    std::stringstream ss;
    ss << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << int(checksum);
    return ss.str();
}

// Random number generators
std::mt19937 rng(std::random_device{}());

double random_uniform(double min, double max)
{
    std::uniform_real_distribution<double> dist(min, max);
    return dist(rng);
}

int random_int(int min, int max)
{
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng);
}

// Functions to generate fake NMEA sentences
std::string generate_nfimu()
{
    int calibration_status = random_int(0, 1);
    double temperature = random_uniform(10, 80);
    double acc_x = random_uniform(-100, 100);
    double acc_y = random_uniform(-100, 100);
    double acc_z = random_uniform(-100, 100);
    double gyro_x = random_uniform(-2 * 3.14, 2 * 3.14);
    double gyro_y = random_uniform(-2 * 3.14, 2 * 3.14);
    double gyro_z = random_uniform(-2 * 3.14, 2 * 3.14);

    std::string veh_acc_x, veh_acc_y, veh_acc_z;
    std::string veh_gyro_x, veh_gyro_y, veh_gyro_z;

    if (calibration_status == 1)
    {
        veh_acc_x = std::to_string(acc_x + random_uniform(-10, 10));
        veh_acc_y = std::to_string(acc_y + random_uniform(-10, 10));
        veh_acc_z = std::to_string(acc_z + random_uniform(-10, 10));
        veh_gyro_x = std::to_string(gyro_x + random_uniform(-2 * 3.14 * 0.1, 2 * 3.14 * 0.1));
        veh_gyro_y = std::to_string(gyro_y + random_uniform(-2 * 3.14 * 0.1, 2 * 3.14 * 0.1));
        veh_gyro_z = std::to_string(gyro_z + random_uniform(-2 * 3.14 * 0.1, 2 * 3.14 * 0.1));
    }

    std::ostringstream nfimu_body;
    nfimu_body << "NFIMU," << calibration_status << "," << std::fixed << std::setprecision(4)
               << temperature << "," << acc_x << "," << acc_y << "," << acc_z << "," << gyro_x
               << "," << gyro_y << "," << gyro_z << "," << veh_acc_x << "," << veh_acc_y << ","
               << veh_acc_z << "," << veh_gyro_x << "," << veh_gyro_y << "," << veh_gyro_z;

    std::string checksum = calculate_checksum(nfimu_body.str());
    return "$" + nfimu_body.str() + "*" + checksum + "\r\n";
}

std::string generate_gpgga()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::gmtime(&t_c);
    char utc_time[16];
    std::strftime(utc_time, sizeof(utc_time), "%H%M%S", &tm);

    double latitude = random_uniform(-90, 90);
    char ns = latitude >= 0 ? 'N' : 'S';
    latitude = std::abs(latitude);

    double longitude = random_uniform(-180, 180);
    char ew = longitude >= 0 ? 'E' : 'W';
    longitude = std::abs(longitude);

    std::string fix_quality = std::to_string(random_int(0, 5));
    std::string num_satellites = std::to_string(random_int(3, 12));
    double horizontal_dil = random_uniform(0.5, 2.5);
    double altitude = random_uniform(10.0, 100.0);
    double geoid_sep = random_uniform(-50.0, 50.0);

    std::ostringstream gpgga_body;
    gpgga_body << "GPGGA," << utc_time << "," << std::fixed << std::setprecision(4) << latitude
               << "," << ns << "," << longitude << "," << ew << "," << fix_quality << ","
               << num_satellites << "," << horizontal_dil << "," << altitude << ",M," << geoid_sep
               << ",M,,,";
    std::string checksum = calculate_checksum(gpgga_body.str());
    return "$" + gpgga_body.str() + "*" + checksum + "\r\n";
}

std::string generate_gprmc()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::gmtime(&t_c);
    char utc_time[16], date_str[16];
    std::strftime(utc_time, sizeof(utc_time), "%H%M%S", &tm);
    std::strftime(date_str, sizeof(date_str), "%d%m%y", &tm);

    double latitude = random_uniform(-90, 90);
    char ns = latitude >= 0 ? 'N' : 'S';
    latitude = std::abs(latitude);

    double longitude = random_uniform(-180, 180);
    char ew = longitude >= 0 ? 'E' : 'W';
    longitude = std::abs(longitude);

    double speed_over_ground = random_uniform(0, 100);
    double course_over_ground = random_uniform(0, 360);

    std::ostringstream gprmc_body;
    gprmc_body << "GPRMC," << utc_time << ",A," << std::fixed << std::setprecision(4) << latitude
               << "," << ns << "," << longitude << "," << ew << "," << std::fixed
               << std::setprecision(1) << speed_over_ground << "," << course_over_ground << ","
               << date_str << ",,,";
    std::string checksum = calculate_checksum(gprmc_body.str());
    return "$" + gprmc_body.str() + "*" + checksum + "\r\n";
}

std::string generate_imuag()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::gmtime(&t_c);
    char utc_time[16];
    std::strftime(utc_time, sizeof(utc_time), "%H%M%S", &tm);

    double roll = random_uniform(-180, 180);
    double pitch = random_uniform(-180, 180);
    double yaw = random_uniform(-180, 180);
    double acc_x = random_uniform(-10, 10);
    double acc_y = random_uniform(-10, 10);
    double acc_z = random_uniform(-10, 10);
    double gyro_x = random_uniform(-10, 10);
    double gyro_y = random_uniform(-10, 10);
    double gyro_z = random_uniform(-10, 10);

    std::ostringstream imuag_body;
    imuag_body << "IMUAG," << utc_time << "," << std::fixed << std::setprecision(4) << roll << ","
               << pitch << "," << yaw << "," << acc_x << "," << acc_y << "," << acc_z << ","
               << gyro_x << "," << gyro_y << "," << gyro_z;
    std::string checksum = calculate_checksum(imuag_body.str());
    return "$" + imuag_body.str() + "*" + checksum + "\r\n";
}

std::string generate_gpgll()
{
    double latitude = random_uniform(-90, 90);
    char ns = latitude >= 0 ? 'N' : 'S';
    latitude = std::abs(latitude);

    double longitude = random_uniform(-180, 180);
    char ew = longitude >= 0 ? 'E' : 'W';
    longitude = std::abs(longitude);

    auto now = std::chrono::system_clock::now();
    std::time_t t_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::gmtime(&t_c);
    char utc_time[16];
    std::strftime(utc_time, sizeof(utc_time), "%H%M%S", &tm);

    std::ostringstream gpgll_body;
    gpgll_body << "GPGLL," << std::fixed << std::setprecision(4) << latitude << "," << ns << ","
               << longitude << "," << ew << "," << utc_time << ",A";
    std::string checksum = calculate_checksum(gpgll_body.str());
    return "$" + gpgll_body.str() + "*" + checksum + "\r\n";
}

std::string generate_gpgsa()
{
    char mode = 'A';
    std::string fix_type = std::to_string(random_int(1, 3));

    std::vector<int> prn_list;
    for (int i = 0; i < 12; ++i)
    {
        prn_list.push_back(random_int(1, 32));
    }

    double pdop = random_uniform(1.0, 5.0);
    double hdop = random_uniform(1.0, 5.0);
    double vdop = random_uniform(1.0, 5.0);

    std::ostringstream gpgsa_body;
    gpgsa_body << "GPGSA," << mode << "," << fix_type;
    for (int prn : prn_list)
    {
        gpgsa_body << "," << prn;
    }
    gpgsa_body << "," << std::fixed << std::setprecision(1) << pdop << "," << hdop << "," << vdop;
    std::string checksum = calculate_checksum(gpgsa_body.str());
    return "$" + gpgsa_body.str() + "*" + checksum + "\r\n";
}

std::string generate_gpgsv()
{
    int num_messages = 1;
    int message_num = 1;
    int num_satellites = 12;

    std::ostringstream gpgsv_body;
    gpgsv_body << "GPGSV," << num_messages << "," << message_num << "," << num_satellites;

    for (int i = 0; i < num_satellites; ++i)
    {
        int prn = random_int(1, 32);
        int elevation = random_int(0, 90);
        int azimuth = random_int(0, 359);
        int snr = random_int(0, 50);
        gpgsv_body << "," << prn << "," << elevation << "," << azimuth << "," << snr;
    }

    std::string checksum = calculate_checksum(gpgsv_body.str());
    return "$" + gpgsv_body.str() + "*" + checksum + "\r\n";
}

std::string yield_nmea_sentences()
{
    std::string lines;
    lines += generate_gpgga();
    lines += generate_gprmc();
    lines += generate_gpgll();
    lines += generate_gpgsa();
    lines += generate_gpgsv();
    lines += generate_nfimu();
    return lines;
}

void serial_writer_pipe(const std::string &pipe_path, double interval)
{
    while (!shutdown_event.load())
    {
        std::ofstream pipe(pipe_path);
        if (!pipe.is_open())
        {
            std::cerr << "Error opening pipe: " << pipe_path << std::endl;
            break;
        }
        while (!shutdown_event.load())
        {
            std::string sentence = yield_nmea_sentences();
            pipe << sentence;
            pipe.flush();
            std::cout << "Sent to pipe: " << sentence;
            std::this_thread::sleep_for(std::chrono::duration<double>(interval));
        }
    }
    std::cout << "Pipe writer thread exiting." << std::endl;
}

void serial_writer_serial(const std::string &serial_port, double interval)
{
    int fd = open(serial_port.c_str(), O_WRONLY | O_NOCTTY);
    if (fd == -1)
    {
        std::cerr << "Error opening serial port: " << serial_port << std::endl;
        return;
    }

    while (!shutdown_event.load())
    {
        std::string sentence = yield_nmea_sentences();
        write(fd, sentence.c_str(), sentence.size());
        fsync(fd);
        std::cout << "Sent to serial port: " << sentence;
        std::this_thread::sleep_for(std::chrono::duration<double>(interval));
    }
    close(fd);
    std::cout << "Serial port writer thread exiting." << std::endl;
}

void serial_writer_pty(int master_fd, double interval)
{
    while (!shutdown_event.load())
    {
        std::string sentence = yield_nmea_sentences();
        ssize_t bytes_written = write(master_fd, sentence.c_str(), sentence.size());
        if (bytes_written == -1)
        {
            std::cerr << "Error writing to PTY" << std::endl;
            shutdown_event.store(true);
            break;
        }
        std::cout << "Sent to PTY: " << sentence;
        std::this_thread::sleep_for(std::chrono::duration<double>(interval));
    }
    close(master_fd);
    std::cout << "PTY writer thread exiting." << std::endl;
}

void signal_handler(int signal)
{
    if (signal == SIGINT)
    {
        std::cout << "\nKeyboardInterrupt received. Shutting down..." << std::endl;
        shutdown_event.store(true);
    }
}

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
            interval = std::stod(optarg);
            break;
        default:
            std::cerr << "Usage: " << argv[0]
                      << " [--pipe PATH] [--serial PORT] [--interval SECONDS]" << std::endl;
            return 1;
        }
    }

    signal(SIGINT, signal_handler);

    std::thread writer_thread;

    if (!serial_port.empty())
    {
        std::cout << "Using serial port: " << serial_port << std::endl;
        writer_thread = std::thread(serial_writer_serial, serial_port, interval);
    }
    else if (!pipe_path.empty())
    {
        if (access(pipe_path.c_str(), F_OK) == -1)
        {
            if (mkfifo(pipe_path.c_str(), 0666) != 0)
            {
                std::cerr << "Failed to create named pipe: " << pipe_path << std::endl;
                return 1;
            }
            std::cout << "Named pipe created at: " << pipe_path << std::endl;
        }
        else
        {
            struct stat stat_buf;
            if (stat(pipe_path.c_str(), &stat_buf) != 0 || !S_ISFIFO(stat_buf.st_mode))
            {
                std::cerr << "Path exists and is not a FIFO: " << pipe_path << std::endl;
                return 1;
            }
            std::cout << "Using existing named pipe: " << pipe_path << std::endl;
        }
        std::cout << "Connect your GNSS-consuming application to the named pipe: " << pipe_path
                  << std::endl;
        writer_thread = std::thread(serial_writer_pipe, pipe_path, interval);
    }
    else
    {
        int master_fd, slave_fd;
        char slave_name[256];

        if (openpty(&master_fd, &slave_fd, slave_name, nullptr, nullptr) == -1)
        {
            std::cerr << "Failed to create virtual serial port" << std::endl;
            return 1;
        }

        std::cout << "Virtual serial port created at: " << slave_name << std::endl;
        std::cout << "Connect your GNSS-consuming application to: " << slave_name << std::endl;
        writer_thread = std::thread(serial_writer_pty, master_fd, interval);
    }

    if (writer_thread.joinable())
    {
        writer_thread.join();
    }

    if (!pipe_path.empty() && access(pipe_path.c_str(), F_OK) != -1)
    {
        if (unlink(pipe_path.c_str()) != 0)
        {
            std::cerr << "Error removing named pipe: " << pipe_path << std::endl;
        }
        else
        {
            std::cout << "Named pipe removed: " << pipe_path << std::endl;
        }
    }

    std::cout << "GNSS simulator exited gracefully." << std::endl;
    return 0;
}
