# Radar Image Fusion

Реализация перевода полярных координат в декартовы, поиск сдвига между снимками.

## Зависимости

```bash
sudo apt install -y libtiff-dev build-essential
```

## Сборка и запуск

```bash
cd src
make
make run-radar
make run-phase
```

Эквивалент:

```bash
./radar ../data/csv_data/radar.csv ../data/to_process_data/radar_cartesian.ppm
./phase_correlation ../data/land_data/Hrnnl.tif ../data/land_data/KHVtG.tif ../data/final_images/CombLand.tif
```

## Что делает каждая программа

- **`radar`** — читает CSV, перевожит из полярный координат в декартовы, сохраняет результат.
- **`phase_correlation`** — находит сдвиг между двумя TIFF через FFT, сохраняет RGB-наложение.
