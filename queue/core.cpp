#include "queue.h"

// folder where mmapped files are created
volatile char _home[STR_BUFFER_LEN];
// queues handles
volatile queue_t *volatile _queues[MAX_NUM_HANDLES];
volatile mmap_t *volatile _mmaps[MAX_NUM_HANDLES];
// lock to protect open/close queue ops
volatile DWORD _lock_open_close;
volatile DWORD _queues_initialized = 0;
LARGE_INTEGER _frequency;
volatile unsigned int _payload_size;

void init_queues(char* home, unsigned int payload_size)
{
	if (1 == InterlockedCompareExchange(&_queues_initialized, 1, 0)) 
		return;

	_payload_size = payload_size;
	_queues_initialized = true;
	/*
	 * perf counters are reported in cycles;
	 * retrieve frequency — num of CPU cycles per second,
	 * so we can convert cycles to (micro)seconds
	 */
	QueryPerformanceFrequency(&_frequency);
	// TODO sanitize home dir name
	if (STR_BUFFER_LEN < strlen(home) + 1)
		panic((char*)"Queue HOME folder name is too long");
	if (0 != strcpy_s((char*)_home, STR_BUFFER_LEN, home))
		panic((char*)"Error storing HOME folder name");
	if ('\\' != _home[strlen((char*)_home) - 1])
		strcat_s((char*)_home, STR_BUFFER_LEN, "\\");
	/* clean up or create home folder */
	wchar_t home_wide_str[STR_BUFFER_LEN];
	mb_to_wide_str((char*)_home, home_wide_str);
	if (directory_exists(home_wide_str))
		delete_files_in_folder((char*)_home);
	else
		CreateDirectory(home_wide_str, NULL);
}

void cleanup_queues()
{
	_queues_initialized = false;
}

queue_handle_t open_queue_for_reading(char* queue_hame)
{
	return _open_queue(queue_hame, false, NOT_CONFLATED, YIELD_STUB);
}

queue_handle_t open_queue_for_writing(char* queue_name, bool conflated, yield_e thread_yield)
{
	return _open_queue(queue_name, true, conflated, thread_yield);
}

void close_queue(queue_handle_t h)
{
	_lock_queues();
	volatile queue_t* q = _queues[h];
	UnmapViewOfFile(q->buffer);
	CloseHandle(q->mapping_handle);
	CloseHandle(q->file_handle);
	free((void*)q);
	_queues[h] = NULL;
	_unlock_queues();
}

void close_mmap(mmap_handle_t h)
{
	_lock_queues();
	volatile mmap_t* m = _mmaps[h];
	UnmapViewOfFile(m->buffer);
	CloseHandle(m->mapping_handle);
	CloseHandle(m->file_handle);
	free((void*)m);
	_mmaps[h] = NULL;
	_unlock_queues();
}

int16_t write_to_queue(queue_handle_t h, uint32_t key, void *payload)
{
	return _write_to_queue(h, key, true, payload);
}

int16_t read_from_queue(queue_handle_t h, void *payload) {
	return _read_from_queue(h, payload, true);
}

/*
 * scan all queues; return whatever data can be red, otherwise keep spinning;
 */
int16_t read_from_queues(
	queue_handle_t hh[],
	uint16_t num_handles,
	void *payloads[],
	bool queue_got_data[])
{
	while (1) {
		bool got_data = false;
		for (unsigned int h = 0; h < num_handles; h++) {
			/* NOTE: non—blocking read (wait=false) */
			int16_t r = _read_from_queue(hh[h], payloads[h], false);
			if (0 == r) {
				got_data = true;
				queue_got_data[h] = true;
			} else if (ERR_QUEUE_INTERRUPTED == r) {
				/* at least one of the queues is interrupted; stop reading */
				return ERR_QUEUE_INTERRUPTED;
			} else {
				queue_got_data[h] = false;
			}
		}
		if (got_data)
			return 0;
		/* here we read from queues in non-blocking mode; yield */
		YieldProcessor();
	}
	return 0;
}

int16_t interrupt_queue(queue_handle_t h)
{
	int16_t result = 0;
	volatile queue_t *q = _queues[h];
	volatile queue_buffer_t *qb = q->buffer;
	qb->interrupted = true;
	MemoryBarrier();
	return result;
}

