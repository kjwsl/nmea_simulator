import serial
import os
import time
import threading
import pty
import argparse
import sys
import random
from datetime import datetime
import stat


def calculate_checksum(nmea_sentence):
    """Calculate NMEA sentence checksum."""
    checksum = 0
    for char in nmea_sentence:
        checksum ^= ord(char)
    return f"{checksum:02X}"


def generate_gpgga():
    """Generate a fake GPGGA sentence."""
    utc_time = datetime.utcnow().strftime("%H%M%S.%f")[:-4]
    latitude = f"{random.uniform(-90, 90):08.4f}"
    ns = "N" if float(latitude) >= 0 else "S"
    latitude = f"{abs(float(latitude)):.4f}"
    longitude = f"{random.uniform(-180, 180):09.4f}"
    ew = "E" if float(longitude) >= 0 else "W"
    longitude = f"{abs(float(longitude)):.4f}"
    fix_quality = random.choice(["0", "1", "2", "4", "5"])
    num_satellites = f"{random.randint(3, 12):02}"
    horizontal_dil = f"{random.uniform(0.5, 2.5):.1f}"
    altitude = f"{random.uniform(10.0, 100.0):.1f}"
    altitude_units = "M"
    geoid_sep = f"{random.uniform(-50.0, 50.0):.1f}"
    geoid_units = "M"
    gpgga_body = f"GPGGA,{utc_time},{latitude},{ns},{longitude},{ew},{fix_quality},{num_satellites},{horizontal_dil},{altitude},{altitude_units},{geoid_sep},{geoid_units},,,"
    checksum = calculate_checksum(gpgga_body)
    return f"${gpgga_body}*{checksum}\r\n"


def generate_gprmc():
    """Generate a fake GPRMC sentence."""
    utc_time = datetime.utcnow().strftime("%H%M%S.%f")[:-4]
    status = "A"  # A = Active, V = Void
    latitude = f"{random.uniform(-90, 90):08.4f}"
    ns = "N" if float(latitude) >= 0 else "S"
    latitude = f"{abs(float(latitude)):.4f}"
    longitude = f"{random.uniform(-180, 180):09.4f}"
    ew = "E" if float(longitude) >= 0 else "W"
    longitude = f"{abs(float(longitude)):.4f}"
    speed_over_ground = f"{random.uniform(0, 100):05.1f}"  # knots
    course_over_ground = f"{random.uniform(0, 360):05.1f}"  # degrees
    date_str = datetime.utcnow().strftime("%d%m%y")
    magnetic_variation = ""
    mv_direction = ""
    gprmc_body = f"GPRMC,{utc_time},{status},{latitude},{ns},{longitude},{ew},{speed_over_ground},{course_over_ground},{date_str},{magnetic_variation},{mv_direction},"
    checksum = calculate_checksum(gprmc_body)
    return f"${gprmc_body}*{checksum}\r\n"


def generate_nmea_sentences():
    """Generator for NMEA sentences."""
    while True:
        yield generate_gpgga()
        yield generate_gprmc()
        time.sleep(1)  # Interval between sentences


def serial_writer_pipe(pipe_path, interval, shutdown_event):
    """Write NMEA sentences to the named pipe."""
    nmea_gen = generate_nmea_sentences()
    try:
        with open(pipe_path, 'w') as pipe:
            while not shutdown_event.is_set():
                sentence = next(nmea_gen)
                try:
                    pipe.write(sentence)
                    pipe.flush()  # Ensure data is written immediately
                    print(f"Sent to pipe: {sentence.strip()}")
                except BrokenPipeError:
                    print("Reader closed the pipe. Exiting.")
                    shutdown_event.set()
                    break
                time.sleep(interval)
    except Exception as e:
        print(f"Error writing to pipe: {e}")
    finally:
        print("Pipe writer thread exiting.")


