// src/main.rs

mod nmea_generator;
mod pty_handler;

use nmea_generator::NmeaGenerator;
use pty_handler::PtyHandler;
use signal_hook::consts::SIGINT;
use signal_hook::iterator::Signals;
use std::error::Error;
use std::fs::OpenOptions;
use std::io::Write;
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc,
};
use std::thread;
use std::time::Duration;

fn main() -> Result<(), Box<dyn Error>> {
    let shutdown_event = Arc::new(AtomicBool::new(false));

    // Set up signal handler
    let shutdown_event_clone = shutdown_event.clone();
    let mut signals = Signals::new(&[SIGINT])?;

    thread::spawn(move || {
        for _ in signals.forever() {
            println!("\nKeyboardInterrupt received. Shutting down...");
            shutdown_event_clone.store(true, Ordering::SeqCst);
        }
    });

    // Ensure correct number of arguments
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 3 {
        eprintln!("Usage: {} <gps_input_path> <gps_output_path>", args[0]);
        std::process::exit(1);
    }

    let gps_input_path = &args[1];
    let gps_output_path = &args[2];

    // Initialize PTY handler
    let mut pty_handler = PtyHandler::new(shutdown_event.clone());
    pty_handler.setup_linked_ptys(gps_input_path, gps_output_path)?;
    pty_handler.start_forwarding()?;

    // Initialize NMEA generator
    let mut nmea_generator = NmeaGenerator::new();

    // Write NMEA messages to /tmp/gps_input
    if let Err(e) = write_nmea_messages(gps_input_path, &mut nmea_generator, shutdown_event.clone())
    {
        eprintln!("Error writing NMEA messages: {}", e);
    }

    // Perform cleanup
    pty_handler.cleanup(gps_input_path, gps_output_path)?;

    Ok(())
}

fn write_nmea_messages(
    gps_input_path: &str,
    nmea_generator: &mut NmeaGenerator,
    shutdown_event: Arc<AtomicBool>,
) -> Result<(), Box<dyn Error>> {
    // Open the GPS input PTY for writing
    println!("Opening GPS input path: {}", gps_input_path);
    let gps_input = OpenOptions::new()
        .write(true)
        .open(gps_input_path)
        .map_err(|e| {
            eprintln!("Failed to open {}: {}", gps_input_path, e);
            e
        })?;

    let mut writer = std::io::BufWriter::new(gps_input);

    // Main loop to write NMEA messages
    while !shutdown_event.load(Ordering::SeqCst) {
        let sentence = nmea_generator.generate_sentences();
        writer.write_all(sentence.as_bytes())?;
        writer.flush()?;
        println!("Sent to {}:\n{}", gps_input_path, sentence.trim());
        thread::sleep(Duration::from_secs(1));
    }

    Ok(())
}
