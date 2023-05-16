from collections import defaultdict
import operator
import numpy as np
from PIL import Image
import re
import argparse
import matplotlib.pyplot as plt

class Record:
    def __init__(self, record_type : str, pid : int, ip : int, time : int):
        self.record_type = record_type
        self.pid = pid
        self.ip = ip
        self.time = time
    
    def __repr__(self) -> str:
        return self.__str__()
    
    def __str__(self) -> str:
        return f"PID: {self.pid}; IP: {self.ip}; Time: {self.time}"

    @staticmethod
    def from_file(lines):
        is_record = lambda s: s in ('cycles', 'cache', 'tlb')
        to_int = lambda s: int(s) if s.isdigit() else s
        i, N = 0, len(lines)
        ret = []
        while i < N:
            li = lines[i].strip()
            li = [to_int(it.strip()) for it in li.split()]
            if is_record(li[0]): ret.append(Record(*li))
            i += 1
        return ret

class MMAPRecord:
    def __init__(self, pid : int, addr : int, length : int, pageoff : int, name : str):
        self.pid = pid
        self.addr = addr
        self.length = length
        self.name = name
    
    def __repr__(self) -> str:
        return self.__str__()
    
    def __str__(self) -> str:
        return f"PID: {self.pid}; Start: {self.addr}; Len: {self.length}; Name: {self.name}"

    def is_include(self, addr : int):
        return self.addr <= addr < self.addr + self.length

    @staticmethod
    def from_file(lines):
        is_mmap = lambda s: s == 'mmap'
        to_int = lambda s: int(s) if s.isdigit() else s
        i, N = 0, len(lines)
        ret = []
        while i < N:
            li = lines[i].strip()
            li = [to_int(it.strip()) for it in li.split()]
            if is_mmap(li[0]):ret.append(MMAPRecord(*li[1:]))
            i += 1
        return ret

def read_file(filename):
    print("Read file:", filename)
    lines = []
    with open(filename, 'r') as f: lines = [li for li in f]
    return Record.from_file(lines), MMAPRecord.from_file(lines)

def rewrite_with_mmap(records : list[Record], mmap_records : list[MMAPRecord]):
    # 1. Sort records by start addr
    mmap_records.sort(key = lambda it: it.addr)
    # 2. Rewrite each .ip field with a pair (mmap_record_id, offset)
    PAGE_SIZE = 4096
    for i in range(len(records)):
        for mmap_id, mmap_record in enumerate(mmap_records):
            if mmap_record.is_include(records[i].ip):
                off = records[i].ip - mmap_record.addr
                records[i].ip = (mmap_id, off // PAGE_SIZE)
                break
    # 3. remove records with no mmap
    records = [it for it in records if type(it.ip) is tuple]
    # 4. Save all offsets for each mmap_record 
    mmap_offsets = defaultdict(set)
    for r in records:
        mmap_id, mmap_offset = r.ip
        mmap_offsets[mmap_id].add(mmap_offset)
    # 5. Replace sets of offsets into list
    mmap_offsets = {k : list(sorted(list(mmap_offsets[k]))) for k in mmap_offsets.keys()}
    # 6. Create a dict for each mmap_record, offset -> id in sorted list
    make_dict = lambda li: { it[1] : it[0] for it in enumerate(li)}
    mmap_offsets = {k : make_dict(mmap_offsets[k]) for k in mmap_offsets.keys()}
    # 7. Calculate base offset for each mmap_record
    base_offsets = [0]
    for i in range(len(mmap_records)): base_offsets.append(base_offsets[-1] + len(mmap_offsets.get(i, [])))
    # 8. Rewrite .ip field for each record
    for i in range(len(records)):
        mmap_id, mmap_offset = records[i].ip
        records[i].ip = base_offsets[mmap_id] + mmap_offsets[mmap_id][mmap_offset]

    n_pages = base_offsets[-1]
    return records, n_pages

def filter_data(records : list[Record], mmap_table : list[MMAPRecord]):
    #found most frequent PID
    PIDs = defaultdict(lambda: 0)
    for r in records: PIDs[r.pid] += 1
    PID = max(PIDs.items(), key = operator.itemgetter(1))[0]

    #remove records with another PID
    records = list(filter(lambda r: r.pid == PID, records))
    mmap_table = list(filter(lambda r: r.pid == PID, mmap_table))
    return records, mmap_table


def generate_plot(plot_width:int, plot_height:int, scale:int, regexpr:str):
    records, mmap_records = read_file("new_format.txt")
    records, mmap_records = filter_data(records, mmap_records)
    mmap_records = [r for r in mmap_records if re.match(regexpr, r.name) != None]
    records, n_pages = rewrite_with_mmap(records, mmap_records)

    total_time = min(max(r.time for r in records), 120*1000)
    print(f"Total time of measurement {total_time} [ms]; Used pages: {n_pages}")

    # Set size of RAW plot
    y_cells = plot_height
    x_cells = plot_width

    CYCLES_CHANNEL, CACHE_CHANNEL, TLB_CHANNEL  = 2, 1, 0
    type_to_channel = {'cycles' : CYCLES_CHANNEL, 'cache' : CACHE_CHANNEL, 'tlb' : TLB_CHANNEL}

    # Write data into image, without any normalization
    data = np.zeros((y_cells, x_cells, 3), 'float64')
    skip_record = lambda x, y: x >= x_cells or y >= y_cells
    for r in records:
        x, y = r.time, r.ip
        x = int(x / total_time * x_cells)
        y = int(y / n_pages * y_cells)
        if skip_record(x, y): continue
        data[y][x][type_to_channel[r.record_type]] += 1

    # Do logarithmic normalization
    data = np.log10(1 + data)
    maxes = np.amax(data, axis = (0, 1))
    data = data / maxes

    # Extract cycles channel
    data = data[:,:,CYCLES_CHANNEL]
    cm = plt.get_cmap('Blues')
    data = cm(data)

    # Resize image
    data = np.kron(data, np.ones((scale, scale,1)))

    rgb_array = np.uint8(255 * data)
    #rgb_array = np.flipud(rgb_array)
    img = Image.fromarray(rgb_array)
    img.save("n-raw-plot.png")

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("width", type = int, help = "The width of generated plot")
    parser.add_argument("height", type = int, help = "The height of generated plot")
    parser.add_argument("scale", type = int, help = "Amount of merged pixels")
    parser.add_argument("regexp", type = str, help = "Regexpr for mmap records sorting")
    args = parser.parse_args()
    generate_plot(args.width, args.height, args.scale, args.regexp)
