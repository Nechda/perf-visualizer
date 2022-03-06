/*
    Run:
    perf record -e instructions:u,L1-icache-load-misses:u,iTLB-load-misses:u ./bnch.out
    ./perf_parser
*/

#include <bitset>
#include <cassert>
#include <cstring>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <fcntl.h>
#include <linux/perf_event.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define dbg(line) std::cout << (#line) << std::endl; line
#define named_emit(var) std::cout << #var << " = " << var << std::endl;


/* Some structures from pref sources */
struct perf_file_section {
	__u64 offset;
	__u64 size;
};

struct perf_file_attr {
    perf_event_attr	attr;
    perf_file_section ids;
};

struct perf_file_header {
	__u64				magic;
	__u64				size;
	__u64				attr_size;
	perf_file_section	attrs;
	perf_file_section	data;
	perf_file_section	event_types;
};

/* Just reading full file into memory */
int read_full_file(const char* filename, char** out_string) {
    if (!filename || !out_string)
        return EXIT_FAILURE;

    FILE* input_file = fopen(filename, "rb");
    if (!input_file ? ferror(input_file) : 0)
        return EXIT_FAILURE;

    fseek(input_file, 0, SEEK_END);
    long fsize = ftell(input_file);
    fseek(input_file, 0, SEEK_SET);

    char* string = (char*)calloc(fsize + 8, sizeof(char));
    if (!string)
        return EXIT_FAILURE;

    unsigned n_read_bytes = fread(string, sizeof(char), fsize, input_file);
    fclose(input_file);
    string[fsize] = 0;

    *out_string = string;

    return n_read_bytes;
}

/* Let's expand the event types to make it easier to work with cache events. */
enum perf_hw_id_extention {
    PERF_COUNT_HW_CACHE_L1I_READ_MISSES = PERF_COUNT_HW_MAX,
    PERF_COUNT_HW_CACHE_ITLB_READ_MISSES,
    PERF_COUNT_HW_EXTENTION_MAX
};

/* Simple range structure for quick identification event_id */
struct range {
    __u32 b;
    __u32 e;
    bool in(__u32 c) { return b <= c && c <= e; }
};
range event_ranges[PERF_COUNT_HW_EXTENTION_MAX] = {};

/* Returns string from pref_hw_id */
const char* get_pref_hw_id_msg(__u32 id) {
    switch (id) {
        #define make_case(cs) case cs : return #cs; break;
        make_case(PERF_COUNT_HW_CPU_CYCLES);
        make_case(PERF_COUNT_HW_INSTRUCTIONS);
        make_case(PERF_COUNT_HW_CACHE_REFERENCES);
        make_case(PERF_COUNT_HW_CACHE_MISSES);
        make_case(PERF_COUNT_HW_BRANCH_INSTRUCTIONS);
        make_case(PERF_COUNT_HW_BRANCH_MISSES);
        make_case(PERF_COUNT_HW_BUS_CYCLES);
        make_case(PERF_COUNT_HW_STALLED_CYCLES_FRONTEND);
        make_case(PERF_COUNT_HW_STALLED_CYCLES_BACKEND);
        make_case(PERF_COUNT_HW_REF_CPU_CYCLES);
        make_case(PERF_COUNT_HW_CACHE_L1I_READ_MISSES);
        make_case(PERF_COUNT_HW_CACHE_ITLB_READ_MISSES);
        #undef make_case
        default:
            return "Undefined perf_hw_id, for more info "
                   "see `perf_event.h` line 45.";
    }
}


/* Simple config-dependent PERF_RECORD_SAMPLE reader */
struct samped_data_t {
    __u64 identifier = 0xFF;
    __u64 ip = 0xFF;
    __u32 pid = 0xFF , tid = 0xFF;
    __u64 time = 0xFF;
    __u64 addr = 0xFF;
    __u64 id = 0xFF;
    __u64 stream_id = 0xFF;
    __u32 cpu = 0xFF, res = 0xFF;
    __u64 period = 0xFF;
    __u64 data_src = 0xFF;
    