static queue_handle_t _open_queue(
	char* queue_name,
	bool for_writing,
	bool conflated, 
	yield_e thread_yield)
{
	/* find next avail slot */
	queue_handle_t queue_h;
	bool found_empty_slot = false;
	/* following block can only be called from one thread;
 	 * this is not highly contended code
	 */
	_lock_queues();
	for (int i = 0; i < MAX_NUM_HANDLES; i++) {
		if (NULL == _queues[i]) {
		queue_h = i;
		found_empty_slot = true;
		break;
		}
	}
	if (!found_empty_slot) {
		_unlock_queues();
		return ERR_NO_FREE_SLOT;
	}
	queue_t* queue = (queue_t*)malloc(sizeof(queue_t));
	_queues[queue_h] = queue;
	_unlock_queues();

	// TODO sanitize queue name
	char queue_file_path[STR_BUFFER_LEN];
	if (0 != strcpy_s(queue_file_path, STR_BUFFER_LEN, (char*)_home))
		panic((char*)"Queue HOME folder name is too long");
	strcat_s(queue_file_path, STR_BUFFER_LEN, queue_name);
	// TODO check errors and string length
	wchar_t queue_file_path_wide_str[STR_BUFFER_LEN];
	mb_to_wide_str(queue_file_path, queue_file_path_wide_str);
	volatile LPVOID buffer;
	HANDLE file_handle, mapping_handle;
	mmap_file(queue_file_path_wide_str, sizeof(queue_buffer_t), &buffer, &mapping_handle, &file_handle);
	queue->file_handle = file_handle;
	queue->mapping_handle = mapping_handle;
	queue->for_writing = for_writing;
	strcpy_s(queue->name, STR_BUFFER_LEN, queue_name);
	queue->buffer = (queue_buffer_t*)buffer;
	if (for_writing) {
		queue->thread_yield = thread_yield;
		queue->conflated = conflated;
		/* writer initializes positions; reader doesn't touch them
		 * because writer may already be writing into the queue
		 */
		queue->buffer->reader_pos = 0;
		queue->buffer->writer_pos = 0;
		queue->buffer->perf_stats.num_lock_retries = 0;
		queue->buffer->perf_stats.num_read_wait_yields = 0;
		queue->buffer->perf_stats.num_write_wait_yields = 0;
		queue->buffer->perf_stats.num_conflated_writes = 0;
		queue->buffer->perf_stats.time_in_queue_0_1 = 0;
		queue->buffer->perf_stats.time_in_queue_2_3 = 0;
		queue->buffer->perf_stats.time_in_queue_4_7 = 0;
		queue->buffer->perf_stats.time_in_queue_8_15 = 0;
		queue->buffer->perf_stats.time_in_queue_16_31 = 0;
		queue->buffer->perf_stats.time_in_queue_32_127 = 0;
		queue->buffer->perf_stats.time_in_queue_128_1023 = 0;
		queue->buffer->perf_stats.time_in_queue_1024_ = 0;
		queue->buffer->perf_stats.queue_length_0_1 = 0;
		queue->buffer->perf_stats.queue_length_2_3 = 0;
		queue->buffer->perf_stats.queue_length_4_7 = 0;
		queue->buffer->perf_stats.queue_length_8_127 = 0;
		queue->buffer->perf_stats.queue_length_128_1023 = 0;
		queue->buffer->perf_stats.queue_length_1024_ = 0;
		queue->buffer->writer_is_connected = true;
	}
	return queue_h;
}

