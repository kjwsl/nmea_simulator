pub mod nmea_simulator;
use nmea_simulator::NmeaSimulator;
use std::env;
use std::error::Error;

fn main() -> Result<(), Box<dyn Error>> {
    // Parse command-line arguments
    let args: Vec<String> = env::args().collect();

    // Default values
    let mut pipe_path = String::new();
    let mut serial_port = String::new();
    let mut interval = 1.0;

    // Simple argument parsing
    let mut i = 1;
    while i < args.len() {
        match args[i].as_str() {
            "--pipe" => {
                if i + 1 < args.len() {
                    pipe_path = args[i + 1].clone();
                    i += 1;
                }
            }
            "--serial" => {
                if i + 1 < args.len() {
                    serial_port = args[i + 1].clone();
                    i += 1;
                }
            }
            "--interval" => {
                if i + 1 < args.len() {
                    interval = args[i + 1].parse::<f64>().unwrap_or(1.0);
                    i += 1;
                }
            }
            _ => {}
        }
        i += 1;
    }

    let mut simulator = NmeaSimulator::new(pipe_path, serial_port, interval);
    simulator.start()
}
