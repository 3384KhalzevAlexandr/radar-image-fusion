CC     = gcc
CFLAGS = -O2 -Wall -Wextra

RADAR_IN   = ../data/csv_data/radar.csv
RADAR_OUT  = ../data/to_process_data/radar_cartesian.ppm

PASS1      = ../data/land_data/Hrnnl.tif
PASS2      = ../data/land_data/KHVtG.tif
PHASE_OUT  = ../data/final_images/CombLand.tif

all: radar phase_correlation

radar: radar.c
	$(CC) radar.c -o radar -lm $(CFLAGS)

phase_correlation: phase_correlation.c
	$(CC) phase_correlation.c -o phase_correlation -ltiff -lm $(CFLAGS)

run-radar: radar
	./radar $(RADAR_IN) $(RADAR_OUT)

run-phase: phase_correlation
	./phase_correlation $(PASS1) $(PASS2) $(PHASE_OUT)

clean:
	rm -f radar phase_correlation

.PHONY: all run-radar run-phase clean