mmap_handle_t open_mmap(
	char* mmap_name,
	uint32_t size)
{
	/* find next avail slot */
	mmap_handle_t mmap_h;
	bool found_empty_slot = false;
	/* following block can only be called from one thread;
	* this is not highly contended code
	*/
	_lock_queues();
	for (int i = 0; i < MAX_NUM_HANDLES; i++) {
		if (NULL == _mmaps[i]) {
			mmap_h = i;
			found_empty_slot = true;
			break;
		}
	}
	if (!found_empty_slot) {
		_unlock_queues();
		return ERR_NO_FREE_SLOT;
	}
	mmap_t* mmap = (mmap_t*)malloc(sizeof(mmap_t));
	_mmaps[mmap_h] = mmap;
	_unlock_queues();

	// TODO sanitize queue name
	char mmap_file_path[STR_BUFFER_LEN];
	if (0 != strcpy_s(mmap_file_path, STR_BUFFER_LEN, (char*)_home))
		panic((char*)"Queue HOME folder name is too long");
	strcat_s(mmap_file_path, STR_BUFFER_LEN, mmap_name);
	// TODO check errors and string length
	wchar_t mmap_file_path_wide_str[STR_BUFFER_LEN];
	mb_to_wide_str(mmap_file_path, mmap_file_path_wide_str);
	volatile LPVOID buffer;
	HANDLE file_handle, mapping_handle;
	mmap_file(mmap_file_path_wide_str, size, &buffer, &mapping_handle, &file_handle);
	mmap->file_handle = file_handle;
	mmap->mapping_handle = mapping_handle;
	strcpy_s(mmap->name, STR_BUFFER_LEN, mmap_name);
	mmap->buffer = (queue_buffer_t*)buffer;
	return mmap_h;
}

/*
 * writer is always standing on a slot to which it will write;
 * after writing to slot, writer will advance to
 * next slot; writer cannot pass reader, but they both
 * can stay on the same slot — when Writer writes to the slot and
 * moves forward, reader will read from the slot;
 */
int16_t _write_to_queue(
	queue_handle_t h,
	uint32_t key,
	bool wait,
	void *payload)
{
	LARGE_INTEGER starting_time;
	QueryPerformanceCounter(&starting_time);
	int result = ERR_OK;
	volatile queue_t *q = _queues[h];
	volatile queue_buffer_t *qb = q->buffer;
	if (!q->for_writing) {
		result = ERR_READER_CANT_WRITE_TO_QUEUE;
		goto done;
	}
	if (qb->interrupted) { 
		result = ERR_QUEUE_INTERRUPTED;
		goto cleanup;
	}
	q->length = qb->writer_pos - qb->reader_pos;
	update_queue_length_counter(q->length, &(qb->perf_stats));
	if (wait) {
		while (qb->writer_pos == qb->reader_pos + QUEUE_SIZE) {
			/*
			writing to non—conflated queue; queue is full; unlock then try again

			| | |R| |
			| | |W| |
			|*|*|*|*| <—- Writer wrapped around the queue and is waiting:
			|_|_[_l_| reader is slow, queue is full
			*/
			yield(q);
			if (qb->interrupted) {
				result = ERR_QUEUE_INTERRUPTED;
				goto cleanup;
			}
			qb->perf_stats.num_write_wait_yields++;
		}
	} else {
		result = ERR_QUEUE_FULL_WRITE_FAILED;
		goto cleanup;
	}

	if (!q->conflated) {
		/* 
		 * in non—conflated queue we always write to new slot;
		 * see if queue has free slots left
		 */
		uint64_t writer_pos_wrapped = qb->writer_pos & QUEUE_POS_MASK;
		qb->events[writer_pos_wrapped].time_placed = starting_time.QuadPart;
		memcpy(
			(void*)(qb->events[writer_pos_wrapped].payload),
			payload,
			_payload_size);
		// make sure that payload becomes visible before 
		// writer advances to new position
		MemoryBarrier();
		qb->writer_pos++;
	} else {
		/*
		writing to conflated queue; see if this key
		has already been written (but not seen by the reader);
		reader cannot move and needs to be locked;
		otherwise we will be writing into same slots that reader
		is reading

		|R| |W| |
		|*|*| | |
		|_|_|_|_| <-- Writer can write and move forward

		|W| |R| |
		| | |*|*|
		|_|_|_|_| <-- Writer can write and move forward:
					  writer wrapped around queue and is behind reader
		*/
		bool key_found = false;
		for (uint64_t i = qb->reader_pos; i < qb->writer_pos; i++) {
			uint64_t i_wrapped = i & QUEUE_POS_MASK;
			if (qb->events[i_wrapped].key == key) {
				qb->events[i_wrapped].time_placed = starting_time.QuadPart;
				memcpy(
					(void *)(qb->events[i_wrapped].payload),
					payload, 
					_payload_size);
				key_found = true;
				qb->perf_stats.num_conflated_writes++;
				break;
			}
		}
		if (!key_found) {
			/* this key has not been written yet; write data to next slot */
			if (wait) { 
				while (qb->writer_pos + QUEUE_SIZE == qb->reader_pos) {
					/*
					| | |R| |
					| | |W| | 
					|*|*|*|*|
					|_|_|_|_| <-- Writer wrapped around the queue
								and is waiting: queue is full
					*/
					yield(q);
					if (qb->interrupted) {
						result = ERR_QUEUE_INTERRUPTED;
						goto cleanup;
					}
					qb->perf_stats.num_write_wait_yields++;
				}
			} else {
				result = ERR_QUEUE_FULL_WRITE_FAILED;
				goto cleanup;
			}
			uint64_t writer_pos_wrapped = qb->writer_pos & QUEUE_POS_MASK;
			qb->events[writer_pos_wrapped].time_placed = starting_time.QuadPart;
			qb->events[writer_pos_wrapped].key = key;
			memcpy(
				(void*)(qb->events[writer_pos_wrapped].payload),
				payload,
				_payload_size);
				MemoryBarrier();
				qb->writer_pos++;
		}
	}
	cleanup:
	done:
		return result;
}

