// main.rs

use chrono::prelude::*;
use rand::distributions::{Distribution, Uniform};
use rand::rngs::ThreadRng;
use rand::thread_rng;
use std::env;
use std::fs::OpenOptions;
use std::io::{prelude::*, BufWriter};
use std::os::unix::io::{AsRawFd, RawFd};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::Duration;
use std::{ffi::CStr, ptr};

use libc::{openpty, winsize};
use nix::fcntl::{open, OFlag};
use nix::sys::stat;
use nix::unistd::{close, write};

use signal_hook::consts::SIGINT;
use signal_hook::iterator::Signals;

use std::error::Error;

pub struct NmeaSimulator {
    pipe_path: String,
    serial_port: String,
    interval: f64,
    shutdown_event: Arc<AtomicBool>,
    rng: ThreadRng,
    master_fd: Option<RawFd>,
    slave_name: Option<String>,
}

impl NmeaSimulator {
    pub fn new(pipe_path: String, serial_port: String, interval: f64) -> Self {
        NmeaSimulator {
            pipe_path,
            serial_port,
            interval,
            shutdown_event: Arc::new(AtomicBool::new(false)),
            rng: thread_rng(),
            master_fd: None,
            slave_name: None,
        }
    }

    pub fn start(&mut self) -> Result<(), Box<dyn Error>> {
        // Set up signal handler
        let shutdown_event = self.shutdown_event.clone();
        let mut signals = Signals::new(&[SIGINT])?;

        // Spawn a thread to handle signals
        thread::spawn(move || {
            for _ in signals.forever() {
                println!("\nKeyboardInterrupt received. Shutting down...");
                shutdown_event.store(true, Ordering::SeqCst);
            }
        });

        if !self.serial_port.is_empty() {
            println!("Using serial port: {}", self.serial_port);
            self.serial_writer_serial()?;
        } else if !self.pipe_path.is_empty() {
            self.setup_named_pipe()?;
            if self.shutdown_event.load(Ordering::SeqCst) {
                return Ok(()); // Exit if setup failed
            }
            println!(
                "Connect your GNSS-consuming application to the named pipe: {}",
                self.pipe_path
            );
            self.serial_writer_pipe()?;
        } else {
            let slave_name = self.setup_pty()?;
            if self.shutdown_event.load(Ordering::SeqCst) {
                return Ok(()); // Exit if setup failed
            }
            println!("Connect your GNSS-consuming application to: {}", slave_name);
            self.serial_writer_pty()?;
        }

        self.cleanup();
        Ok(())
    }

    fn setup_named_pipe(&self) -> Result<(), Box<dyn Error>> {
        use std::os::unix::fs::FileTypeExt;
        use std::path::Path;
        let pipe_path = Path::new(&self.pipe_path);

        if !pipe_path.exists() {
            match nix::unistd::mkfifo(pipe_path, stat::Mode::S_IRWXU) {
                Ok(_) => println!("Named pipe created at: {}", self.pipe_path),
                Err(e) => {
                    eprintln!("Failed to create named pipe: {}", e);
                    self.shutdown_event.store(true, Ordering::SeqCst);
                    return Err(Box::new(e));
                }
            }
        } else {
            let metadata = std::fs::metadata(pipe_path)?;
            if !metadata.file_type().is_fifo() {
                eprintln!("Path exists and is not a FIFO: {}", self.pipe_path);
                self.shutdown_event.store(true, Ordering::SeqCst);
                return Err(Box::new(std::io::Error::new(
                    std::io::ErrorKind::Other,
                    "Path is not a FIFO",
                )));
            } else {
                println!("Using existing named pipe: {}", self.pipe_path);
            }
        }
        Ok(())
    }

