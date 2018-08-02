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

#define MAX_TOTAL_PARENT_ORDERS 1000
#define MAX_CHILD_ORDERS_PER_PARENT 1000
#define MAX_TOTAL_CHILD_ORDERS \
	MAX_TOTAL_PARENT_ORDERS*MAX_CHILD_ORDERS_PER_PARENT
#define MAX_TOTAL_SYMBOLS 10000
/* DEMO algo parameters */
#define SLICE_OTY 100
#define PARENT_ORDER_QTY 10000
#define POV_THRESHOLD 10000

#define UPSTREAM_TO_ALGO_QUEUE "upstream_to_algo.queue"
#define DOWNSTREAM_TO_ALGO_QUEUE "downstream_to_algo.queue"
#define MARKET_DATA_TO_ALGO_QUEUE "market_data_to_algo.queue"
#define ALGO_TO_MARKET_DATA_QUEUE "algo_to_market_data.queue"
#define ALGO_TO_DOWNSTREAM_QUEUE "algo_to_downstream.queue"
#define ALGO_TO_UPSTREAM_QUEUE "algo_to_upstream.queue"

#define PARENT_ORDERS_MMAP "parent_orders.mmap"
#define CHILD_ORDERS_MMAP "child_orders.mmap"
#define SYMBOLS_MMAP "symbols.mmap"
#define SUBSCRIPTIONS_MMAP "subscriptions.mmap"

enum order_type_e {
	ORDER_TYPE_MARKET,
	ORDER_TYPE_LIMIT
};
enum order_status_e {
	ORDER_STATUS_NEW,
	ORDER_STATUS_PENDING,
	ORDER_STATUS_FILLED,
	ORDER_STATUS_PARTIALLY_FILLED,
	ORDER_STATUS_CANCELED_PENDING
	/* IMPLEMENTATION: add order statuses here */
};
enum algo_type_e {
	ALGO_TYPE_POV
	/* IMPLEMENTATION: add algo types here */
};
enum side_e {
	SIDE_BUY, 
	SIDE_SELL
};
enum market_data_type_e {
	MARKET_DATA_TYPE_TRADE,
	MARKET_DATA_TYPE_QUOTE
};
enum event_type_e {
	EVENT_TYPE_TIMER,
	EVENT_TYPE_NEW_ORDER,
	EVENT_TYPE_CANCEL_ORDER,
	EVENT_TYPE_ORDER_STATUS,
	EVENT_TYPE_MARKET_DATA,
	EVENT_TYPE_MARKET_DATA_SUB,
	EVENT_TYPE_MARKET_DATA_UNSUB
};
typedef int32_t order_handle_t;
typedef int32_t security_handle_t;
typedef uint64_t price_t;
typedef uint64_t qty_t;
struct order_t {
	DWORD lock;
	uint64_t id;
	order_type_e type;
	order_status_e status;
	security_handle_t security_h;
	qty_t qty;
	price_t price;
	side_e side;
	uint64_t timestamp;
};
struct child_order_t {
	order_handle_t parent_order_h;
	uint64_t tick_timestamp;
order_t order;
};
struct algo_context_pov_t {
	qty_t accum_volume;
	/* IMPLEMENTATION: add algo context details here */
};
struct parent_order_t {
	DWORD lock;
	order_t order;
	order_handle_t child_orders_h[MAX_CHILD_ORDERS_PER_PARENT];
	uint32_t num_child_orders;
	algo_type_e algo_type;
	qty_t sent_qty;
	qty_t filled_qty;
	union algo_context_t {
		algo_context_pov_t algo_context_pov;
		/* IMPLEMENTATION: add algo types contexts here */
	} algo_context;
};
/*
* preallocated block of parent orders;
* each parent order has an array of handles pointing to child orders
* from child orders block
*/
struct parent_orders_t { 
	parent_order_t orders[MAX_TOTAL_PARENT_ORDERS];
	int32_t num_parent_orders;
};
/* preallocated block of child orders */
struct child_orders_t {
	child_order_t orders[MAX_TOTAL_CHILD_ORDERS];
	int32_t num_child_orders;
};
struct symbol_t {
	char symbol[12];
};
/*
* symbols need to be sorted for faster lookup
* securitywhandler_t is an index in this table;
* IMPLEMENTATION: add venues, commissions, trading hours etc
*/
struct symbols_t {
	symbol_t symbols[MAX_TOTAL_SYMBOLS];
	int32_t num_symbols;
};

volatile struct sec_subscriptions_t {
	order_handle_t _security_subscriptions[MAX_TOTAL_SYMBOLS] \
		[MAX_TOTAL_PARENT_ORDERS];
	mmap_handle_t _security_subscriptions_mmap_h;
	order_handle_t _security_subscriptions_root[MAX_TOTAL_SYMBOLS];
	mmap_handle_t _security_subscriptions_root_mmap_h;
	DWORD _security_subscriptions_write_lock[MAX_TOTAL_SYMBOLS];
	mmap_handle_t _security_subscriptions_write_lock_mmap_h;
};

struct timer_event_t {
	uint64_t timestamp;
};
	/* upstream tells algo that new order arrived */
