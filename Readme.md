# Perf-visualizer
This repository contain a source code of a simple `perf.data` visualizer.
The project has two main parts: `perf_parser` and `record.py`. The first 
util reads `perf.data` file, extract from it records' info and write result it text files . The second
is for generating raw 2d histograms from files that produces by the first utility.

# How to get a plot?
## Preparation
Firstry you need build perf_parser:
```bash
g++ -O2 perf_parser.cpp -o perf_parser
```

Then collect data from perf:
```bash
perf record -e instructions:u,L1-icache-load-misses:u,iTLB-load-misses:u <benchmark>
```

## Plot generation

Generate plot!
```bash
python3 plot_gen.py
```
The command above generate two files `raw_plot.png` and `plog.png`.


## Plot gerenationg (new version)

At the current time I'm at the process of migrating to the new intermediate text representation, and you can also generate `raw` plot
with the command below:
```bash
python3 records.py <width> <height> <regexpr>
python3 records.py 1920 1280 '.*cc1plus.*'
```


# Example of plots

## Plot with description (legacy)
![legacy-plot](res/plot.png?raw=true)

## Raw plot (new)
![new-raw-plot](res/new_raw.png?raw=true)