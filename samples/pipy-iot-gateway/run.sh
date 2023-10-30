#!/bin/bash

cd /root/pipy-iot-gateway

mkdir -p data

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$PWD

nohup /root/pipy-iot-gateway/pipy /root/pipy-iot-gateway/main.js &


