#include "../queue/queue.h"
#include "algo.h"

#pragma warning(disable:4996)

volatile bool stop_stats_thread = false;

int main() 
{
	char data_dir[STR_BUFFER_LEN];
	if (!GetCurrentDirectoryA(STR_BUFFER_LEN, data_dir))
		panic((char*)"failed to get current directory");
	strcat_s(data_dir, STR_BUFFER_LEN, "/_data");
	start_algo(data_dir);
	DWORD pid;
	HANDLE ph;
	ph = ::CreateThread(NULL, 0, collect_stats_thread, 0, 0, &pid);
	ph = ::CreateThread(NULL, 0, httpd_thread, 0, 0, &pid);
	printf("press Enter to exit...");
	getchar();
	stop_stats_thread = true;
	stop_algo();
	return 0;
}

void start_algo(char *home_folder)
{
	setlocale(LC_NUMERIC, "");
	init_queues(home_folder, sizeof(event_t));

	/* open mmap regions */
	_parent_orders_mmap_h = open_mmap((char*)PARENT_ORDERS_MMAP, sizeof(parent_orders_t));
	_parent_orders = (volatile parent_orders_t*)_mmaps[_parent_orders_mmap_h]->buffer;
	_parent_orders->num_parent_orders = 0;

	_child_orders_mmap_h = open_mmap((char*)CHILD_ORDERS_MMAP, sizeof(child_orders_t));
	_child_orders = (volatile child_orders_t*)_mmaps[_child_orders_mmap_h]->buffer;
	_child_orders->num_child_orders = 0;

	_symbols_mmap_h = open_mmap((char*)SYMBOLS_MMAP, sizeof(symbols_t));
	_symbols = (volatile symbols_t*)_mmaps[_symbols_mmap_h]->buffer;
	_symbols->num_symbols = 0;

	_sec_subscriptions_mmap_h = open_mmap((char*)SUBSCRIPTIONS_MMAP, sizeof(sec_subscriptions_t));
	_sec_subscriptions = (volatile sec_subscriptions_t*)_mmaps[_sec_subscriptions_mmap_h]->buffer;

	/* DEMO: fill symbols from sec master */
	_symbols->num_symbols = 100;
	char sym[12];
	for (security_handle_t i = 0; i < _symbols->num_symbols; i++) {
		sprintf(sym, "SYM%03d\0", i);
		strcpy((char *)_symbols->symbols[i].symbol, sym);
	}
	/* no subscriptions yet: roots point to —1; zero subscription matrix */
	for (uint32_t i = 0; i < MAX_TOTAL_SYMBOLS; i++) {
		_sec_subscriptions->_security_subscriptions_root[i] = -1;
		_sec_subscriptions->_security_subscriptions_write_lock[i] = 0;
		for (uint32_t j = 0; j < MAX_TOTAL_PARENT_ORDERS; j++)
			_sec_subscriptions->_security_subscriptions[i][j] = 0;
	}

	/* open queues for reading */
	_q_upstream_to_algo_r = open_queue_for_reading(
		(char*)UPSTREAM_TO_ALGO_QUEUE);
	_q_downstream_to_algo_r = open_queue_for_reading(
		(char*)DOWNSTREAM_TO_ALGO_QUEUE);
	_q_market_data_to_algo_r = open_queue_for_reading(
		(char*)MARKET_DATA_TO_ALGO_QUEUE);
	_q_algo_to_market_data_r = open_queue_for_reading(
		(char*)ALGO_TO_MARKET_DATA_QUEUE);
	_q_algo_to_downstream_r = open_queue_for_reading(
		(char*)ALGO_TO_DOWNSTREAM_QUEUE);
	_q_algo_to_upstream_r = open_queue_for_reading(
		(char*)ALGO_TO_UPSTREAM_QUEUE);
	/* open queues for writing */
	_q_upstream_to_algo_w = open_queue_for_writing((char*)UPSTREAM_TO_ALGO_QUEUE,
		NOT_CONFLATED, YIELD_SLEEP);
	_q_downstream_to_algo_w = open_queue_for_writing((char*)DOWNSTREAM_TO_ALGO_QUEUE,
		NOT_CONFLATED, YIELD_CPU_PAUSE);
	_q_market_data_to_algo_w = open_queue_for_writing((char*)MARKET_DATA_TO_ALGO_QUEUE,
		CONFLATED, YIELD_CPU_PAUSE);
	_q_algo_to_market_data_w = open_queue_for_writing((char*)ALGO_TO_MARKET_DATA_QUEUE,
		NOT_CONFLATED, YIELD_SLEEP);
	_q_algo_to_downstream_w = open_queue_for_writing((char*)ALGO_TO_DOWNSTREAM_QUEUE,
		NOT_CONFLATED, YIELD_CPU_PAUSE);
	_q_algo_to_upstream_w = open_queue_for_writing((char*)ALGO_TO_UPSTREAM_QUEUE,
		NOT_CONFLATED, YIELD_SLEEP);
	// start threads
	DWORD pid;
	HANDLE ph;
	ph = ::CreateThread(NULL, 0, upstream_incoming_thread, 0, 0, &pid);
	ph = ::CreateThread(NULL, 0, upstream_outgoing_thread, 0, 0, &pid);
	ph = ::CreateThread(NULL, 0, market_data_incoming_thread, 0, 0, &pid);
	ph = ::CreateThread(NULL, 0, market_data_sub_unsub_thread, 0, 0, &pid);
	ph = ::CreateThread(NULL, 0, algo_thread, 0, 0, &pid);
	ph = ::CreateThread(NULL, 0, downstream_incoming_thread, 0, 0, &pid);
	ph = ::CreateThread(NULL, 0, downstream_outgoing_thread, 0, 0, &pid);
}

