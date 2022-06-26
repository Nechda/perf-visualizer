#include <linux/perf_event.h>

// This structures from perf sources
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

typedef __u64 ui64;
typedef __u32 ui32;
