#!/bin/bash
python3 tools/mkhive.py boot/data/SYSTEM && python3 tools/mkdisk.py && ./boot.sh --serial
