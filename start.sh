#!/bin/bash
socat -dd pty,raw,echo=0,link=/tmp/gps_input pty,raw,echo=0,link=/tmp/gps_output &
build/fakegps --pipe /tmp/gps_input