    void read(char* data) {
        assert(data);
        cur_ptr = data;
        #define read_32_if(flag, field)\
            if((config & flag) == flag) field = read_32()
        #define read_64_if(flag, field)\
            if((config & flag) == flag) field = read_64()

        read_64_if(PERF_SAMPLE_IDENTIFIER, identifier);
        read_64_if(PERF_SAMPLE_IP, ip);
        read_32_if(PERF_SAMPLE_TID, pid);
        read_32_if(PERF_SAMPLE_TID, tid);
        read_64_if(PERF_SAMPLE_TIME, time);
        read_64_if(PERF_SAMPLE_ADDR, addr);
        read_64_if(PERF_SAMPLE_ID, id);
        read_64_if(PERF_SAMPLE_STREAM_ID, stream_id);
        read_32_if(PERF_SAMPLE_CPU, cpu);
        read_32_if(PERF_SAMPLE_CPU, res);
        read_64_if(PERF_SAMPLE_PERIOD, period);
        /* here may be another fields */
        read_64_if(PERF_SAMPLE_DATA_SRC, data_src);

        #undef read_32_if
        #undef read_64_if
    }

    static void set_config(__u32 cfg) {
        config = cfg;
    } 

    private:

    char* cur_ptr = NULL;
    static __u32 config;

    __u32 read_32() {
        __u32 res = *((__u32*)cur_ptr);
        cur_ptr += sizeof(__u32);
        return res;
    }

    __u64 read_64() {
        __u64 res = *((__u64*)cur_ptr);
        cur_ptr += sizeof(__u64);
        return res;
    }
};
__u32 samped_data_t::config = 0;

/* Minimum info from PERF_RECORD_SAMPLE */
struct record_info_t {
    __u32 pid;
    __u64 action_id;
    __u64 ip;
    __u64 addr;
    __u64 time;
    __u64 data_src;
};

struct mmap_info_t {
    __u32 pid;
    __u64 addr;
    __u64 len;
    __u64 pgoff;
    std::string filename;
};

/* Writing record's info to the file, filtered by `perf_hw_id` */
void write_event_in_file(
    const std::string filename,
    __u32 event_type,
    const std::vector<record_info_t>& records,
    const std::vector<mmap_info_t>& mmap_records
    ) {
    std::ofstream out_file;
    out_file.open(filename);

    for(const auto& it : mmap_records) {
        out_file << "m\n" << it.pid << " " << it.addr << " " << it.len << " " << it.pgoff << " " << it.filename <<std::endl;
    }

    __u64 start_time = records[0].time;
    for (const auto& it : records) {
        if (event_ranges[event_type].in(it.action_id))
            out_file << "r\n" << it.pid << " " << it.addr << " " << (__u64)((it.time-start_time) / 1000000)
                     << std::endl;
    }
    out_file.close();
}

