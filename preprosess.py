#! /usr/bin/python3
import operator

total_time = -1
actual_total_time = -1
stop_time = 1000

used_records = set()
mmap_table = []
data_sets = [[],[],[]]

total_records = 0
kernel_records = 0

def get_adress_info(inst : str, icache : str, itlb : str):
    global total_time
    total_time = -1

    # Read data from file
    data_1, mmap_1 = read_file(inst)
    data_2, mmap_2 = read_file(icache)
    data_3, mmap_3 = read_file(itlb)

    # Casting all addresses use only one mmap_table
    used_records_1 = cacting_addr_sapce(data_1, mmap_1)
    used_records_2 = cacting_addr_sapce(data_2, mmap_1)
    used_records_3 = cacting_addr_sapce(data_3, mmap_1)
    
    #save used records from mmap_table & save mmap_table
    global used_records
    used_records = used_records_1.union(used_records_2).union(used_records_3)
    global mmap_table
    mmap_table = mmap_1
    global data_sets
    data_sets[0] = data_1
    data_sets[1] = data_2
    data_sets[2] = data_3

'''
    This function read file and do some transformation under it:
        1. Finding most appearing PID among readed data
        2. Filtering readed data by PID, that described before
        3. Recalculate .ip field in data-list into tuple (id,offset)
            where id --- id in mmap_table; offset -- offset in mmap range
    As a result we give a pair of data-list and mmap_table
'''
def read_file(filename):
    global actual_total_time
    global stop_time
    print("Reading file:", filename)
    data = []
    mmap_table = []
    PIDs = {}
    with open(filename,'r') as f:
        state = ''
        for line in f:
            if line.strip() in ('r','m') and len(state) == 0 :
                state = line.strip()
                continue
            
            if state == 'r':
                state = ''
                pid, ip, time = map(int,line.split())
                if time > 60*60*1000:
                    continue
                if time >  stop_time:
                    break
                if pid not in PIDs:
                    PIDs[pid] = 1
                else:
                    PIDs[pid] += 1
                data.append([pid, ip, time])
                actual_total_time = max(actual_total_time, time)
            if state == 'm':
                state = ''
                pid, addr, length, _ , name = line.split()
                pid = int(pid)
                addr = int(addr)
                length = int(length)
                mmap_table.append((pid, addr, length, name))

    #Finding PID that have bigger frequency
    PID = max(PIDs.items(),key = operator.itemgetter(1))[0]

    #Then filtering mmap_table
    mmap_table = list(filter(lambda x: x[0] == PID, mmap_table))
    mmap_table.sort(key = lambda item: item[1])


    print('Number of records', len(data))
    #Divide adresses by group that we have read from mmap_table
    recalc_addr_space(data, mmap_table)

    #Return list of data and mmap_table
    return data, mmap_table

'''
    Recalculate .ip field in data-list into tuple (id,offset)
        where id --- id in mmap_table; offset -- offset in mmap range
'''
def recalc_addr_space(readed_data, mmap_table):
    for i in range(len(readed_data)):
        pid, ip, _ = readed_data[i]

        found = False
        for j in range(len(mmap_table)):
            if pid == mmap_table[j][0] and mmap_table[j][1] <= ip <= mmap_table[j][1] + mmap_table[j][2] - 1:
                delta = ip - mmap_table[j][1]
                readed_data[i][1] = (j+1, delta)
                found = True
                break
        
        if not found:
            readed_data[i][1] = (0, ip)

'''
    Castring pair of (id, offset) into linear adress
    and delete all adresses that are not in any mmap_block
'''
def cacting_addr_sapce(readed_data, mmap_table):
    used_mmap_records = set()
    max_time = 0

    global total_records
    total_records = len(readed_data)

    #delete all adreses with no mmap region
    new_ = list(filter(lambda x: x[1][0] > 0, readed_data))
    readed_data.clear()
    readed_data += new_

    global kernel_records
    kernel_records = total_records - len(readed_data)

    for i in range(len(readed_data)):
        _, pair, time = readed_data[i]
        if pair[0] == 0:
            readed_data[i][1] = pair[1]
        else:
            readed_data[i][1] = mmap_table[pair[0]-1][1] + pair[1]
            used_mmap_records.add(pair[0]-1)
            max_time = max(max_time, time)
        readed_data[i] = tuple(readed_data[i])

    #Update total working_time
    global total_time
    total_time = max_time if total_time < 0 else min(max_time, total_time)
    return used_mmap_records
