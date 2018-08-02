#include "queue.h"

#pragma warning(disable:4996)
uint64_t stat_get_and_reset_num_messages(queue_handle_t h)
{
	volatile queue_t *q = _queues[h];
	uint64_t t = q->buffer->perf_stats.num_messages;
	q->buffer->perf_stats.num_messages = 0;
	return t;
}

void stat_time_in_queue_histo(
	queue_handle_t h,
	uint64_t buckets[],
	uint16_t num_buckets)
{
	volatile queue_t *q = _queues[h];
	if (num_buckets != 8)
		panic((char*)"wrong number of buckets");
	uint64_t total = q->buffer->perf_stats.time_in_queue_0_1 +
		q->buffer->perf_stats.time_in_queue_2_3 +
		q->buffer->perf_stats.time_in_queue_4_7 +
		q->buffer->perf_stats.time_in_queue_8_15 +
		q->buffer->perf_stats.time_in_queue_16_31 +
		q->buffer->perf_stats.time_in_queue_32_127 +
		q->buffer->perf_stats.time_in_queue_128_1023 +
		q->buffer->perf_stats.time_in_queue_1024_;
	if (0 == total)
		return;
	buckets[0] = q->buffer->perf_stats.time_in_queue_0_1 * 100 / total;
	buckets[1] = q->buffer->perf_stats.time_in_queue_2_3 * 100 / total;
	buckets[2] = q->buffer->perf_stats.time_in_queue_4_7 * 100 / total;
	buckets[3] = q->buffer->perf_stats.time_in_queue_8_15 * 100 / total;
	buckets[4] = q->buffer->perf_stats.time_in_queue_16_31 * 100 / total;
	buckets[5] = q->buffer->perf_stats.time_in_queue_32_127 * 100 / total;
	buckets[6] = q->buffer->perf_stats.time_in_queue_128_1023 * 100 / total;
	buckets[7] = q->buffer->perf_stats.time_in_queue_1024_ * 100 / total;
}

void stat_time_in_queue(queue_handle_t h, char str[], uint16_t str_len)
{
	volatile queue_t *q = _queues[h];
	uint64_t total = q->buffer->perf_stats.time_in_queue_0_1 +
		q->buffer->perf_stats.time_in_queue_2_3 +
		q->buffer->perf_stats.time_in_queue_4_7 +
		q->buffer->perf_stats.time_in_queue_8_15 +
		q->buffer->perf_stats.time_in_queue_16_31 +
		q->buffer->perf_stats.time_in_queue_32_127 +
		q->buffer->perf_stats.time_in_queue_128_1023 +
		q->buffer->perf_stats.time_in_queue_1024_;
	if (0 == total)
		return;
	sprintf(str, "%llu, %llu, %llu, %llu, %llu, %llu, %llu, %llu",
		q->buffer->perf_stats.time_in_queue_0_1 * 100 / total,
		q->buffer->perf_stats.time_in_queue_2_3 * 100 / total,
		q->buffer->perf_stats.time_in_queue_4_7 * 100 / total,
		q->buffer->perf_stats.time_in_queue_8_15 * 100 / total,
		q->buffer->perf_stats.time_in_queue_16_31 * 100 / total,
		q->buffer->perf_stats.time_in_queue_32_127 * 100 / total,
		q->buffer->perf_stats.time_in_queue_128_1023 * 100 / total,
		q->buffer->perf_stats.time_in_queue_1024_ * 100 / total
	);
}

void stat_queue_length_histo(
	queue_handle_t h,
	uint64_t buckets[],
	uint16_t num_buckets)
{
	if (num_buckets != 6)
		panic((char*)"wrong number of buckets");
	volatile queue_t *q = _queues[h];
	uint64_t total = q->buffer->perf_stats.queue_length_0_1 +
		q->buffer->perf_stats.queue_length_2_3 +
		q->buffer->perf_stats.queue_length_4_7 +
		q->buffer->perf_stats.queue_length_8_127 +
		q->buffer->perf_stats.queue_length_128_1023 +
		q->buffer->perf_stats.queue_length_1024_;
	buckets[0] = q->buffer->perf_stats.queue_length_0_1 * 100 / total;
	buckets[1] = q->buffer->perf_stats.queue_length_2_3 * 100 / total;
	buckets[2] = q->buffer->perf_stats.queue_length_4_7 * 100 / total;
	buckets[3] = q->buffer->perf_stats.queue_length_8_127 * 100 / total;
	buckets[4] = q->buffer->perf_stats.queue_length_128_1023 * 100 / total;
	buckets[5] = q->buffer->perf_stats.queue_length_1024_ * 100 / total;
}

