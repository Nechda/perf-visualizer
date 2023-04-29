/*
    Run:
    perf record -e instructions:u,L1-icache-load-misses:u,iTLB-load-misses:u
   ./bnch.out
   ./perf_parser && python3 plot_gen.py


   Or:
   ./perf_parser && python3 records.py
*/

#include <bitset>
#include <cassert>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <streambuf>
#include <sys/mman.h>
#include <sys/stat.h>
#include <vector>
#include <type_traits>

#include "perf_structures.inl"
#include "perf_samples.inl"

#define named_emit(var) std::cout << #var << " = " << var << std::endl;

struct Pointer {
public:
  template <typename T> Pointer(T ptr) : raw_ptr((void *)(ptr)) {}
  char *AsByte() const && { return (char *)raw_ptr; }
  template <typename T> T *As() const && { return (T *)raw_ptr; }

private:
  void *raw_ptr = nullptr;
};

int read_full_file(const char *filename, std::string &buff) {
  std::ifstream t(filename);
  if (!t.is_open())
    return EXIT_FAILURE;

  buff.clear();
  t.seekg(0, std::ios::end);
  buff.reserve(t.tellg());
  t.seekg(0, std::ios::beg);

  buff.assign((std::istreambuf_iterator<char>(t)),
              std::istreambuf_iterator<char>());
  return EXIT_SUCCESS;
}

/* Let's expand the event types to make it easier to work with cache events. */
enum perf_hw_id_extention {
  PERF_COUNT_HW_CACHE_L1I_READ_MISSES = PERF_COUNT_HW_MAX,
  PERF_COUNT_HW_CACHE_ITLB_READ_MISSES,
  PERF_COUNT_HW_EXTENTION_MAX
};

/* Simple range structure for quick identification event_id */
struct range {
  size_t b;
  size_t e;
  bool in(size_t c) { return b <= c && c <= e; }
};
range event_ranges[PERF_COUNT_HW_EXTENTION_MAX] = {};

