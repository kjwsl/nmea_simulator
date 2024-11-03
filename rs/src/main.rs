mod nmea_simulator;
use nmea_simulator::NmeaSimulator;
use std::env;
use std::error::Error;

fn print_help_msg(program_name: &str) {
    println!(
        "Usage: {} [input_file] [output_file]
        -h, --help  Display this help message",
        program_name
    );
}

fn main() -> Result<(), Box<dyn Error>> {
    // Parse command-line arguments
    let args: Vec<String> = env::args().collect();
    const DEFAULT_INPUT_PATH: &str = "/tmp/gps_input";
    const DEFAULT_OUTPUT_PATH: &str = "/tmp/gps_output";

    if args.len() == 2 && (args[1] == "-h" || args[1] == "--help") {
        print_help_msg(&args[0]);
        return Ok(());
    }

    let input_path = if args.len() > 1 {
        &args[1]
    } else {
        DEFAULT_INPUT_PATH
    };

    let output_path = if args.len() > 2 {
        &args[2]
    } else {
        DEFAULT_OUTPUT_PATH
    };

    let mut simulator = NmeaSimulator::new();
    simulator.start(input_path, output_path)?;
    Ok(())
}