__u32 read_perf_file_attr(const perf_file_attr* pp_file_attr, const char* data, __u32 actual_struct_size) {
    assert(pp_file_attr);
    assert(data);
    /*      Quite good documentation you can find in man    */
    /* the article `perf_event_open(2) — Linux manual page` */

    const perf_event_attr* pp_event_attr = &pp_file_attr->attr;
    /*   Field `.sample_type` is a __u32 filed its bits  */
    /* represent which data we expect to see in records. */
    samped_data_t::set_config(pp_event_attr->sample_type);

    /* Here some difference in coding common hardware events and cache events */
    __u32 event_idx = 0;
    if (pp_event_attr->type == PERF_TYPE_HW_CACHE) {
        /* See man for good undestanding */
        std::cout << "=====[PERF_TYPE_HW_CACHE]=====";
        std::cout << std::endl << "Need addition information about .config field:";
        std::cout << std::endl << "          perf_hw_cache_id = " << (pp_event_attr->config & 0xFF);
        std::cout << std::endl << "       perf_hw_cache_op_id = " << ((pp_event_attr->config >> 8) & 0xFF);
        std::cout << std::endl << "perf_hw_cache_op_result_id = " << ((pp_event_attr->config >> 16) & 0xFF);
        std::cout << std::endl << "==============================";
        std::cout << std::endl;
        #define gen_CACHE_MISS_flag(ch)\
        ((PERF_COUNT_HW_CACHE_RESULT_MISS << 16) | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (ch))

        if(pp_event_attr->config == gen_CACHE_MISS_flag(PERF_COUNT_HW_CACHE_L1I))
            event_idx = PERF_COUNT_HW_CACHE_L1I_READ_MISSES;
        if(pp_event_attr->config == gen_CACHE_MISS_flag(PERF_COUNT_HW_CACHE_ITLB))
            event_idx = PERF_COUNT_HW_CACHE_ITLB_READ_MISSES;

        #undef gen_CACHE_MISS_flag
    } else {
        /* If our event is PERF_TYPE_HARDWARE, so the .config field tells us */
        /* what harwdare event id describes this perf_event_attr structure.  */
        /* For more inf see `enum perf_hw_id` in `perf_event.h : line 45`    */
        std::cout << "=====[PERF_TYPE_HARDWARE]=====";
        std::cout << std::endl << "==============================";
        std::cout << std::endl;
        event_idx = pp_event_attr->config;
    }
    /* Now we are interested in reading ids of events, that we will get from records. */
    /* So it means, that there is a field .id in records (see samped_data_t), and for */
    /* associating this id to your event list you should read id list.                */

    /* According to my observation list of ids is a range of numbers, so I decided */
    /* just save only the first and the last item from this list.                  */
    perf_file_section* ids =
    (perf_file_section*)((char*)pp_file_attr + actual_struct_size - sizeof(perf_file_section));
    __u64* p_ids = (__u64*)(data + ids->offset);
    event_ranges[event_idx].b = *p_ids;
    p_ids += ids->size/sizeof(__u64) - 1;
    event_ranges[event_idx].e = *p_ids;
    return event_ranges[event_idx].b;
}