    fn setup_pty(&mut self) -> Result<String, Box<dyn Error>> {
        let mut master_fd: i32 = 0;
        let mut slave_fd: i32 = 0;
        let mut slave_name_buf = [0u8; 1024];

        let result = unsafe {
            openpty(
                &mut master_fd,
                &mut slave_fd,
                slave_name_buf.as_mut_ptr() as *mut libc::c_char,
                ptr::null_mut(),
                ptr::null_mut(),
            )
        };

        if result != 0 {
            eprintln!("Failed to create PTY");
            self.shutdown_event.store(true, Ordering::SeqCst);
            return Err(Box::new(std::io::Error::last_os_error()));
        }

        // Get the slave device name
        let c_str = unsafe { CStr::from_ptr(slave_name_buf.as_ptr() as *const libc::c_char) };
        let slave_name = c_str.to_str()?.to_string();

        self.master_fd = Some(master_fd);
        self.slave_name = Some(slave_name.clone());

        println!("Virtual serial port created at: {}", slave_name);
        Ok(slave_name)
    }

    fn cleanup(&self) {
        if !self.pipe_path.is_empty() {
            use std::path::Path;
            let pipe_path = Path::new(&self.pipe_path);
            if pipe_path.exists() {
                match std::fs::remove_file(pipe_path) {
                    Ok(_) => println!("Named pipe removed: {}", self.pipe_path),
                    Err(e) => eprintln!("Error removing named pipe: {}", e),
                }
            }
        }

        if let Some(master_fd) = self.master_fd {
            let _ = close(master_fd);
            println!("PTY writer thread exiting.");
        }

        println!("NmeaSimulator exited gracefully.");
    }

    // Random uniform double
    fn random_uniform(&mut self, min: f64, max: f64) -> f64 {
        let between = Uniform::from(min..max);
        between.sample(&mut self.rng)
    }

    // Random integer
    fn random_int(&mut self, min: i32, max: i32) -> i32 {
        let between = Uniform::from(min..=max);
        between.sample(&mut self.rng)
    }

    fn generate_location(&mut self) -> LocationData {
        let latitude = self.random_uniform(-90.0, 90.0);
        let ns = if latitude >= 0.0 { 'N' } else { 'S' };
        let lat_deg = latitude.abs().floor();
        let lat_min = (latitude.abs() - lat_deg) * 60.0;

        let longitude = self.random_uniform(-180.0, 180.0);
        let ew = if longitude >= 0.0 { 'E' } else { 'W' };
        let lon_deg = longitude.abs().floor();
        let lon_min = (longitude.abs() - lon_deg) * 60.0;

        LocationData {
            latitude: format!("{:02}{:07.4}", lat_deg as u32, lat_min),
            ns,
            longitude: format!("{:03}{:07.4}", lon_deg as u32, lon_min),
            ew,
        }
    }

    fn get_utc_time(&self) -> String {
        let now = Utc::now();
        now.format("%H%M%S").to_string()
    }

    fn get_utc_date(&self) -> String {
        let now = Utc::now();
        now.format("%d%m%y").to_string()
    }

    fn calculate_checksum(&self, sentence: &str) -> String {
        let mut checksum: u8 = 0;
        for c in sentence.chars() {
            checksum ^= c as u8;
        }
        format!("{:02X}", checksum)
    }

    fn generate_gpgga(&mut self, loc: &LocationData, num_satellites: i32) -> String {
        let utc_time = self.get_utc_time();
        let fix_quality = self.random_int(0, 5);
        let horizontal_dil = self.random_uniform(0.5, 2.5);
        let altitude = self.random_uniform(10.0, 100.0);
        let geoid_sep = self.random_uniform(-50.0, 50.0);

        let body = format!(
            "GPGGA,{},{},{},{},{},{},{},{},{:.1},M,{:.1},M,,",
            utc_time,
            loc.latitude,
            loc.ns,
            loc.longitude,
            loc.ew,
            fix_quality,
            num_satellites,
            horizontal_dil,
            altitude,
            geoid_sep
        );
        let checksum = self.calculate_checksum(&body);
        format!("${}*{}\r\n", body, checksum)
    }

