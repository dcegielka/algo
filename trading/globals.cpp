#include "../queue/queue.h"
#include "algo.h"

/* GLOBAL STATE */
/* a pre-allocated block of parent orders */
volatile parent_orders_t *_parent_orders;
mmap_handle_t _parent_orders_mmap_h;
/* a pre-allocated block of child orders */
volatile child_orders_t *_child_orders;
mmap_handle_t _child_orders_mmap_h;
/* security master */
volatile symbols_t *_symbols;
mmap_handle_t _symbols_mmap_h;
/* subscriptions  mapping:
 * "security -> linked list of parent orders, subscribed for this security";
 * reading thread is accessing only _security_subscriptions_root
 */
volatile sec_subscriptions_t *_sec_subscriptions;
mmap_handle_t _sec_subscriptions_mmap_h;

/* flags indicating which threads are running */
volatile bool upstream_incoming_thread_is_running = false;
volatile bool upstream_outgoing_thread_is_running = false;
volatile bool market_data_incoming_thread_is_running = false;
volatile bool market_data_sub_unsub_thread_is_running = false;
volatile bool algo_thread_is_running = false;
volatile bool downstream_incoming_thread_is_running = false;
volatile bool downstream_outgoing_thread_is_running = false;
/* reading ends of the queues */
volatile queue_handle_t _q_upstream_to_algo_r;
volatile queue_handle_t _q_downstream_to_algo_r;
volatile queue_handle_t _q_market_data_to_algo_r;
volatile queue_handle_t _q_algo_to_market_data_r;
volatile queue_handle_t _q_algo_to_downstream_r;
volatile queue_handle_t _q_algo_to_upstream_r;
/* writing ends of the queues */ 
volatile queue_handle_t _q_upstream_to_algo_w;
volatile queue_handle_t _q_downstream_to_algo_w;
volatile queue_handle_t _q_market_data_to_algo_w;
volatile queue_handle_t _q_algo_to_market_data_w;
volatile queue_handle_t _q_algo_to_downstream_w;
volatile queue_handle_t _q_algo_to_upstream_w;

const char *_perf_stat_fmt = "%s \n\
Queue Length Histogram (microseconds) \n\
upstream -> algo: [0-1]: %3d; [2-3]: %3d; [4-7]: %3d; [8-127]: %3d; [128-1023]: %3d; [1024-]: %3d \n\
market_data -> algo: [0-1]: %3d; [2-3]: %3d; [4-7]: %3d; [8-127]: %3d; [128-1023]: %3d; [1024-]: %3d \n\
algo -> downstream: [0-1]: %3d; [2-3]: %3d; [4-7]: %3d; [8-127]: %3d; [128-1023]: %3d; [1024-]: %3d \n\
downstream -> algo: [O-l]: %3d; [2-3]: %3d; [4-7]: %3d; [8-127]: %3d; [128-1023]: %3d; [1024~]: %3d \n\
algo -> market*data: [O-l]: %3d; [2-3]: %3d; [4-7]: %3d; [8-127]: %3d; [128-1023]: %3d; [1024-]: %3d \n\
algo -> upstream: [0-1]: %3d; [2-3]: %3d; [4-7]: %3d; [8-127]: %3d; [128-1023]: %3d; [1024-]: %3d \n\
\n\
Time in Queue Histogram (microseconds) \n\
upstream -> algo: [O-l]: %3d; [2-3]: %3d; [4-7]: %3d; [8-15]: %3d; [16-31]: %3d; [32-127]: %3d; [128-1023]: %3d; [1024-]: %3d \n\
market_data -> algo: [0-1]: %3d; [2-3]: %3d; [4-7]: %3d; [8-15]: %3d; [16-31]: %3d; [32-127]: %3d; [128-1023]: %3d; [1024-]: %3d \n\
algo -> downstream: [0-1]: %3d; [2-3]: %3d; [4-7]: %3d; [8-15]: %3d; [16-31]: %3d; [32-127]: %3d; [128-1023]: %3d; [1024-]: %3d \n\
downstream -> algo: [O-l]: %3d; [2-3]: %3d; [4-7]: %3d; [8-15]: %3d; [16-31]: %3d; [32-127]: %3d; [128-1023]: %3d; [1024-]: %3d \n\
algo -> market_data: [O-l]: %3d; [2-3]: %3d; [4-7]: %3d; [8-15]: %3d; [16-31]: %3d; [32-127]: %3d; [128-1023]: %3d; [1024~]: %3d \n\
algo -> upstream: [O-l]: %3d; [2-3]: %3d; [4-7]: %3d; [8-15]: %3d; [16-31]: %3d; [32-127]: %3d; [128-1023]: %3d; [1024-]: %3d \n\
\n\
Throughput (messages per second): \n\
upstream -> algo: %d                  \n\
market_data -> algo: %d               \n\
algo -> downstream: %d                \n\
downstream -> algo: %d \n\
algo -> market_data: %d \n\
algo -> upstream: %d \n\
\n\
Tick-to-trade Histogram (microseconds): \n\
[O-l]: %3d; [2-3]: %3d; [4-7]: %3d; [8-15]: %3d; [16-31]: %3d; [32-63]: %3d; [64-127]: %3d; [128-1023]: %3d; [1024-]: %3d \n\
\n\
Total Parent Orders: %d \n\
Active Parent Orders: %d \n\
Filled Parent Orders: %d \n\
\n\
Total Child Orders: %d \n\
Active Child Orders: %d \n\
Filled Child Orders: %d \n\
\n";