int main(int argc, char** argv) {
    char* data;
    int size = 0;
    /*   You may read file in other way, for example via `mmap` function.   */
    if ((size = read_full_file("perf.data", &data)) == EXIT_FAILURE)
        return EXIT_FAILURE;
    
    /* Fistly read list of attributes, from this we need just ids */
    /*  of events and infomation about data in record structures. */
    perf_file_header* ph = (perf_file_header*)data;
    __u32 saved_id = 0;
    bool only_one_event = ph->attrs.size / ph->attr_size == 1;
    for (__u32 i = 0; i < ph->attrs.size / ph->attr_size; i++)
    {
        char* ptr = (data + ph->attrs.offset) + i * ph->attr_size;
        saved_id = std::max(0U,read_perf_file_attr((perf_file_attr*)ptr, data, ph->attr_size));
    }
   
    std::vector<record_info_t> records;
    std::vector<mmap_info_t> mmap_records;
    char* data_ptr = data + ph->data.offset;
    for (__u64 readed_bytes = 0; readed_bytes < ph->data.size;) {
        /*       Any record consist header and then info.        */
        /* From the header we need just a .size and .type fileds */
        perf_event_header* pp_event_header = (perf_event_header*)data_ptr;


        /*       There is a problem here related to undescribed in `perf_event.h` record's type.      */
        /*   In the first time you can get invalid type of record (grather or equal PERF_RECORD_MAX)  */
        /*  Don`t panic, you can check the correctness of your parsing. Just use perf report -D, this */
        /*      command can show you all infomation about records that have beed written by perf.     */
        if (!(pp_event_header->size < 512 && pp_event_header->type < 100)) {
            std::cout << "bad size = " << pp_event_header->size << std::endl;
            std::cout << "bad type = " << pp_event_header->type << std::endl;
            break;
        }

        /* The iternal of record can be different, and it depends on perf_event_attr.sample_type */
        if (pp_event_header->type == PERF_RECORD_SAMPLE) {
            /* Here I use simple reader of records. */
            samped_data_t sampled_data;
            sampled_data.read((char*)(data_ptr + sizeof(perf_event_header)));
            
            records.push_back({
                sampled_data.pid,
                only_one_event ? saved_id : sampled_data.id,
                sampled_data.addr,
                sampled_data.ip,
                sampled_data.time,
                sampled_data.data_src
            });
        }
        
        if (pp_event_header->type == PERF_RECORD_MMAP2) {
            struct mmap2_data_t {
                __u32 pid, tid;
                __u64 addr;
                __u64 len;
                __u64 pgoff;
                __u32 maj, min;
                __u64 ino;
                __u64 ino_gereration;
                __u32 prot,flags;
                char filename[1];
            };
            mmap2_data_t* mmap_data = (mmap2_data_t*)(data_ptr + sizeof(perf_event_header));
            std::string filename(mmap_data->filename, strlen(mmap_data->filename));
            mmap_records.push_back({
                mmap_data->pid,
                mmap_data->addr,
                mmap_data->len,
                mmap_data->pgoff,
                filename
            });
        }

        if (pp_event_header->type == 79) {
            struct time_conv {
		        struct perf_event_header header;
		        __u64 time_shift;
		        __u64 time_mult;
		        __u64 time_zero;
	        };
	        time_conv* tc = (time_conv*)data_ptr;
	        std::cout << "~~~~[time conv event]~~~~" << std::endl;
	        named_emit(tc->time_shift);
	        named_emit(tc->time_mult);
	        named_emit(tc->time_zero);
	        std::cout << "~~~~~~~~~~~~~~~~~~~~~~~~~" << std::endl;
        }
        
        data_ptr += pp_event_header->size;
        readed_bytes += pp_event_header->size;
    }


    /* Now calculate how many events of each type we have. */
    unsigned n_records[PERF_COUNT_HW_EXTENTION_MAX] = {};
    __u64 min_time = -1;
    __u64 max_time = 0;
    for (const auto& it : records)
    {
        min_time = std::min(min_time, it.time);
        max_time = std::max(max_time, it.time);
        for (int i = PERF_COUNT_HW_CPU_CYCLES; PERF_COUNT_HW_EXTENTION_MAX != i; i++)
            n_records[i] += event_ranges[i].in(it.action_id);
    }

    std::cout << "Total time duration: " << (max_time - min_time) / 1000000 << " ms" << std::endl; 

    /* And print info. */
    std::cout << "Events distribution:" << std::endl;
    for (int i = PERF_COUNT_HW_CPU_CYCLES; PERF_COUNT_HW_EXTENTION_MAX != i; i++)
        if(n_records[i])
            std::cout << std::setw(40) << get_pref_hw_id_msg(i) << " = " << n_records[i] << std::endl;
    std::cout << "Total records = " << records.size() << std::endl;

    /* Also write info into files. */
    if(n_records[PERF_COUNT_HW_CPU_CYCLES])
        write_event_in_file("data_inst.txt", PERF_COUNT_HW_CPU_CYCLES, records, mmap_records);
    if(n_records[PERF_COUNT_HW_CACHE_L1I_READ_MISSES])
        write_event_in_file("data_icache.txt", PERF_COUNT_HW_CACHE_L1I_READ_MISSES, records, mmap_records);
    if(n_records[PERF_COUNT_HW_CACHE_ITLB_READ_MISSES])
        write_event_in_file("data_itlb.txt", PERF_COUNT_HW_CACHE_ITLB_READ_MISSES, records, mmap_records);
    
    free(data);

    return 0;
}