/* Returns string from pref_hw_id */
const char *get_pref_hw_id_msg(size_t id) {
  switch (id) {
#define make_case(cs)                                                          \
  case cs:                                                                     \
    return #cs;                                                                \
    break;
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

/* Minimum info from PERF_RECORD_SAMPLE */
struct record_info_t {
  ui32 pid;
  ui64 action_id;
  ui64 ip;
  ui64 addr;
  ui64 time;
  ui64 data_src;
};

struct mmap_info_t {
  ui32 pid;
  ui64 addr;
  ui64 len;
  ui64 pgoff;
  std::string filename;
};

/* Writing record's info to the file, filtered by `perf_hw_id` */
void write_event_in_file(const std::string &filename, size_t event_type,
                         const std::vector<record_info_t> &records,
                         const std::vector<mmap_info_t> &mmap_records)
{
  std::ofstream out_file;
  out_file.open(filename);

  for (const auto &it : mmap_records) {
    out_file << "m\n"
             << it.pid << " " << it.addr << " " << it.len << " " << it.pgoff
             << " " << it.filename << std::endl;
  }

  auto start_time = records[0].time;
  for (const auto &it : records) {
    if (event_ranges[event_type].in(it.action_id))
      out_file << "r\n"
               << it.pid << " " << it.addr << " "
               << (__u64)((it.time - start_time) / 1000000) << std::endl;
  }
  out_file.close();
}

void write_events(const std::string &filename,
                  const std::vector<record_info_t> &records,
                  const std::vector<mmap_info_t> &mmap_records)
{
  std::ofstream out_file;
  out_file.open(filename);
  for (const auto &it : mmap_records) {
    out_file << "mmap "
             << it.pid << " " << it.addr << " " << it.len << " " << it.pgoff
             << " " << it.filename << std::endl;
  }

  auto start_time = records[0].time;
  for (const auto &it : records) {
    if (event_ranges[PERF_COUNT_HW_CACHE_ITLB_READ_MISSES].in(it.action_id))
      out_file << "tlb "
              << it.pid << " " << it.addr << " "
              << (__u64)((it.time - start_time) / 1000000) << std::endl;
    if (event_ranges[PERF_COUNT_HW_CACHE_L1I_READ_MISSES].in(it.action_id))
      out_file << "cache "
              << it.pid << " " << it.addr << " "
              << (__u64)((it.time - start_time) / 1000000) << std::endl;
    if (event_ranges[PERF_COUNT_HW_CPU_CYCLES].in(it.action_id))
      out_file << "cycles "
              << it.pid << " " << it.addr << " "
              << (__u64)((it.time - start_time) / 1000000) << std::endl;
  }
}

size_t read_perf_file_attr(const perf_file_attr *pp_file_attr, const char *data,
                           size_t actual_struct_size) {
  assert(pp_file_attr);
  assert(data);
  /*      Quite good documentation you can find in man    */
  /* the article `perf_event_open(2) — Linux manual page` */

  const auto pp_event_attr = &pp_file_attr->attr;
  /*   Field `.sample_type` is a __u32 filed its bits  */
  /* represent which data we expect to see in records. */
  samped_data_t::set_config(pp_event_attr->sample_type);

  /* Here some difference in coding common hardware events and cache events */
  size_t event_idx = 0;
  if (pp_event_attr->type == PERF_TYPE_HW_CACHE) {
    /* See man for good undestanding */
    std::cout << "=====[PERF_TYPE_HW_CACHE]=====\n"
              << "Need addition information about .config field:\n"
              << "          perf_hw_cache_id = " << (pp_event_attr->config & 0xFF) << '\n'
              << "       perf_hw_cache_op_id = " << ((pp_event_attr->config >> 8) & 0xFF) << '\n'
              << "perf_hw_cache_op_result_id = " << ((pp_event_attr->config >> 16) & 0xFF) << '\n'
              << "==============================" << std::endl;
#define gen_CACHE_MISS_flag(ch)                                                \
  ((PERF_COUNT_HW_CACHE_RESULT_MISS << 16) |                                   \
   (PERF_COUNT_HW_CACHE_OP_READ << 8) | (ch))

    if (pp_event_attr->config == gen_CACHE_MISS_flag(PERF_COUNT_HW_CACHE_L1I))
      event_idx = PERF_COUNT_HW_CACHE_L1I_READ_MISSES;
    if (pp_event_attr->config == gen_CACHE_MISS_flag(PERF_COUNT_HW_CACHE_ITLB))
      event_idx = PERF_COUNT_HW_CACHE_ITLB_READ_MISSES;

#undef gen_CACHE_MISS_flag
  } else {
    /* If our event is PERF_TYPE_HARDWARE, so the .config field tells us */
    /* what harwdare event id describes this perf_event_attr structure.  */
    /* For more inf see `enum perf_hw_id` in `perf_event.h : line 45`    */
    std::cout << "=====[PERF_TYPE_HARDWARE]=====\n";
    std::cout << "==============================\n";
    event_idx = pp_event_attr->config;
  }
  /* Now we are interested in reading ids of events, that we will get from records. */
  /*   So it means, that there is a field .id in records (see samped_data_t), and   */
  /*      for associating this id to your event list you should read id list.       */

  /* According to my observation list of ids is a range of numbers, so I decided */
  /*        just save only the first and the last item from this list.           */
  auto ids_raw = Pointer(pp_file_attr).AsByte() + actual_struct_size - sizeof(perf_file_section);
  auto ids = Pointer(ids_raw).As<perf_file_section>();
  auto p_ids = Pointer(data + ids->offset).As<ui64>();

  auto start = *p_ids;
  p_ids += ids->size / sizeof(*p_ids) - 1;
  auto stop = *p_ids;
  event_ranges[event_idx].b = start;
  event_ranges[event_idx].e = stop;
  return event_ranges[event_idx].b;
}

int main(int argc, char **argv) {
  std::string buf;
  if (read_full_file("perf.data", buf) == EXIT_FAILURE)
    return EXIT_FAILURE;
  auto data = buf.data();

  /* Fistly read list of attributes, from this we need just ids */
  /*  of events and infomation about data in record structures. */
  perf_file_header *ph = (perf_file_header *)data;
  size_t saved_id = 0;
  bool only_one_event = ph->attrs.size / ph->attr_size == 1;
  for (size_t i = 0; i < ph->attrs.size / ph->attr_size; i++) {
    auto ptr = Pointer((data + ph->attrs.offset) + i * ph->attr_size).As<perf_file_attr>();
    saved_id = read_perf_file_attr(ptr, data, ph->attr_size);
  }

  std::vector<record_info_t> records;
  std::vector<mmap_info_t> mmap_records;
  auto data_ptr = data + ph->data.offset;
  for (size_t readed_bytes = 0; readed_bytes < ph->data.size;) {
    /*       Any record consist header and then info.        */
    /* From the header we need just a .size and .type fileds */
    auto pp_event_header = Pointer(data_ptr).As<perf_event_header>();

    /*       There is a problem here related to undescribed in `perf_event.h` record's type.      */
    /*   In the first time you can get invalid type of record (grather or equal PERF_RECORD_MAX)  */
    /*  Don`t panic, you can check the correctness of your parsing. Just use perf report -D, this */
    /*      command can show you all infomation about records that have beedwritten by perf.      */
    if (!(pp_event_header->size < 512 && pp_event_header->type < 100)) {
      std::cerr << "bad size = " << pp_event_header->size << std::endl;
      std::cerr << "bad type = " << pp_event_header->type << std::endl;
    }

    /* The iternal of record can be different, and it depends on
     * perf_event_attr.sample_type */
    if (pp_event_header->type == PERF_RECORD_SAMPLE) {
      /* Here I use simple reader of records. */
      samped_data_t sampled_data;
      sampled_data.read(data_ptr + sizeof(perf_event_header));
      records.push_back({sampled_data.pid,
                         only_one_event ? saved_id : sampled_data.id,
                         sampled_data.addr, sampled_data.ip, sampled_data.time,
                         sampled_data.data_src});
    }

    if (pp_event_header->type == PERF_RECORD_MMAP2) {
      auto mmap_data = Pointer(data_ptr + sizeof(perf_event_header)).As<mmap2_data_t>();
      std::string filename(mmap_data->filename, strlen(mmap_data->filename));
      mmap_records.push_back({mmap_data->pid, mmap_data->addr, mmap_data->len,
                              mmap_data->pgoff, filename});
    }

    if (pp_event_header->type == 79) {
      auto tc = Pointer(data_ptr).As<time_conv>();
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
  ui64 min_time = -1;
  ui64 max_time = 0;
  for (const auto &it : records) {
    min_time = std::min(min_time, it.time);
    max_time = std::max(max_time, it.time);
    for (int i = PERF_COUNT_HW_CPU_CYCLES; PERF_COUNT_HW_EXTENTION_MAX != i; i++)
      n_records[i] += event_ranges[i].in(it.action_id);
  }

  std::cout << "Total time duration: " << (max_time - min_time) / 1000000 << " ms" << std::endl;

  /* And print info. */
  std::cout << "Events distribution:" << std::endl;
  for (int i = PERF_COUNT_HW_CPU_CYCLES; PERF_COUNT_HW_EXTENTION_MAX != i; i++)
    if (n_records[i])
      std::cout << std::setw(40) << get_pref_hw_id_msg(i) << " = " << n_records[i] << std::endl;
  std::cout << "Total records = " << records.size() << std::endl;

  /* Also write info into files. */
  write_events("new_format.txt", records, mmap_records);

  if (n_records[PERF_COUNT_HW_CPU_CYCLES])
    write_event_in_file("data_inst.txt", PERF_COUNT_HW_CPU_CYCLES, records, mmap_records);
  if (n_records[PERF_COUNT_HW_CACHE_L1I_READ_MISSES])
    write_event_in_file("data_icache.txt", PERF_COUNT_HW_CACHE_L1I_READ_MISSES, records, mmap_records);
  if (n_records[PERF_COUNT_HW_CACHE_ITLB_READ_MISSES])
    write_event_in_file("data_itlb.txt", PERF_COUNT_HW_CACHE_ITLB_READ_MISSES, records, mmap_records);

  return 0;
}