void stat_queue_length(queue_handle_t h, char str[], uint16_t str_len)
{
	volatile queue_t *q = _queues[h];
	uint64_t total = q->buffer->perf_stats.queue_length_0_1 +
		q->buffer->perf_stats.queue_length_2_3 +
		q->buffer->perf_stats.queue_length_4_7 +
		q->buffer->perf_stats.queue_length_8_127 +
		q->buffer->perf_stats.queue_length_128_1023 +
		q->buffer->perf_stats.queue_length_1024_;
	sprintf(str, "%llu %llu %llu %llu %llu %llu",
		q->buffer->perf_stats.queue_length_0_1 * 100 / total,
		q->buffer->perf_stats.queue_length_2_3 * 100 / total,
		q->buffer->perf_stats.queue_length_4_7 * 100 / total,
		q->buffer->perf_stats.queue_length_8_127 * 100 / total,
		q->buffer->perf_stats.queue_length_128_1023 * 100 / total,
		q->buffer->perf_stats.queue_length_1024_ * 100 / total
	);
}

void stat_contention(queue_handle_t h, char str[], uint16_t str_len)
{
	volatile queue_t *q = _queues[h];
	sprintf(str, "cw: %llu, lr: %llu, ry: %llu, wy: %llu",
		q->buffer->perf_stats.num_conflated_writes,
		q->buffer->perf_stats.num_lock_retries,
		q->buffer->perf_stats.num_read_wait_yields,
		q->buffer->perf_stats.num_write_wait_yields
	) ;
	q->buffer->perf_stats.num_conflated_writes = 0;
	q->buffer->perf_stats.num_lock_retries = 0;
	q->buffer->perf_stats.num_read_wait_yields = 0;
	q->buffer->perf_stats.num_write_wait_yields = 0;
}

LONGLONG calc_mikes(LARGE_INTEGER *starting_tme, LARGE_INTEGER *ending_time)
{
	LARGE_INTEGER elapsed_microseconds;
	elapsed_microseconds.QuadPart =
		(*ending_time).QuadPart - (*starting_tme).QuadPart;
	/*
	* we now have the elapsed number of ticks, along with the
	* number of ticks—per—second. We use these values
	* to convert to the number of elapsed microseconds;
	* to guard against loss—of-precision, we convert 7
	* to microseconds *before* dividing by ticks—per—second.
	*/
	elapsed_microseconds.QuadPart *= 1000000;
	elapsed_microseconds.QuadPart /= _frequency.QuadPart;
	return elapsed_microseconds.QuadPart;
}

void update_queue_length_counter(
	uint64_t queue_length,
	volatile queue_perf_stats_t *qp)
{
	if (!(queue_length & ~MASK_1))
		qp->queue_length_0_1++;
	else if (!(queue_length & ~MASK_3))
		qp->queue_length_2_3++;
	else if (!(queue_length & ~MASK_7))
		qp->queue_length_4_7++;
	else if (!(queue_length & ~MASK_127))
		qp->queue_length_8_127++;
	else if (!(queue_length & ~MASK_1023))
		qp->queue_length_128_1023++;
	else
		qp->queue_length_1024_++;
}

void update_time_in_queue_counter(
	uint64_t elapsed,
	volatile queue_perf_stats_t *qp)
{
	/* TODO get rid of division and multiplication */
	elapsed *= 1000000;
	elapsed /= _frequency.QuadPart;
	if (!(elapsed & ~MASK_1))
		qp->time_in_queue_0_1++;
	else if (!(elapsed & ~MASK_3))
		qp->time_in_queue_2_3++;
	else if (!(elapsed & ~MASK_7))
		qp->time_in_queue_4_7++;
	else if (!(elapsed & ~MASK_15))
		qp->time_in_queue_8_15++;
	else if (!(elapsed & ~MASK_31))
		qp->time_in_queue_16_31++;
	else if (!(elapsed & ~MASK_127))
		qp->time_in_queue_32_127++;
	else if (!(elapsed & ~MASK_1023))
		qp->time_in_queue_128_1023++;
	else
		qp->time_in_queue_1024_++;
}