bool is_any_algo_thread_running()
{
	return upstream_incoming_thread_is_running ||
		upstream_outgoing_thread_is_running ||
		market_data_incoming_thread_is_running ||
		market_data_sub_unsub_thread_is_running ||
		algo_thread_is_running ||
		downstream_incoming_thread_is_running ||
		downstream_outgoing_thread_is_running;
}

bool all_algo_threads_are_running()
{
	return upstream_incoming_thread_is_running &&
		upstream_outgoing_thread_is_running &&
		market_data_incoming_thread_is_running &&
		market_data_sub_unsub_thread_is_running &&
		algo_thread_is_running &&
		downstream_incoming_thread_is_running &&
		downstream_outgoing_thread_is_running;
}

void stop_algo()
{
	interrupt_queue(_q_upstream_to_algo_r);
	interrupt_queue(_q_downstream_to_algo_r);
	interrupt_queue(_q_market_data_to_algo_r);
	interrupt_queue(_q_algo_to_market_data_r);
	interrupt_queue(_q_algo_to_downstream_r);
	interrupt_queue(_q_algo_to_upstream_r);
	interrupt_queue(_q_upstream_to_algo_w);
	interrupt_queue(_q_downstream_to_algo_w);
	interrupt_queue(_q_market_data_to_algo_w);
	interrupt_queue(_q_algo_to_market_data_w);
	interrupt_queue(_q_algo_to_downstream_w);
	interrupt_queue(_q_algo_to_upstream_w);
	// wait for all threads to finish I
	while (is_any_algo_thread_running()) {
		Sleep(100);
	}
	close_queue(_q_upstream_to_algo_r);
	close_queue(_q_downstream_to_algo_r);
	close_queue(_q_market_data_to_algo_r);
	close_queue(_q_algo_to_market_data_r);
	close_queue(_q_algo_to_downstream_r);
	close_queue(_q_algo_to_upstream_r);
	close_queue(_q_upstream_to_algo_w);
	close_queue(_q_downstream_to_algo_w);
	close_queue(_q_market_data_to_algo_w);
	close_queue(_q_algo_to_market_data_w);
	close_queue(_q_algo_to_downstream_w);
	close_queue(_q_algo_to_upstream_w);

	close_mmap(_parent_orders_mmap_h);
	close_mmap(_child_orders_mmap_h);
	close_mmap(_symbols_mmap_h);
	cleanup_queues();
}

