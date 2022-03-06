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

struct time_conv {
    struct perf_event_header header;
    __u64 time_shift;
    __u64 time_mult;
    __u64 time_zero;
};