const char *_perf_stat_json_fmt = "{\
\"timestamp\":\"%s\", \n\
\"queue_length_histogram\": { \n\
	\"upstream_to_algo\": {\"0-1\": %3d, \"2-3\": %3d, \"4-7\": %3d, \"8-127\": %3d, \"128-1023\": %3d, \"1024-\": %3d}, \n\
	\"market_data_to_algo\": {\"0-1\": %3d, \"2-3\": %3d, \"4-7\": %3d, \"8-127\": %3d, \"128-1023\": %3d, \"1024-\": %3d}, \n\
	\"algo_to_downstream\": {\"0-1\": %3d, \"2-3\": %3d, \"4-7\": %3d, \"8-127\": %3d, \"128-1023\": %3d, \"1024-\": %3d}, \n\
	\"downstream_to_algo\": {\"0-1\": %3d, \"2-3\": %3d, \"4-7\": %3d, \"8-127\": %3d, \"128-1023\": %3d, \"1024-\": %3d}, \n\
	\"algo_to_market_data\": {\"0-1\": %3d, \"2-3\": %3d, \"4-7\": %3d, \"8-127\": %3d, \"128-1023\": %3d, \"1024-\": %3d}, \n\
	\"algo_to_upstream\": {\"0-1\": %3d, \"2-3\": %3d, \"4-7\": %3d, \"8-127\": %3d, \"128-1023\": %3d, \"1024-\": %3d} \n\
}, \n\
\"time_in_queue_histogram\": { \n\
	\"upstream_to_algo\": {\"0-1\": %3d,  \"2-3\": %3d,  \"4-7\": %3d,  \"8-15\": %3d,  \"16-31\": %3d,  \"32-127\": %3d,  \"128-1023\": %3d,  \"1024-\": %3d}, \n\
	\"market_data_to_algo\":{ \"0-1\": %3d,  \"2-3\": %3d,  \"4-7\": %3d,  \"8-15\": %3d,  \"16-31\": %3d,  \"32-127\": %3d,  \"128-1023\": %3d,  \"1024-\": %3d}, \n\
	\"algo_to_downstream\": {\"0-1\": %3d,  \"2-3\": %3d,  \"4-7\": %3d,  \"8-15\": %3d,  \"16-31\": %3d,  \"32-127\": %3d,  \"128-1023\": %3d,  \"1024-\": %3d}, \n\
	\"downstream_to_algo\": {\"0-1\": %3d,  \"2-3\": %3d,  \"4-7\": %3d,  \"8-15\": %3d,  \"16-31\": %3d,  \"32-127\": %3d,  \"128-1023\": %3d,  \"1024-\": %3d}, \n\
	\"algo_to_market_data\": {\"0-1\": %3d,  \"2-3\": %3d,  \"4-7\": %3d,  \"8-15\": %3d,  \"16-31\": %3d,  \"32-127\": %3d,  \"128-1023\": %3d,  \"1024~\": %3d}, \n\
	\"algo_to_upstream\": {\"0-1\": %3d,  \"2-3\": %3d,  \"4-7\": %3d,  \"8-15\": %3d,  \"16-31\": %3d,  \"32-127\": %3d,  \"128-1023\": %3d,  \"1024-\": %3d} \n\
},\n\
\"throughput\": { \n\
	\"upstream_to_algo\": %d, \n\
	\"market_data_to_algo\": %d, \n\
	\"algo_to_downstream\": %d, \n\
	\"downstream_to_algo\": %d, \n\
	\"algo_to_market_data\": %d, \n\
	\"algo_to_upstream\": %d \n\
},\n\
\"tick_to_trade_histogram\": { \n\
	\"0-1\": %3d,  \"2-3\": %3d,  \"4-7\": %3d,  \"8-15\": %3d,  \"16-31\": %3d,  \"32-63\": %3d,  \"64-127\": %3d,  \"128-1023\": %3d,  \"1024-\": %3d \n\
},\n\
\"orders_stat\": { \n\
	\"total_parent_orders\": %d, \n\
	\"active_parent_orders\": %d, \n\
	\"filled_parent_orders\": %d, \n\
	\n\
	\"total_child_orders\": %d, \n\
	\"active_child_orders\": %d, \n\
	\"filled_child_orders\": %d \n\
}\n\
}\n";