def serial_writer_serial(serial_port, interval, shutdown_event):
    """Write NMEA sentences to the specified serial port."""
    nmea_gen = generate_nmea_sentences()
    try:
        with open(serial_port, 'w') as ser:
            while not shutdown_event.is_set():
                sentence = next(nmea_gen)
                try:
                    ser.write(sentence)
                    ser.flush()
                    print(f"Sent to serial port: {sentence.strip()}")
                except BrokenPipeError:
                    print("Reader closed the serial port. Exiting.")
                    shutdown_event.set()
                    break
                time.sleep(interval)
    except Exception as e:
        print(f"Error writing to serial port: {e}")
    finally:
        print("Serial port writer thread exiting.")


def serial_writer_pty(master_fd, interval, shutdown_event):
    """Write NMEA sentences to the virtual serial port."""
    nmea_gen = generate_nmea_sentences()
    try:
        while not shutdown_event.is_set():
            sentence = next(nmea_gen)
            try:
                os.write(master_fd, sentence.encode("ascii"))
                print(f"Sent to PTY: {sentence.strip()}")
            except OSError as e:
                print(f"Error writing to PTY: {e}")
                shutdown_event.set()
                break
            time.sleep(interval)
    except Exception as e:
        print(f"Error in PTY writer: {e}")
    finally:
        try:
            os.close(master_fd)
            print("Serial port closed.")
        except Exception as e:
            print(f"Error closing master FD: {e}")
        print("PTY writer thread exiting.")


def main():
    parser = argparse.ArgumentParser(description="GNSS Simulator")
    parser.add_argument(
        "--pipe",
        dest="pipe_path",
        help="Path to the named pipe (FIFO) to write NMEA sentences",
    )
    parser.add_argument(
        "--serial",
        dest="serial_port",
        help="Path to the serial port to write NMEA sentences",
    )
    parser.add_argument(
        "--interval",
        type=float,
        default=1.0,
        help="Interval in seconds between NMEA sentences",
    )

    args = parser.parse_args()

    shutdown_event = threading.Event()

    if args.serial_port:
        serial_port = args.serial_port
        print(f"Using serial port: {serial_port}")

        # Start the serial writer using the specified serial port
        writer_thread = threading.Thread(
            target=serial_writer_serial,
            args=(serial_port, args.interval, shutdown_event),
            name="SerialWriterThread"
        )
    elif args.pipe_path:
        pipe_path = args.pipe_path

        # Create the named pipe if it doesn't exist
        if not os.path.exists(pipe_path):
            try:
                os.mkfifo(pipe_path)
                print(f"Named pipe created at: {pipe_path}")
            except OSError as e:
                print(f"Failed to create named pipe: {e}")
                sys.exit(1)
        else:
            if not stat.S_ISFIFO(os.stat(pipe_path).st_mode):
                print(f"Path exists and is not a FIFO: {pipe_path}")
                sys.exit(1)
            print(f"Using existing named pipe: {pipe_path}")

        # Inform the user to connect their application to the named pipe
        print(
            f"Connect your GNSS-consuming application to the named pipe: {pipe_path}")

        # Start the serial writer using the named pipe
        writer_thread = threading.Thread(
            target=serial_writer_pipe,
            args=(pipe_path, args.interval, shutdown_event),
            name="PipeWriterThread"
        )
    else:
        # Create a virtual serial port using PTY
        master_fd, slave_fd = pty.openpty()
        slave_name = os.ttyname(slave_fd)
        print(f"Virtual serial port created at: {slave_name}")

        # Inform the user to connect their application to the slave device
        print(f"Connect your GNSS-consuming application to: {slave_name}")

        # Start the serial writer using the PTY
        writer_thread = threading.Thread(
            target=serial_writer_pty,
            args=(master_fd, args.interval, shutdown_event),
            name="PTYWriterThread"
        )

    # Start the writer thread
    writer_thread.start()

    try:
        while writer_thread.is_alive():
            writer_thread.join(timeout=1)
    except KeyboardInterrupt:
        print("\nKeyboardInterrupt received. Shutting down...")
        shutdown_event.set()
        writer_thread.join()
    finally:
        if args.pipe_path and os.path.exists(args.pipe_path):
            try:
                os.unlink(args.pipe_path)
                print(f"Named pipe removed: {args.pipe_path}")
            except Exception as e:
                print(f"Error removing named pipe: {e}")
        print("GNSS simulator exited gracefully.")


if __name__ == "__main__":
    main()
