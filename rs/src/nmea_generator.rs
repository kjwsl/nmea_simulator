use std::char::MAX;

use chrono::prelude::*;
use rand::{
    distributions::{Distribution, Uniform},
    rngs::ThreadRng,
    thread_rng, Rng,
};

pub struct RandomGenerator {
    rng: ThreadRng,
}

impl RandomGenerator {
    pub fn new() -> Self {
        RandomGenerator { rng: thread_rng() }
    }

    pub fn random_uniform(&mut self, min: f64, max: f64) -> f64 {
        let range = Uniform::from(min..max);
        range.sample(&mut self.rng)
    }

    pub fn random_int(&mut self, min: i32, max: i32) -> i32 {
        let range = Uniform::from(min..max);
        range.sample(&mut self.rng)
    }
}

#[derive(Debug, Clone)]
struct Satellite {
    constellation: Constellation,
    id: u16,
}

impl Satellite {
    pub fn new(constell: Constellation, id: u16) -> Self {
        Satellite {
            constellation: constell,
            id,
        }
    }

    pub fn new_random() -> Self {
        let constell = Constellation::get_random();
        let id = match constell {
            Constellation::GPS => RandomGenerator::new().random_int(1, 32),
            Constellation::GLONASS => RandomGenerator::new().random_int(65, 96),
            Constellation::GALILEO => RandomGenerator::new().random_int(1, 36),
            Constellation::BEIDOU => RandomGenerator::new().random_int(101, 136),
            Constellation::QZSS => RandomGenerator::new().random_int(183, 202),
        } as u16;
        Satellite {
            constellation: constell,
            id,
        }
    }
}

#[derive(Debug, Clone)]
pub enum Constellation {
    GPS,
    GLONASS,
    GALILEO,
    BEIDOU,
    QZSS,
}

impl Constellation {
    pub fn to_string(&self) -> String {
        match self {
            Constellation::GPS => "GPS".to_string(),
            Constellation::GLONASS => "GLONASS".to_string(),
            Constellation::GALILEO => "GALILEO".to_string(),
            Constellation::BEIDOU => "BEIDOU".to_string(),
            Constellation::QZSS => "QZSS".to_string(),
        }
    }
    pub fn to_code(&self) -> String {
        match self {
            Constellation::GPS => "GP".to_string(),
            Constellation::GLONASS => "GL".to_string(),
            Constellation::GALILEO => "GA".to_string(),
            Constellation::BEIDOU => "GB".to_string(),
            Constellation::QZSS => "GQ".to_string(),
        }
    }

    pub fn len() -> usize {
        Constellation::QZSS as usize + 1
    }

    pub fn get_random() -> Self {
        let mut rng = thread_rng();
        let range = Uniform::from(0..Constellation::len() - 1);
        let index = range.sample(&mut rng);

        match index {
            0 => Constellation::GPS,
            1 => Constellation::GLONASS,
            2 => Constellation::GALILEO,
            3 => Constellation::BEIDOU,
            _ => panic!("Invalid index"),
        }
    }
}

pub struct LocationData {
    pub latitude: String,
    pub ns: char,
    pub longitude: String,
    pub ew: char,
}

pub struct NmeaGenerator {
    rg: RandomGenerator,
}

impl NmeaGenerator {
    pub fn new() -> Self {
        NmeaGenerator {
            rg: RandomGenerator::new(),
        }
    }