char _perf_stat[1024 * 10];
extern const char *_perf_stat_fmt;
char _perf_stat_json[1024 * 10];
extern const char *_perf_stat_json_fmt;

DWORD WINAPI collect_stats_thread(LPVOID par)
{
	Sleep(3000);
	HANDLE console_h = GetStdHandle(STD_OUTPUT_HANDLE);
	COORD coord;
	coord.X = 0;
	coord.Y = 1;
	SYSTEMTIME st;
	GetLocalTime(&st);
	time_t timer;
	char time_buffer[26];
	struct tm* tm_info;
	while (1) {
		if (stop_stats_thread)
			return 0;
		SetConsoleCursorPosition(console_h, coord);
		Sleep(1000);
		if (!all_algo_threads_are_running())
			continue;
		time(&timer);
		tm_info = localtime(&timer);
		strftime(time_buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);
		WORD timestamp = st.wMilliseconds;
		uint64_t stat_queue_length_buckets_q_upstream_to_algo_r[6];
		uint64_t stat_queue_length_buckets_q_market_data_to_algo_r[6];
		uint64_t stat_queue_length_buckets_q_algo_to_downstream_r[6];
		uint64_t stat_queue_length_buckets_q_downstream_to_algo_r[6];
		uint64_t stat_queue_length_buckets_q_algo_to_market_data_r[6];
		uint64_t stat_queue_length_buckets_q_algo_to_upstream_r[6];
		uint64_t stat_time_in_queue_buckets_q_upstream_to_algo_r[8];
		uint64_t stat_time_in_queue_buckets_q_market_data_to_algo_r[8];
		uint64_t stat_time_in_queueh_buckets_q_algo_to_downstream_r[8];
		uint64_t stat_time_in_queue_buckets_q_downstream_to_algo_r[8];
		uint64_t stat_time_in_queueh_buckets_q_algo_to_market_data_r[8];
		uint64_t stat_time_in_queue_buckets_q_algo_to_upstream_r[8];
		stat_time_in_queue_histo(_q_upstream_to_algo_r,
			stat_time_in_queue_buckets_q_upstream_to_algo_r, 8);
		stat_time_in_queue_histo(_q_market_data_to_algo_r,
			stat_time_in_queue_buckets_q_market_data_to_algo_r, 8);
		stat_time_in_queue_histo(_q_algo_to_downstream_r,
			stat_time_in_queueh_buckets_q_algo_to_downstream_r, 8);
		stat_time_in_queue_histo(_q_downstream_to_algo_r,
		stat_time_in_queue_buckets_q_downstream_to_algo_r, 8);
		stat_time_in_queue_histo(_q_algo_to_market_data_r,
		stat_time_in_queueh_buckets_q_algo_to_market_data_r, 8);
		stat_time_in_queue_histo(_q_algo_to_upstream_r,
		stat_time_in_queue_buckets_q_algo_to_upstream_r, 8);
		stat_queue_length_histo(_q_upstream_to_algo_r,
			stat_queue_length_buckets_q_upstream_to_algo_r, 6);
		stat_queue_length_histo(_q_market_data_to_algo_r,
			stat_queue_length_buckets_q_market_data_to_algo_r, 6);
		stat_queue_length_histo(_q_algo_to_downstream_r,
			stat_queue_length_buckets_q_algo_to_downstream_r, 6);
		stat_queue_length_histo(_q_downstream_to_algo_r,
			stat_queue_length_buckets_q_downstream_to_algo_r, 6);
		stat_queue_length_histo(_q_algo_to_market_data_r,
			stat_queue_length_buckets_q_algo_to_market_data_r, 6);
		stat_queue_length_histo(_q_algo_to_upstream_r,
			stat_queue_length_buckets_q_algo_to_upstream_r, 6);
		uint64_t stat_tick_to_trade_[9];
		stat_tick_to_trade_histo(stat_tick_to_trade_, 9);

		uint64_t _q_upstream_to_algo_troughput = stat_get_and_reset_num_messages(_q_upstream_to_algo_r);
		uint64_t _q_market_data_to_algo_troughput = stat_get_and_reset_num_messages(_q_market_data_to_algo_r);
		uint64_t _q_algo_to_downstream_troughput = stat_get_and_reset_num_messages(_q_algo_to_downstream_r);
		uint64_t _q_downstream_to_algo_troughput = stat_get_and_reset_num_messages(_q_downstream_to_algo_r);
		uint64_t _q_algo_to_market_data_troughput = stat_get_and_reset_num_messages(_q_algo_to_market_data_r);
		uint64_t _q_algo_to_upstream_troughput = stat_get_and_reset_num_messages(_q_algo_to_upstream_r);

		int po_total = _parent_orders->num_parent_orders;
		int po_filled = 0;
		int po_not_filled = 0;
		for (int poh = 0; poh < _parent_orders->num_parent_orders; poh++) {
			volatile parent_order_t *po = &_parent_orders->orders[poh];
			if (po->order.status == ORDER_STATUS_FILLED)
				po_filled++;
			else
				po_not_filled++;
		}
		int co_total = _child_orders->num_child_orders;
		int co_filled = 0;
		int co_not_filled = 0;
		for (int coh = 0; coh < _child_orders->num_child_orders; coh++) {
			volatile child_order_t *co = &_child_orders->orders[coh];
			if (co->order.status == ORDER_STATUS_FILLED)
				co_filled++;
			else
				co_not_filled++;
		}
		sprintf(_perf_stat_json, _perf_stat_json_fmt,
			time_buffer,
			stat_queue_length_buckets_q_upstream_to_algo_r[0],
			stat_queue_length_buckets_q_upstream_to_algo_r[1],
			stat_queue_length_buckets_q_upstream_to_algo_r[2],
			stat_queue_length_buckets_q_upstream_to_algo_r[3],
			stat_queue_length_buckets_q_upstream_to_algo_r[4],
			stat_queue_length_buckets_q_upstream_to_algo_r[5],
			stat_queue_length_buckets_q_market_data_to_algo_r[0],
			stat_queue_length_buckets_q_market_data_to_algo_r[1],
			stat_queue_length_buckets_q_market_data_to_algo_r[2], 
			stat_queue_length_buckets_q_market_data_to_algo_r[3],
			stat_queue_length_buckets_q_market_data_to_algo_r[4],
			stat_queue_length_buckets_q_market_data_to_algo_r[5],
			stat_queue_length_buckets_q_algo_to_downstream_r[0],
			stat_queue_length_buckets_q_algo_to_downstream_r[1],
			stat_queue_length_buckets_q_algo_to_downstream_r[2],
			stat_queue_length_buckets_q_algo_to_downstream_r[3],
		
			stat_queue_length_buckets_q_algo_to_downstream_r[4],
			stat_queue_length_buckets_q_algo_to_downstream_r[5],
			stat_queue_length_buckets_q_downstream_to_algo_r[0],
			stat_queue_length_buckets_q_downstream_to_algo_r[1],
			stat_queue_length_buckets_q_downstream_to_algo_r[2],
			stat_queue_length_buckets_q_downstream_to_algo_r[3],
			stat_queue_length_buckets_q_downstream_to_algo_r[4],
			stat_queue_length_buckets_q_downstream_to_algo_r[5],
			stat_queue_length_buckets_q_algo_to_market_data_r[0],
			stat_queue_length_buckets_q_algo_to_market_data_r[1],
			stat_queue_length_buckets_q_algo_to_market_data_r[2],
			stat_queue_length_buckets_q_algo_to_market_data_r[3],
			stat_queue_length_buckets_q_algo_to_market_data_r[4],
			stat_queue_length_buckets_q_algo_to_market_data_r[5],
			stat_queue_length_buckets_q_algo_to_upstream_r[0],
			stat_queue_length_buckets_q_algo_to_upstream_r[1],
			stat_queue_length_buckets_q_algo_to_upstream_r[2],
			stat_queue_length_buckets_q_algo_to_upstream_r[3],
			stat_queue_length_buckets_q_algo_to_upstream_r[4],
			stat_queue_length_buckets_q_algo_to_upstream_r[5],
			stat_time_in_queue_buckets_q_upstream_to_algo_r[0],
			stat_time_in_queue_buckets_q_upstream_to_algo_r[1],
			stat_time_in_queue_buckets_q_upstream_to_algo_r[2],
			stat_time_in_queue_buckets_q_upstream_to_algo_r[3],
			stat_time_in_queue_buckets_q_upstream_to_algo_r[4],
			stat_time_in_queue_buckets_q_upstream_to_algo_r[5],
			stat_time_in_queue_buckets_q_upstream_to_algo_r[6],
			stat_time_in_queue_buckets_q_upstream_to_algo_r[7],
			stat_time_in_queue_buckets_q_market_data_to_algo_r[0],
			stat_time_in_queue_buckets_q_market_data_to_algo_r[1],
			stat_time_in_queue_buckets_q_market_data_to_algo_r[2],
			stat_time_in_queue_buckets_q_market_data_to_algo_r[3],
			stat_time_in_queue_buckets_q_market_data_to_algo_r[4],
			stat_time_in_queue_buckets_q_market_data_to_algo_r[5],
			stat_time_in_queue_buckets_q_market_data_to_algo_r[6],
			stat_time_in_queue_buckets_q_market_data_to_algo_r[7],
			stat_time_in_queueh_buckets_q_algo_to_downstream_r[0],
			stat_time_in_queueh_buckets_q_algo_to_downstream_r[1],
			stat_time_in_queueh_buckets_q_algo_to_downstream_r[2],
			stat_time_in_queueh_buckets_q_algo_to_downstream_r[3],
			stat_time_in_queueh_buckets_q_algo_to_downstream_r[4],
			stat_time_in_queueh_buckets_q_algo_to_downstream_r[5],
			stat_time_in_queueh_buckets_q_algo_to_downstream_r[6],
			stat_time_in_queueh_buckets_q_algo_to_downstream_r[7],
			stat_time_in_queue_buckets_q_downstream_to_algo_r[0],
			stat_time_in_queue_buckets_q_downstream_to_algo_r[1],
			stat_time_in_queue_buckets_q_downstream_to_algo_r[2],
			stat_time_in_queue_buckets_q_downstream_to_algo_r[3],
			stat_time_in_queue_buckets_q_downstream_to_algo_r[4],
			stat_time_in_queue_buckets_q_downstream_to_algo_r[5],
			stat_time_in_queue_buckets_q_downstream_to_algo_r[6],
			stat_time_in_queue_buckets_q_downstream_to_algo_r[7],
			stat_time_in_queueh_buckets_q_algo_to_market_data_r[0],
			stat_time_in_queueh_buckets_q_algo_to_market_data_r[1],
			stat_time_in_queueh_buckets_q_algo_to_market_data_r[2],
			stat_time_in_queueh_buckets_q_algo_to_market_data_r[3],
			stat_time_in_queueh_buckets_q_algo_to_market_data_r[4],
			stat_time_in_queueh_buckets_q_algo_to_market_data_r[5],
			stat_time_in_queueh_buckets_q_algo_to_market_data_r[6],
			stat_time_in_queueh_buckets_q_algo_to_market_data_r[7],
			stat_time_in_queue_buckets_q_algo_to_upstream_r[0],
			stat_time_in_queue_buckets_q_algo_to_upstream_r[1],
			stat_time_in_queue_buckets_q_algo_to_upstream_r[2],
			stat_time_in_queue_buckets_q_algo_to_upstream_r[3],
			stat_time_in_queue_buckets_q_algo_to_upstream_r[4],
			stat_time_in_queue_buckets_q_algo_to_upstream_r[5],
			stat_time_in_queue_buckets_q_algo_to_upstream_r[6],
			stat_time_in_queue_buckets_q_algo_to_upstream_r[7],
			_q_upstream_to_algo_troughput,
			_q_market_data_to_algo_troughput,
			_q_algo_to_downstream_troughput,
			_q_downstream_to_algo_troughput,
			_q_algo_to_market_data_troughput,
			_q_algo_to_upstream_troughput,
			stat_tick_to_trade_[0],
			stat_tick_to_trade_[1],
			stat_tick_to_trade_[2],
			stat_tick_to_trade_[3],
			stat_tick_to_trade_[4],
			stat_tick_to_trade_[5],
			stat_tick_to_trade_[6],
			stat_tick_to_trade_[7],
			stat_tick_to_trade_[8],
			po_total,
			po_total - po_filled,
			po_filled,
			co_total,
			co_total - co_filled,
			co_filled
		);

		printf(_perf_stat_fmt,
			time_buffer,
			stat_queue_length_buckets_q_upstream_to_algo_r[0],
			stat_queue_length_buckets_q_upstream_to_algo_r[1],
			stat_queue_length_buckets_q_upstream_to_algo_r[2],
			stat_queue_length_buckets_q_upstream_to_algo_r[3],
			stat_queue_length_buckets_q_upstream_to_algo_r[4],
			stat_queue_length_buckets_q_upstream_to_algo_r[5],
			stat_queue_length_buckets_q_market_data_to_algo_r[0],
			stat_queue_length_buckets_q_market_data_to_algo_r[1],
			stat_queue_length_buckets_q_market_data_to_algo_r[2],
			stat_queue_length_buckets_q_market_data_to_algo_r[3],
			stat_queue_length_buckets_q_market_data_to_algo_r[4],
			stat_queue_length_buckets_q_market_data_to_algo_r[5],
			stat_queue_length_buckets_q_algo_to_downstream_r[0],
			stat_queue_length_buckets_q_algo_to_downstream_r[1],
			stat_queue_length_buckets_q_algo_to_downstream_r[2],
			stat_queue_length_buckets_q_algo_to_downstream_r[3],

			stat_queue_length_buckets_q_algo_to_downstream_r[4],
			stat_queue_length_buckets_q_algo_to_downstream_r[5],
			stat_queue_length_buckets_q_downstream_to_algo_r[0],
			stat_queue_length_buckets_q_downstream_to_algo_r[1],
			stat_queue_length_buckets_q_downstream_to_algo_r[2],
			stat_queue_length_buckets_q_downstream_to_algo_r[3],
			stat_queue_length_buckets_q_downstream_to_algo_r[4],
			stat_queue_length_buckets_q_downstream_to_algo_r[5],
			stat_queue_length_buckets_q_algo_to_market_data_r[0],
			stat_queue_length_buckets_q_algo_to_market_data_r[1],
			stat_queue_length_buckets_q_algo_to_market_data_r[2],
			stat_queue_length_buckets_q_algo_to_market_data_r[3],
			stat_queue_length_buckets_q_algo_to_market_data_r[4],
			stat_queue_length_buckets_q_algo_to_market_data_r[5],
			stat_queue_length_buckets_q_algo_to_upstream_r[0],
			stat_queue_length_buckets_q_algo_to_upstream_r[1],
			stat_queue_length_buckets_q_algo_to_upstream_r[2],
			stat_queue_length_buckets_q_algo_to_upstream_r[3],
			stat_queue_length_buckets_q_algo_to_upstream_r[4],
			stat_queue_length_buckets_q_algo_to_upstream_r[5],
			stat_time_in_queue_buckets_q_upstream_to_algo_r[0],
			stat_time_in_queue_buckets_q_upstream_to_algo_r[1],
			stat_time_in_queue_buckets_q_upstream_to_algo_r[2],
			stat_time_in_queue_buckets_q_upstream_to_algo_r[3],
			stat_time_in_queue_buckets_q_upstream_to_algo_r[4],
			stat_time_in_queue_buckets_q_upstream_to_algo_r[5],
			stat_time_in_queue_buckets_q_upstream_to_algo_r[6],
			stat_time_in_queue_buckets_q_upstream_to_algo_r[7],
			stat_time_in_queue_buckets_q_market_data_to_algo_r[0],
			stat_time_in_queue_buckets_q_market_data_to_algo_r[1],
			stat_time_in_queue_buckets_q_market_data_to_algo_r[2],
			stat_time_in_queue_buckets_q_market_data_to_algo_r[3],
			stat_time_in_queue_buckets_q_market_data_to_algo_r[4],
			stat_time_in_queue_buckets_q_market_data_to_algo_r[5],
			stat_time_in_queue_buckets_q_market_data_to_algo_r[6],
			stat_time_in_queue_buckets_q_market_data_to_algo_r[7],
			stat_time_in_queueh_buckets_q_algo_to_downstream_r[0],
			stat_time_in_queueh_buckets_q_algo_to_downstream_r[1],
			stat_time_in_queueh_buckets_q_algo_to_downstream_r[2],
			stat_time_in_queueh_buckets_q_algo_to_downstream_r[3],
			stat_time_in_queueh_buckets_q_algo_to_downstream_r[4],
			stat_time_in_queueh_buckets_q_algo_to_downstream_r[5],
			stat_time_in_queueh_buckets_q_algo_to_downstream_r[6],
			stat_time_in_queueh_buckets_q_algo_to_downstream_r[7],
			stat_time_in_queue_buckets_q_downstream_to_algo_r[0],
			stat_time_in_queue_buckets_q_downstream_to_algo_r[1],
			stat_time_in_queue_buckets_q_downstream_to_algo_r[2],
			stat_time_in_queue_buckets_q_downstream_to_algo_r[3],
			stat_time_in_queue_buckets_q_downstream_to_algo_r[4],
			stat_time_in_queue_buckets_q_downstream_to_algo_r[5],
			stat_time_in_queue_buckets_q_downstream_to_algo_r[6],
			stat_time_in_queue_buckets_q_downstream_to_algo_r[7],
			stat_time_in_queueh_buckets_q_algo_to_market_data_r[0],
			stat_time_in_queueh_buckets_q_algo_to_market_data_r[1],
			stat_time_in_queueh_buckets_q_algo_to_market_data_r[2],
			stat_time_in_queueh_buckets_q_algo_to_market_data_r[3],
			stat_time_in_queueh_buckets_q_algo_to_market_data_r[4],
			stat_time_in_queueh_buckets_q_algo_to_market_data_r[5],
			stat_time_in_queueh_buckets_q_algo_to_market_data_r[6],
			stat_time_in_queueh_buckets_q_algo_to_market_data_r[7],
			stat_time_in_queue_buckets_q_algo_to_upstream_r[0],
			stat_time_in_queue_buckets_q_algo_to_upstream_r[1],
			stat_time_in_queue_buckets_q_algo_to_upstream_r[2],
			stat_time_in_queue_buckets_q_algo_to_upstream_r[3],
			stat_time_in_queue_buckets_q_algo_to_upstream_r[4],
			stat_time_in_queue_buckets_q_algo_to_upstream_r[5],
			stat_time_in_queue_buckets_q_algo_to_upstream_r[6],
			stat_time_in_queue_buckets_q_algo_to_upstream_r[7],
			_q_upstream_to_algo_troughput,
			_q_market_data_to_algo_troughput,
			_q_algo_to_downstream_troughput,
			_q_downstream_to_algo_troughput,
			_q_algo_to_market_data_troughput,
			_q_algo_to_upstream_troughput,
			stat_tick_to_trade_[0],
			stat_tick_to_trade_[1],
			stat_tick_to_trade_[2],
			stat_tick_to_trade_[3],
			stat_tick_to_trade_[4],
			stat_tick_to_trade_[5],
			stat_tick_to_trade_[6],
			stat_tick_to_trade_[7],
			stat_tick_to_trade_[8],
			po_total,
			po_total - po_filled,
			po_filled,
			co_total,
			co_total - co_filled,
			co_filled
		);
	}
	return 0;
}
