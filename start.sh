#!/bin/bash
socat -dd pty,raw,echo=0,link=/tmp/gps_input pty,raw,echo=0,link=/tmp/gps_output &
sleep 1
build/fakegps --serial /tmp/gps_input