    fn generate_location(&mut self) -> LocationData {
        let latitude = self.rg.random_uniform(-90.0, 90.0);
        let ns = if latitude >= 0.0 { 'N' } else { 'S' };
        let lat_deg = latitude.abs().floor();
        let lat_min = (latitude.abs() - lat_deg) * 60.0;

        let longitude = self.rg.random_uniform(-180.0, 180.0);
        let ew = if longitude >= 0.0 { 'E' } else { 'W' };
        let lon_deg = longitude.abs().floor();
        let lon_min = (longitude.abs() - lon_deg) * 60.0;

        LocationData {
            latitude: format!("{:02}{:07.4}", lat_deg, lat_min),
            ns,
            longitude: format!("{:03}{:07.4}", lon_deg, lon_min),
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
        for c in sentence.as_bytes() {
            checksum ^= c;
        }

        format!("{:02X}", checksum)
    }

    fn complete_sentence(&self, sentence: &str) -> String {
        format!("${}*{}\r\n", sentence, self.calculate_checksum(sentence))
    }

    fn generate_gga(&mut self, loc: &LocationData, num_satellites: i32) -> String {
        let utc_time = self.get_utc_time();
        let fix_quality = self.rg.random_int(0, 5);
        let altitude = self.rg.random_uniform(0.0, 1000.0);
        let hdop = self.rg.random_uniform(0.5, 10.0);
        let geoid_height = self.rg.random_uniform(-100.0, 100.0);

        let sentence = format!(
            "GPGGA,{},{},{},{},{},{},{},{:.1},{:.1},M,{:.1},M,,",
            utc_time,
            loc.latitude,
            loc.ns,
            loc.longitude,
            loc.ew,
            fix_quality,
            num_satellites,
            hdop,
            altitude,
            geoid_height
        );

        self.complete_sentence(&sentence)
    }

    fn generate_rmc(&mut self, loc: &LocationData) -> String {
        let utc_time = self.get_utc_time();
        let status = 'A';
        let latitude = format!("{}{}", loc.latitude, loc.ns);
        let longitude = format!("{}{}", loc.longitude, loc.ew);
        let speed = self.rg.random_uniform(0.0, 100.0);
        let course = self.rg.random_uniform(0.0, 360.0);
        let utc_date = self.get_utc_date();

        let sentence = format!(
            "GPRMC,{},{},{},{},{},{:.1},{:.1},{},{},,,",
            utc_time, status, latitude, loc.ns, longitude, loc.ew, speed, course, utc_date
        );

        self.complete_sentence(&sentence)
    }

    fn generate_gll(&mut self, loc: &LocationData) -> String {
        let latitude = format!("{}{}", loc.latitude, loc.ns);
        let longitude = format!("{}{}", loc.longitude, loc.ew);
        let utc_time = self.get_utc_time();
        let status = 'A';

        let sentence = format!(
            "GPGLL,{},{},{},{},{},{}",
            latitude, loc.ns, longitude, loc.ew, utc_time, status
        );

        self.complete_sentence(&sentence)
    }

    fn generate_gsa(&mut self, satellites: &Vec<Satellite>) -> String {
        let mode = 'A';
        let fix_type = 3;
        let mut msgs = Vec::new();

        let pdop = self.rg.random_uniform(0.5, 10.0);
        let hdop = self.rg.random_uniform(0.5, 10.0);
        let vdop = self.rg.random_uniform(0.5, 10.0);
        // Separte the satellites by constellation

        let mut sats_by_constell = vec![Vec::new(); Constellation::len()];
        for sat in satellites {
            let index = match sat.constellation {
                Constellation::GPS => 0,
                Constellation::GLONASS => 1,
                Constellation::GALILEO => 2,
                Constellation::BEIDOU => 3,
                Constellation::QZSS => 4,
            };
            sats_by_constell[index].push(sat);
        }

        (0..sats_by_constell.len()).for_each(|i| {
            let constellations = &sats_by_constell[i];
            if constellations.is_empty() {
                return;
            }

            let constell = constellations[0].constellation.to_code();
            let mut sats_str = constellations
                .iter()
                .map(|sat| sat.id.to_string())
                .collect::<Vec<String>>()
                .join(",");

            for _ in constellations.len()..12 {
                sats_str.push(',');
            }

            let sentence = self.complete_sentence(
                format!("{constell}GSA,{mode},{fix_type},{sats_str},{pdop:.1},{hdop:.1},{vdop:.1}")
                    .as_str(),
            );
            msgs.push(sentence);
        });

        msgs.join("")
    }

    fn generate_gsv(&mut self, satellites: &[Satellite]) -> String {
        let num_msgs = self.rg.random_int(satellites.len() as i32, 16) as usize;
        let mut msgs = Vec::new();

        for i in 0..num_msgs {
            let start = i * 4;
            let end = if i == num_msgs - 1 {
                satellites.len()
            } else {
                (i + 1) * 4
            };
            let sats = &satellites[start..end];

            let num_sats = sats.len();
            let num_sats_str = num_sats.to_string();
            let msg_num = (i + 1).to_string();
            let total_msgs = num_msgs.to_string();

            let mut sats_str = String::new();
            for sat in sats {
                sats_str.push_str(&format!("{},{},{},", sat.id, 0, 0));
            }

            let sentence = format!(
                "GPGSV,{total_msgs},{msg_num},{num_sats_str},{sats_str}",
                total_msgs = total_msgs,
                msg_num = msg_num,
                num_sats_str = num_sats_str,
                sats_str = sats_str
            );

            msgs.push(self.complete_sentence(&sentence));
        }

        msgs.join("")
    }

    fn generate_satellites(&mut self) -> Vec<Satellite> {
        let num_satellites = self.rg.random_int(4, 12);
        let mut satellites = Vec::new();
        for _ in 0..num_satellites {
            satellites.push(Satellite::new_random());
        }

        satellites
    }

    pub fn generate_sentences(&mut self) -> String {
        let loc = self.generate_location();
        let active_satellites = self.generate_satellites();
        let num_satellites = active_satellites.len() as i32;

        let mut sentences = String::new();
        sentences.push_str(&self.generate_rmc(&loc));
        sentences.push_str(&self.generate_gga(&loc, num_satellites));
        sentences.push_str(&self.generate_gll(&loc));
        sentences.push_str(&self.generate_gsa(&active_satellites));
        sentences.push_str(&self.generate_gsv(&active_satellites));

        sentences
    }
}
