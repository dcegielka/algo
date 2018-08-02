#include "../queue/queue.h"
#include "algo.h"

/*
* thread listen to downstream events,
* i.e. child order statuses and forwards them to algo
*/
DWORD WINAPI downstream_incoming_thread(LPVOID par)
{
	downstream_incoming_thread_is_running = true;
	event_t e;
	e.event_type = EVENT_TYPE_ORDER_STATUS;
	while (1) {
		if (is_queue_interrupted(_q_downstream_to_algo_w))
			break;
		/*
		* INTEGRATION: listen to downstream events, e.g. from FIX gateway
		* DEMO: scan child orders, find ones that are
		* pending and update status to filled
		*/
		order_handle_t coh;
		for (coh = 0; coh < _child_orders->num_child_orders; coh++) {
			volatile child_order_t *co = &_child_orders->orders[coh];
			if (ORDER_STATUS_PENDING == co->order.status) {
				// TODO make this configurable to user can change fill rate
				/* DEMO: fill child orders every X milliseconds */
				Sleep(50);
				co->order.status = ORDER_STATUS_FILLED;
				e.event_body.order_status_event.order_h = coh;
				int16_t r =
					write_event(_q_downstream_to_algo_w, &e);
				if (ERR_QUEUE_INTERRUPTED == r)
					break;
			}
		}
	}
	downstream_incoming_thread_is_running = false;
	return 0;
}
/*
* thread receives the child orders from algo and send them for execution
*/
DWORD WINAPI downstream_outgoing_thread(LPVOID par)
{
	pin_thread_to_core(3);
	downstream_outgoing_thread_is_running = true;
	event_t e;
	while (1) {
		int16_t r = read_event(_q_algo_to_downstream_r, &e);
		if (ERR_QUEUE_INTERRUPTED == r)
			break;
		order_handle_t coh;
		uint64_t tick2trade;
		switch (e.event_type) {
		case EVENT_TYPE_NEW_ORDER:
			coh = e.event_body.new_order_event.order_h;
			_child_orders->orders[coh].order.status = ORDER_STATUS_PENDING;
			/* INTEGRATION: send new child order for execution downstream */
			LARGE_INTEGER trade_time;
			QueryPerformanceCounter(&trade_time);
			tick2trade =
				trade_time.QuadPart - _child_orders->orders[coh].tick_timestamp;
			update_tick_to_trade_counter(tick2trade);
			break;
		case EVENT_TYPE_CANCEL_ORDER:
			coh = e.event_body.new_order_event.order_h;
			_child_orders->orders[coh].order.status =
				ORDER_STATUS_CANCELED_PENDING;
			/* INTEGRATION: send cancel child order for execution downstream */
		}
	}
	downstream_outgoing_thread_is_running = false;
	return 0;
}