struct new_order_event_t {
	order_handle_t order_h;
};
/* status of referenced order has changed */
struct order_status_event_t {
	order_handle_t order_h;
};
struct cancel_order_event_t {
	order_handle_t order_h;
};
struct mkt_data_event_t {
	market_data_type_e market_data_type;
	security_handle_t security_h;
	side_e side;
	qty_t qty;
	price_t price;
	uint64_t tick_timestamp;
};
struct sub_unsub_event_t {
	order_handle_t parent_order_h;
	security_handle_t security_h;
};
/* union of all possible event types; this will be go to queue */
struct event_t {
	event_type_e event_type;
	union {
		timer_event_t timer_event;
		new_order_event_t new_order_event;
		order_status_event_t order_status_event;
		cancel_order_event_t cancel_order_event;
		mkt_data_event_t mkt_data_event;
		sub_unsub_event_t sub_unsub_event;
	} event_body;
};
/* functions */
void start_algo(char *home_folder);
void stop_algo();
int16_t generate_market_data(event_t *event_to_algo, security_handle_t sec);
int16_t market_data_sub(order_handle_t poh, security_handle_t seh);
int16_t market_data_unsub(order_handle_t poh, security_handle_t seh); 
int16_t algo_process_new_parent_order(order_handle_t new_order_h);
int16_t algo_process_market_data(
	order_handle_t parent_order_h,
	qty_t qty,
	price_t price,
	uint64_t tick_time); 
int16_t algo_process_order_status(order_handle_t child_order_h);
int16_t create_new_parent_order(
	algo_type_e algo_type,
	security_handle_t security_h,
	qty_t qty,
	side_e side,
	order_type_e order_type,
	uint64_t unique_order_id
);

int16_t read_event(queue_handle_t q, event_t *e);
int16_t read_events(
	queue_handle_t queues[],
	event_t events[],
	bool queue_got_data[],
	int num_of_queues);
int16_t write_event(queue_handle_t q, event_t *e);
int16_t write_event_conflated(queue_handle_t q, event_t *e, uint32_t key);
bool is_any_algo_thread_running();
bool all_algo_threads_are_running();
void stat_tick_to_trade(char str[], uint16_t str_len);
void update_tick_to_trade_counter(uint64_t elapsed);
/* threads */
DWORD WINAPI upstream_incoming_thread(LPVOID par);
DWORD WINAPI upstream_outgoing_thread(LPVOID par);
DWORD WINAPI market_data_incoming_thread(LPVOID par);
DWORD WINAPI market_data_sub_unsub_thread(LPVOID par);
DWORD WINAPI algo_thread(LPVOID par);
DWORD WINAPI downstream_incoming_thread(LPVOID par);
DWORD WINAPI downstream_outgoing_thread(LPVOID par);
DWORD WINAPI collect_stats_thread(LPVOID par);
DWORD WINAPI httpd_thread(LPVOID par);

// TODO move below to structs
/* externs */
extern volatile symbols_t *_symbols;
extern mmap_handle_t _symbols_mmap_h;
extern volatile parent_orders_t *_parent_orders;
extern mmap_handle_t _parent_orders_mmap_h;
extern volatile child_orders_t *_child_orders;
extern mmap_handle_t _child_orders_mmap_h;
extern volatile sec_subscriptions_t *_sec_subscriptions;
extern mmap_handle_t _sec_subscriptions_mmap_h;
/*extern volatile order_handle_t *_security_subscriptions[MAX_TOTAL_SYMBOLS] \
	[MAX_TOTAL_PARENT_ORDERS];
extern mmap_handle_t _security_subscriptions_mmap_h;
extern volatile order_handle_t *_security_subscriptions_root[MAX_TOTAL_SYMBOLS];
extern mmap_handle_t _security_subscriptions_root_mmap_h;
extern volatile DWORD *_security_subscriptions_write_lock[MAX_TOTAL_SYMBOLS];
extern mmap_handle_t _security_subscriptions_write_lock_mmap_h;*/

extern uint64_t _demo_security_last_price[MAX_TOTAL_SYMBOLS];
/* reading ends of the queues */
extern volatile queue_handle_t _q_upstream_to_algo_r;
extern volatile queue_handle_t _q_downstream_to_algo_r;
extern volatile queue_handle_t _q_market_data_to_algo_r;
extern volatile queue_handle_t _q_algo_to_market_data_r;
extern volatile queue_handle_t _q_algo_to_downstream_r;
extern volatile queue_handle_t _q_algo_to_upstream_r;
/* writing ends of the queues */
extern volatile queue_handle_t _q_timer_to_algo_w;
extern volatile queue_handle_t _q_upstream_to_algo_w;
extern volatile queue_handle_t _q_downstream_to_algo_w;
extern volatile queue_handle_t _q_market_data_to_algo_w;
extern volatile queue_handle_t _q_algo_to_market_data_w;
extern volatile queue_handle_t _q_algo_to_downstream_w;
extern volatile queue_handle_t _q_algo_to_upstream_w;
/* thread status */
extern volatile bool upstream_incoming_thread_is_running;
extern volatile bool upstream_outgoing_thread_is_running;
extern volatile bool market_data_incoming_thread_is_running;
extern volatile bool market_data_sub_unsub_thread_is_running;
extern volatile bool algo_thread_is_running;
extern volatile bool downstream_incoming_thread_is_running;
extern volatile bool downstream_outgoing_thread_is_running;
extern const char *_perf_stat_fmt;
