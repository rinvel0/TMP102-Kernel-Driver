#!/bin/bash
while true; do
	clear

	# Read raw 16-bit data from TMP102
	raw_temp=$(i2cget -y 3 0x48 0x00 w)
	echo "Raw temperature data: $raw_temp"

	# Swap the bytes to get the correct 16-bit data
	swapped=$(( ((raw_temp & 0xFF) << 8) | (raw_temp >> 8) ))


	# swapped=$( ((raw_temp << 8) | (raw_temp >> 8) ))
	echo "Swapped temperature data (hex): $(printf '0x%04X' $swapped)"

	# Mask the lower 4 bits (keeping the upper 12 bits)
	temp_raw=$((swapped >> 4))
	echo "Masked temperature (upper 12 bits): $temp_raw"

	# Check if the temperature is negative (sign extension for 12-bit)
	if ((temp_raw & 0x800)); then
	  temp_raw=$((temp_raw - 0x1000))  # Sign-extend to negative value
	  echo "Sign-extended negative temperature: $temp_raw"
	else
	  echo "Positive temperature: $temp_raw"
	fi

	# Convert to Celsius (TMP102 has 0.0625°C per unit)
	temperature=$(echo "scale=4; $temp_raw * 0.0625" | bc)
	echo "Temperature in Celsius: $temperature"
	sleep 0.2
done
