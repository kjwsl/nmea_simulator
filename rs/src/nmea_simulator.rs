// main.rs

use chrono::prelude::*;
use rand::distributions::{Distribution, Uniform};
use rand::rngs::ThreadRng;
use rand::thread_rng;
use std::fs::{self, OpenOptions};
use std::io::{prelude::*, BufWriter, ErrorKind};
use std::os::unix::fs::symlink;
use std::os::unix::io::RawFd;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::Duration;
use std::{ffi::CStr, ptr};

use libc::openpty;
use nix::fcntl::{open, OFlag};
use nix::sys::stat;
use nix::unistd::{close, read, write};
use std::path::Path;

use signal_hook::consts::SIGINT;
use signal_hook::iterator::Signals;

use std::error::Error;

pub struct NmeaSimulator {
    shutdown_event: Arc<AtomicBool>,
    master_fd1: Option<RawFd>,
    master_fd2: Option<RawFd>,
    forward_thread1: Option<thread::JoinHandle<()>>,
    forward_thread2: Option<thread::JoinHandle<()>>,
    rng: ThreadRng,
}

impl NmeaSimulator {
    pub fn new() -> Self {
        NmeaSimulator {
            shutdown_event: Arc::new(AtomicBool::new(false)),
            master_fd1: None,
            master_fd2: None,
            forward_thread1: None,
            forward_thread2: None,
            rng: thread_rng(),
        }
    }

    pub fn start(
        &mut self,
        gps_input_path: &str,
        gps_output_path: &str,
    ) -> Result<(), Box<dyn Error>> {
        // Set up signal handler
        let shutdown_event = self.shutdown_event.clone();
        let mut signals = Signals::new([SIGINT])?;

        // Spawn a thread to handle signals
        thread::spawn(move || {
            for _ in signals.forever() {
                println!("\nKeyboardInterrupt received. Shutting down...");
                shutdown_event.store(true, Ordering::SeqCst);
            }
        });

        self.setup_linked_ptys(gps_input_path, gps_output_path)?;

        self.start_forwarding()?;

        self.write_nmea_messages(gps_output_path)?;

        self.cleanup(gps_input_path, gps_output_path)?;
        Ok(())
    }

    fn setup_linked_ptys(
        &mut self,
        gps_input_path: &str,
        gps_output_path: &str,
    ) -> Result<(), Box<dyn Error>> {
        let (master_fd1, slave_name1) = self.create_pty()?;
        println!("Master FD1: {}, Slave Name1: {}", master_fd1, slave_name1);

        let (master_fd2, slave_name2) = self.create_pty()?;
        println!("Master FD2: {}, Slave Name2: {}", master_fd2, slave_name2);

        self.create_symlink(&slave_name1, gps_output_path)?;
        self.create_symlink(&slave_name2, gps_input_path)?;

        self.master_fd1 = Some(master_fd1);
        self.master_fd2 = Some(master_fd2);

        Ok(())
    }

    fn create_symlink(&self, target: &str, link: &str) -> Result<(), Box<dyn Error>> {
        let link_path = Path::new(link);
        if link_path.exists() {
            fs::remove_file(link_path)?;
        }
        symlink(target, link)?;
        Ok(())
    }

