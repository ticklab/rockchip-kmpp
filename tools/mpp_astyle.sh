#!/bin/bash
set -e
./astyle --quiet --options=astylerc "../*.c" "../*.h"