/*
* tick to trade histogram buckets
*/
// TODO move to struct
volatile uint64_t tick_to_trade_0_1;
volatile uint64_t tick_to_trade_2_3;
volatile uint64_t tick_to_trade_4_7;
volatile uint64_t tick_to_trade_8_15;
volatile uint64_t tick_to_trade_16_31;
volatile uint64_t tick_to_trade_32_63;
volatile uint64_t tick_to_trade_64_127;
volatile uint64_t tick_to_trade_128_1023;
volatile uint64_t tick_to_trade_1024_;

void update_tick_to_trade_counter(uint64_t elapsed)
{
	// TODO get rid of division and multiplicationl (?)
	// use CPU cycles in histogram;
	// later convert them to micros
	elapsed *= 1000000; 
	elapsed /= _frequency.QuadPart;
	if (!(elapsed & ~MASK_1))
		tick_to_trade_0_1++;
	else if (!(elapsed & ~MASK_3))
		tick_to_trade_2_3++;
	else if (!(elapsed & ~MASK_7))
		tick_to_trade_4_7++;
	else if (!(elapsed & ~MASK_15))
		tick_to_trade_8_15++;
	else if (!(elapsed & ~MASK_31))
		tick_to_trade_16_31++;
	else if (!(elapsed & ~MASK_63))
		tick_to_trade_32_63++;
	else if (!(elapsed & ~MASK_127))
		tick_to_trade_64_127++;
	else if (!(elapsed & ~MASK_1023))
		tick_to_trade_128_1023++;
	else
		tick_to_trade_1024_++;
}

void stat_tick_to_trade(char str[], uint16_t str_len)
{
	uint64_t total = tick_to_trade_0_1 + 
		tick_to_trade_2_3 +
		tick_to_trade_4_7 +
		tick_to_trade_8_15 +
		tick_to_trade_16_31 +
		tick_to_trade_32_63 +
		tick_to_trade_64_127 +
		tick_to_trade_128_1023 +
		tick_to_trade_1024_;
	if (0 == total)
		return;
	sprintf(str, "%llu, %llu, %llu, %llu, %llu, %llu, %llu, %llu, %llu",
		tick_to_trade_0_1 * 100 / total,
		tick_to_trade_2_3 * 100 / total,
		tick_to_trade_4_7 * 100 / total,
		tick_to_trade_8_15 * 100 / total,
		tick_to_trade_16_31 * 100 / total,
		tick_to_trade_32_63 * 100 / total,
		tick_to_trade_64_127 * 100 / total,
		tick_to_trade_128_1023 * 100 / total, 
		tick_to_trade_1024_ * 100 / total
	);
}

void stat_tick_to_trade_histo(uint64_t buckets[], uint16_t num_buckets)
{
	if (num_buckets != 9)
		panic((char*)"wrong number of buckets");
	uint64_t total = tick_to_trade_0_1 +
		tick_to_trade_2_3 +
		tick_to_trade_4_7 +
		tick_to_trade_8_15 +
		tick_to_trade_16_31 +
		tick_to_trade_32_63 +
		tick_to_trade_64_127 +
		tick_to_trade_128_1023 +
		tick_to_trade_1024_;
	if (0 == total)
		return;
	buckets[0] = tick_to_trade_0_1 * 100 / total;
	buckets[1] = tick_to_trade_2_3 * 100 / total;
	buckets[2] = tick_to_trade_4_7 * 100 / total;
	buckets[3] = tick_to_trade_8_15 * 100 / total;
	buckets[4] = tick_to_trade_16_31 * 100 / total;
	buckets[5] = tick_to_trade_32_63 * 100 / total;
	buckets[6] = tick_to_trade_64_127 * 100 / total;
	buckets[7] = tick_to_trade_128_1023 * 100 / total;
	buckets[8] = tick_to_trade_1024_ * 100 / total;
}
