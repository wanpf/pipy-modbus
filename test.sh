#!/bin/bash

mkdir -p /tmp/data

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$PWD

./pipy main.js