    fn generate_gprmc(&mut self, loc: &LocationData) -> String {
        let utc_time = self.get_utc_time();
        let date_str = self.get_utc_date();
        let speed_over_ground = self.random_uniform(0.0, 100.0);
        let course_over_ground = self.random_uniform(0.0, 360.0);

        let body = format!(
            "GPRMC,{},{},{},{},{},{},{:.1},{:.1},{},,,",
            utc_time,
            "A", // Status A=Active, V=Void
            loc.latitude,
            loc.ns,
            loc.longitude,
            loc.ew,
            speed_over_ground,
            course_over_ground,
            date_str
        );
        let checksum = self.calculate_checksum(&body);
        format!("${}*{}\r\n", body, checksum)
    }

    fn generate_gpgll(&mut self, loc: &LocationData) -> String {
        let utc_time = self.get_utc_time();

        let body = format!(
            "GPGLL,{},{},{},{},{},A",
            loc.latitude, loc.ns, loc.longitude, loc.ew, utc_time
        );
        let checksum = self.calculate_checksum(&body);
        format!("${}*{}\r\n", body, checksum)
    }

    fn generate_all_sentences(&mut self) -> String {
        let loc = self.generate_location();
        let num_satellites = self.random_int(4, 12);
        let mut sentences = String::new();
        sentences.push_str(&self.generate_gpgga(&loc, num_satellites));
        sentences.push_str(&self.generate_gprmc(&loc));
        sentences.push_str(&self.generate_gpgll(&loc));
        // You can implement and add other sentences similarly
        sentences
    }

    fn serial_writer_pipe(&mut self) -> Result<(), Box<dyn Error>> {
        while !self.shutdown_event.load(Ordering::SeqCst) {
            let pipe = OpenOptions::new().write(true).open(&self.pipe_path)?;
            let mut writer = BufWriter::new(pipe);
            while !self.shutdown_event.load(Ordering::SeqCst) {
                let sentences = self.generate_all_sentences();
                writer.write_all(sentences.as_bytes())?;
                writer.flush()?;
                println!("Sent to pipe:\n{}", sentences);
                thread::sleep(Duration::from_secs_f64(self.interval));
            }
        }
        println!("Pipe writer thread exiting.");
        Ok(())
    }

    fn serial_writer_serial(&mut self) -> Result<(), Box<dyn Error>> {
        let fd = open(
            self.serial_port.as_str(),
            OFlag::O_WRONLY | OFlag::O_NOCTTY,
            stat::Mode::empty(),
        )?;
        while !self.shutdown_event.load(Ordering::SeqCst) {
            let sentences = self.generate_all_sentences();
            let bytes_written = write(fd, sentences.as_bytes())?;
            if bytes_written == 0 {
                eprintln!("Error writing to serial port: {}", self.serial_port);
                break;
            }
            println!("Sent to serial port:\n{}", sentences);
            thread::sleep(Duration::from_secs_f64(self.interval));
        }
        close(fd)?;
        println!("Serial port writer thread exiting.");
        Ok(())
    }

    fn serial_writer_pty(&mut self) -> Result<(), Box<dyn Error>> {
        if let Some(master_fd) = self.master_fd {
            while !self.shutdown_event.load(Ordering::SeqCst) {
                let sentences = self.generate_all_sentences();
                let bytes_written = unsafe {
                    libc::write(
                        master_fd,
                        sentences.as_ptr() as *const libc::c_void,
                        sentences.len(),
                    )
                };
                if bytes_written == -1 {
                    eprintln!("Error writing to PTY");
                    self.shutdown_event.store(true, Ordering::SeqCst);
                    break;
                }
                println!("Sent to PTY:\n{}", sentences);
                thread::sleep(Duration::from_secs_f64(self.interval));
            }
            unsafe {
                libc::close(master_fd);
            }
            println!("PTY writer thread exiting.");
        }
        Ok(())
    }
}

// LocationData struct
struct LocationData {
    latitude: String,
    ns: char,
    longitude: String,
    ew: char,
}
