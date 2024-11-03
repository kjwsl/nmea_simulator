// src/pty_handler.rs

use libc::{openpty, ptsname};
use nix::unistd::{close, read, write};
use std::error::Error;
use std::ffi::CStr;
use std::fs;
use std::fs::OpenOptions;
use std::io::ErrorKind;
use std::os::unix::fs::symlink;
use std::os::unix::io::{AsRawFd, FromRawFd, RawFd};
use std::path::Path;
use std::ptr;
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc,
};
use std::thread;

pub struct PtyHandler {
    pub shutdown_event: Arc<AtomicBool>,
    pub master_fd1: Option<RawFd>,
    pub master_fd2: Option<RawFd>,
    pub forward_thread1: Option<thread::JoinHandle<()>>,
    pub forward_thread2: Option<thread::JoinHandle<()>>,
    // Keep the slave FDs open to prevent Bad file descriptor
    pub slave_fd1: Option<RawFd>,
    pub slave_fd2: Option<RawFd>,
}

impl PtyHandler {
    pub fn new(shutdown_event: Arc<AtomicBool>) -> Self {
        PtyHandler {
            shutdown_event,
            master_fd1: None,
            master_fd2: None,
            forward_thread1: None,
            forward_thread2: None,
            slave_fd1: None,
            slave_fd2: None,
        }
    }

    pub fn setup_linked_ptys(
        &mut self,
        gps_input_path: &str,
        gps_output_path: &str,
    ) -> Result<(), Box<dyn Error>> {
        // Create first PTY
        let (master_fd1, slave_name1) = self.create_pty()?;
        println!("Created PTY1: {}", slave_name1);

        // Create second PTY
        let (master_fd2, slave_name2) = self.create_pty()?;
        println!("Created PTY2: {}", slave_name2);

        // Create symbolic links
        self.create_symlink(&slave_name1, gps_input_path)?;
        self.create_symlink(&slave_name2, gps_output_path)?;

        // Open the slave ends to keep them open
        let slave_fd1 = OpenOptions::new()
            .read(true)
            .write(true)
            .open(gps_input_path)
            .map_err(|e| {
                eprintln!("Failed to open gps_input_path {}: {}", gps_input_path, e);
                e
            })?
            .as_raw_fd();
        self.slave_fd1 = Some(slave_fd1);
        println!("Opened gps_input_path: {}", gps_input_path);

        let slave_fd2 = OpenOptions::new()
            .read(true)
            .write(true)
            .open(gps_output_path)
            .map_err(|e| {
                eprintln!("Failed to open gps_output_path {}: {}", gps_output_path, e);
                e
            })?
            .as_raw_fd();
        self.slave_fd2 = Some(slave_fd2);
        println!("Opened gps_output_path: {}", gps_output_path);

        // Store master FDs for forwarding
        self.master_fd1 = Some(master_fd1);
        self.master_fd2 = Some(master_fd2);

        Ok(())
    }

    fn create_pty(&self) -> Result<(RawFd, String), Box<dyn Error>> {
        let mut master_fd: i32 = 0;
        let mut slave_fd: i32 = 0;

        let result = unsafe {
            openpty(
                &mut master_fd,
                &mut slave_fd,
                ptr::null_mut(),
                ptr::null_mut(),
                ptr::null_mut(),
            )
        };

        if result != 0 {
            eprintln!("Failed to create PTY");
            return Err(Box::new(std::io::Error::last_os_error()));
        }

        // Get the slave device name using ptsname
        let slave_name_ptr = unsafe { ptsname(master_fd) };
        if slave_name_ptr.is_null() {
            eprintln!("Failed to get slave device name");
            return Err(Box::new(std::io::Error::last_os_error()));
        }

        let c_str = unsafe { CStr::from_ptr(slave_name_ptr) };
        let slave_name = c_str.to_str()?.to_string();

        // Close the slave FD as we don't need it here
        unsafe {
            libc::close(slave_fd);
        }

        Ok((master_fd, slave_name))
    }

    fn create_symlink(&self, target: &str, link_path: &str) -> Result<(), Box<dyn Error>> {
        println!("Creating symlink from {} to {}", link_path, target);
        let link = Path::new(link_path);
        if link.exists() {
            fs::remove_file(link)?;
        }
        symlink(target, link)?;
        Ok(())
    }

    pub fn start_forwarding(&mut self) -> Result<(), Box<dyn Error>> {
        let shutdown_event = self.shutdown_event.clone();

        let master_fd1 = self.master_fd1.unwrap();
        let master_fd2 = self.master_fd2.unwrap();

        // Forward data from master_fd1 to master_fd2
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

        // Forward data from master_fd2 to master_fd1
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

        // Store the forwarding threads so we can join them later
        self.forward_thread1 = Some(forward_thread1);
        self.forward_thread2 = Some(forward_thread2);

        Ok(())
    }

    pub fn cleanup(
        &mut self,
        gps_input_path: &str,
        gps_output_path: &str,
    ) -> Result<(), Box<dyn Error>> {
        // Wait for forwarding threads to finish
        if let Some(thread) = self.forward_thread1.take() {
            let _ = thread.join();
        }
        if let Some(thread) = self.forward_thread2.take() {
            let _ = thread.join();
        }

        // Remove the symbolic links
        if Path::new(gps_input_path).exists() {
            fs::remove_file(gps_input_path)?;
        }
        if Path::new(gps_output_path).exists() {
            fs::remove_file(gps_output_path)?;
        }
        println!("Cleaned up symbolic links.");

        // Close the slave FDs
        if let Some(slave_fd1) = self.slave_fd1.take() {
            let _ = close(slave_fd1);
            println!("Closed slave_fd1");
        }
        if let Some(slave_fd2) = self.slave_fd2.take() {
            let _ = close(slave_fd2);
            println!("Closed slave_fd2");
        }

        Ok(())
    }
}
