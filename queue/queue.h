#pragma once

// Including SDKDDKVer.h defines the highest available Windows platform.
// If you wish to build your application for a previous Windows platform, include WinSDKVer.h and
// set the _WIN32_WINNT macro to the platform you wish to support before including SDKDDKVer.h.
#include <SDKDDKVer.h>

#include <stdio.h>
#include <tchar.h>
#include <windows.h>
#include <stdint.h>
#include <time.h>
#include <locale.h>

#define CACHE_LINE_SIZE 64
// buffers sizes 
#define STR_BUFFER_LEN 1024*10
#define MAX_NUM_HANDLES 1024
#define QUEUE_SIZE 0x100
// TODO: calc mask in-code
#define QUEUE_POS_MASK 0xFF
// masks for histograms -
#define MASK_1 0x1
#define MASK_3 0x3
#define MASK_7 0x7
#define MASK_15 0xF
#define MASK_31 0x1F
#define MASK_63 0x3F
#define MASK_127 0x7F
#define MASK_1023 0x2FF

#define CONFLATED true
#define NOT_CONFLATED false

// error codes
#define ERR_OK 0
#define ERR_NO_FREE_SLOT -1
#define ERR_READER_CANT_WRITE_TO_QUEUE -2
#define ERR_QUEUE_FULL_WRITE_FAILED -3
#define ERR_WRITER_CANT_READ_FROM_QUEUE -4
#define ERR_QUEUE_EMPTY_READ_FAILED -5
#define ERR_QUEUE_INTERRUPTED -6
#define ERR_PIN_TO_CORE_FAILED -7

enum yield_e {
	YIELD_CPU_PAUSE,
	YIELD_YIELD_THREAD,
	YIELD_SLEEP,
	YIELD_STUB
};

typedef int queue_handle_t;
typedef int mmap_handle_t;

// TODO make sure that __declspec is the right thing to do:https://msdn.microsoft.com/en—us/library/83ythb65.aspx
// pad this sctructure so it does not cross cache line boundaries to avoid false sharing
volatile __declspec(align(CACHE_LINE_SIZE)) struct queue_element_t { 
	// key to keep track of conflated updates — override value with same key
	uint32_t key;
	// timestamp when message was placed into queue
	uint64_t time_placed;
	// make sure that payload fits any event union
	byte payload[1024];
};

volatile struct queue_perf_stats_t {
	uint64_t num_messages;
	uint64_t num_lock_retries;
	uint64_t num_write_wait_yields;
	uint64_t num_read_wait_yields;
	uint64_t num_conflated_writes;
	uint64_t time_in_queue_0_1; 
	uint64_t time_in_queue_2_3;
	uint64_t time_in_queue_4_7;
	uint64_t time_in_queue_8_15;
	uint64_t time_in_queue_16_31;
	uint64_t time_in_queue_32_127;
	uint64_t time_in_queue_128_1023;
	uint64_t time_in_queue_1024_;
	uint64_t queue_length_0_1;
	uint64_t queue_length_2_3; 
	uint64_t queue_length_4_7;
	uint64_t queue_length_8_127;
	uint64_t queue_length_128_1023;
	uint64_t queue_length_1024_;
};

volatile struct queue_buffer_t {
	bool writer_is_connected;
	bool interrupted;
	// TODO reader and writer positions are modified by reader and writer threads;
	// make sure they are on different cache lines
	__declspec(align(CACHE_LINE_SIZE)) uint64_t reader_pos;
	__declspec(align(CACHE_LINE_SIZE)) uint64_t writer_pos;
	__declspec(align(CACHE_LINE_SIZE)) queue_perf_stats_t perf_stats;
	__declspec(align(CACHE_LINE_SIZE)) queue_element_t events[QUEUE_SIZE];
}; 

volatile struct queue_t {
	char name[STR_BUFFER_LEN * 2];
	bool for_writing; 
	HANDLE file_handle;
	HANDLE mapping_handle;
	uint64_t length = QUEUE_SIZE;
	bool conflated;
	yield_e thread_yield;
	queue_buffer_t *buffer;
};

volatile struct mmap_t {
	char name[STR_BUFFER_LEN * 2];
	HANDLE file_handle;
	HANDLE mapping_handle;
	uint64_t length;
	LPVOID buffer;
};

void init_queues(char* home, unsigned int payloadvsize);
void cleanup_queues();
mmap_handle_t open_mmap(char* mmap_name, uint32_t size);
void close_mmap(mmap_handle_t h);
queue_handle_t open_queue_for_writing(char* queue_name, bool conflated, yield_e thread_yield);
queue_handle_t open_queue_for_reading(char* queue_name);
int16_t write_to_queue(queue_handle_t, uint32_t key, void *payload);
int16_t read_from_queue(queue_handle_t, void*);
int16_t read_from_queues(
	queue_handle_t hh[],
	uint16_t num_handles,
	void *data[],
	bool queue_got_data[]);
int16_t interrupt_queue(queue_handle_t);
int16_t is_queue_interrupted(queue_handle_t);
uint64_t queue_length(queue_handle_t);
void close_queue(queue_handle_t);
int16_t pin_thread_to_core(int16_t core_id);
void lock(volatile DWORD *lock);
void unlock(volatile DWORD *lock);
void print_queue_perf_stat(queue_handle_t);
void stat_time_in_queue(queue_handle_t h, char str[], uint16_t str_len);
void stat_queue_length(queue_handle_t h, char str[], uint16_t str_len);
void stat_queue_length_histo(
	queue_handle_t h,
	uint64_t buckets[],
	uint16_t num_buckets);
void stat_time_in_queue_histo(
	queue_handle_t h, 
	uint64_t buckets[],
	uint16_t num_buckets);
void stat_tick_to_trade_histo(uint64_t buckets[], uint16_t num_buckets);
void stat_contention(queue_handle_t h, char str[], uint16_t strylen);
uint64_t stat_get_and_reset_num_messages(queue_handle_t h);
LONGLONG calc_mikes(LARGE_INTEGER *starting_time, LARGE_INTEGER *ending_time);;
void panic(char*);
void mmap_file(wchar_t path[], unsigned int size, volatile LPVOID *buffer, HANDLE *mapping_handle, HANDLE *file_handle);
BOOL directory_exists(LPCTSTR);
void delete_files_in_folder(char*);
BOOL file_exists(LPCTSTR);
void mb_to_wide_str(char* mb_str, wchar_t* wide_str);
queue_handle_t _open_queue(char* queue_name, bool for_writing, bool conflated, yield_e thread_yield);
int16_t _write_to_queue(queue_handle_t, uint32_t key, bool wait, void *payload);
int16_t _read_from_queue(queue_handle_t, void*, bool wait);
void _lock_queues();
void _unlock_queues();
int16_t _lock(volatile queue_t*);
void _unlock(volatile queue_t*);
void update_queue_length_counter(
	uint64_t queue_length,
	volatile queue_perf_stats_t*);
void update_time_in_queue_counter(
	uint64_t elapsed,
	volatile queue_perf_stats_t*);
void yield(volatile queue_t *q);

/*
 * externs
 */
extern volatile char _home[];
extern volatile queue_t *volatile _queues[];
extern volatile mmap_t *volatile _mmaps[];
extern volatile DWORD _lock_open_close;
extern volatile DWORD _queues_initialized;
/* performance: CPU cycles in one sec */
extern LARGE_INTEGER _frequency;
extern volatile unsigned int _payload_size;