    fn start_forwarding(&mut self) -> Result<(), Box<dyn Error>> {
        let shutdown_event = self.shutdown_event.clone();

        let master_fd1 = self.master_fd1.unwrap();
        let master_fd2 = self.master_fd2.unwrap();

        // Forward data from master_fd2 to master_fd1
        let shutdown_event_clone = shutdown_event.clone();
        let forward_thread1 = thread::spawn(move || {
            let mut buf = [0u8; 1024];
            while !shutdown_event_clone.load(Ordering::SeqCst) {
                match unsafe {
                    libc::read(master_fd1, buf.as_mut_ptr() as *mut libc::c_void, buf.len())
                } {
                    n if n > 0 => {
                        let _ = unsafe {
                            libc::write(master_fd2, buf.as_ptr() as *const libc::c_void, n as usize)
                        };
                    }
                    0 => {}
                    -1 => {
                        let err = std::io::Error::last_os_error();
                        if err.kind() == ErrorKind::Interrupted {
                            continue;
                        } else {
                            break;
                        }
                    }
                    _ => break,
                }
            }
            let _ = close(master_fd1);
        });

        let shutdown_event_clone = shutdown_event.clone();
        let forward_thread2 = thread::spawn(move || {
            let mut buf = [0u8; 1024];
            while !shutdown_event_clone.load(Ordering::SeqCst) {
                match unsafe {
                    libc::read(master_fd2, buf.as_mut_ptr() as *mut libc::c_void, buf.len())
                } {
                    n if n > 0 => {
                        let _ = unsafe {
                            libc::write(master_fd1, buf.as_ptr() as *const libc::c_void, n as usize)
                        };
                    }
                    0 => {}
                    -1 => {
                        let err = std::io::Error::last_os_error();
                        if err.kind() == ErrorKind::Interrupted {
                            continue;
                        } else {
                            break;
                        }
                    }
                    _ => break,
                }
            }
            let _ = close(master_fd2);
        });

        self.forward_thread1 = Some(forward_thread1);
        self.forward_thread2 = Some(forward_thread2);

        Ok(())
    }

    fn create_pty(&self) -> Result<(RawFd, String), Box<dyn Error>> {
        let mut master_fd: i32 = 0;
        let mut slave_fd: i32 = 0;
        let mut slave_name_buf = [0u8; libc::PATH_MAX as usize];

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
            return Err(Box::new(std::io::Error::last_os_error()));
        }

        // Get the slave device name
        let c_str = unsafe { CStr::from_ptr(slave_name_buf.as_ptr() as *const libc::c_char) };
        let slave_name = c_str.to_str()?.to_string();

        unsafe {
            libc::close(slave_fd);
        }

        Ok((master_fd, slave_name))
    }

    fn write_nmea_messages(&mut self, gps_input_path: &str) -> Result<(), Box<dyn Error>> {
        use std::fs::OpenOptions;
        use std::io::Write;

        let gps_input = OpenOptions::new().write(true).open(gps_input_path)?;

        let mut writer = BufWriter::new(gps_input);

        while !self.shutdown_event.load(Ordering::SeqCst) {
            let sentences = self.generate_all_sentences();
            writer.write_all(sentences.as_bytes())?;
            writer.flush()?;
            println!("Sent to GPS input:\n{}", sentences);
            std::thread::sleep(std::time::Duration::from_secs(1));
        }

        Ok(())
    }

    fn cleanup(&self, gps_input_path: &str, gps_output_path: &str) -> Result<(), Box<dyn Error>> {
        if Path::new(gps_input_path).exists() {
            fs::remove_file(gps_input_path)?;
        }

        if Path::new(gps_output_path).exists() {
            fs::remove_file(gps_output_path)?;
        }

        println!("Cleaned up symlink links.");
        Ok(())
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

    fn complete_nmea_sentence(&self, sentence: &str) -> String {
        let checksum = self.calculate_checksum(sentence);
        format!("${}*{}\r\n", sentence, checksum)
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
        self.complete_nmea_sentence(&body)
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

    fn generate_gpgsa(&mut self, satellites: &Vec<u16>) -> String {
        static MODES: [char; 2] = ['M', 'A'];
        let fix_mode = MODES[self.random_int(0, 1) as usize];
        let mode = self.random_int(1, 3);
        let mut sv_id_str = String::new();
        sv_id_str.push_str(
            satellites
                .iter()
                .map(|x| x.to_string())
                .collect::<Vec<String>>()
                .join(",")
                .as_str(),
        );
        for _ in 0..(12 - satellites.len()) {
            sv_id_str.push_str(",");
        }

        let pdop = self.random_uniform(1.0, 10.0);
        let hdop = self.random_uniform(1.0, 10.0);
        let vdop = self.random_uniform(1.0, 10.0);

        let body = format!("GPGSA,{mode},{fix_mode},{sv_id_str},{pdop},{hdop},{vdop}");

        self.complete_nmea_sentence(&body)
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
}

// LocationData struct
struct LocationData {
    latitude: String,
    ns: char,
    longitude: String,
    ew: char,
}