/*
 * reader can read slot only if writer moved forward from it;
 * when reader is standing on a slot — he is reading it;
 * note that logic of reading from conflated and non—conflated queue is the same
 */
int16_t _read_from_queue(queue_handle_t h, void *payload, bool wait)
{ 
	int16_t result = 0;
	volatile queue_t *q = _queues[h];
	volatile queue_buffer_t *qb = q->buffer;
	if (q->for_writing) {
		return ERR_WRITER_CANT_READ_FROM_QUEUE;
	}
	/* wait for writer to connect */
	while (!qb->writer_is_connected) {
		yield(q);
	}
	/* update perf stats - queue length */ 
	q->length = qb->writer_pos - qb->reader_pos;
	update_queue_length_counter(q->length, &(qb->perf_stats));
	/*
	 * reader will try to read from the slot it is currently on;
	 * reader will wait if writer is on the same slot;
	 * after reading the slot data, reader will advance to the next slot;
	 */
	if (wait) {
		while (qb->reader_pos == qb->writer_pos) {
			/*
			| |R| | |
			| |W| | |
			|_|_|_|_| <-— Reader is waiting:
			writer is too slow, queue is empty
			*/
			yield(q);
			if (qb->interrupted) {
				return ERR_QUEUE_INTERRUPTED;
			}
			qb->perf_stats.num_read_wait_yields++;
		}
	} else {
		if (qb->reader_pos == qb->writer_pos) {
			/* we are not waiting and queue is empty (see diagram above) */
			if (qb->interrupted) {
				return ERR_QUEUE_INTERRUPTED;
			}
			return ERR_QUEUE_EMPTY_READ_FAILED;
		}
	}
	/*
	|R|W| | |
	|*| | | | 
	|_]_!_|_| <-- Reader can read and move forward
	
	|W| | |R|
	| | | |*|
	|_|_|_|_| <-- Reader read and move forward:
				writer wrapped around queue and is behind reader
	*/
	uint64_t reader_pos_wrapped = qb->reader_pos & QUEUE_POS_MASK;
	uint64_t time_placed = qb->events[reader_pos_wrapped].time_placed;
	memcpy(
		payload,
		(void*)(qb->events[reader_pos_wrapped]).payload,
		_payload_size);
	qb->reader_pos++;
	// update perf stats — time in queue
	LARGE_INTEGER ending_time;
	QueryPerformanceCounter(&ending_time);
	uint64_t elapsed = ending_time.QuadPart - time_placed;
	update_time_in_queue_counter(elapsed, &(qb->perf_stats));
	q->buffer->perf_stats.num_messages++;
	return result;
}

void yield(volatile queue_t *q) {
	switch (q->thread_yield) {
	case YIELD_CPU_PAUSE:
		/* this one is really fast: CPU will run at 90% */
		YieldProcessor();
		break;
	case YIELD_YIELD_THREAD:
		/* this one is a bit slower: CPU will run at 40% */
		SwitchToThread();
		break;
	case YIELD_SLEEP:
		/* this one is slowest: CPU will run at 20% */
		Sleep(1);
		break;
	}
}
