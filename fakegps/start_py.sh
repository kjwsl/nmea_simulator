#!/bin/bash

socat -dd pty,raw,echo=0,link=/tmp/gps_input pty,raw,echo=0,link=/tmp/gps_output &
python3 fakegps.py --serial /tmp/gps_input
