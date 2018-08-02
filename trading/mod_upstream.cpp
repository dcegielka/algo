#include "../queue/queue.h"
#include "algo.h"

/*
* thread that imitates incoming upstream events
*/
DWORD WINAPI upstream_incoming_thread(LPVOID par)
{
	upstream_incoming_thread_is_running = true;
	/* this thread is not latency—critical and doesn't have to be pinned */
	int16_t r;
	uint64_t unique_order_id = 0;
	uint64_t seh_counter = 0;
	while (1) {
		if (is_queue_interrupted(_q_upstream_to_algo_w))
			break;
		/* interval between incoming orders */
		Sleep(1000);
		unique_order_id++;
		if (MAX_TOTAL_PARENT_ORDERS == _parent_orders->num_parent_orders) {
			/* reached max num of parent orders;
			* IMPLEMENTATION: send reject order upstream, notify dispatcher
			* to stop routing orders to this engine
			*/
			continue;
		}
		/* INTEGRATION: read incoming orders from source — FIX, socket etc;
		* create mock parent order
		*/
		security_handle_t seh = seh_counter % _symbols->num_symbols;
		seh_counter++;
		qty_t qty = 100000;
		side_e side = SIDE_BUY;
		algo_type_e algo_type = ALGO_TYPE_POV;
		order_type_e order_type = ORDER_TYPE_MARKET;
		/* send it to algo */
		r = create_new_parent_order(
			algo_type,
			seh,
			qty,
			side,
			order_type,
			unique_order_id);
		if (ERR_QUEUE_INTERRUPTED == r)
			break;
	}
	upstream_incoming_thread_is_running = false;
	return 0;
}

/*
* thread that recieves order status updates from algo and
* sends notifications upstream
*/
DWORD WINAPI upstream_outgoing_thread(LPVOID par)
{
	upstream_outgoing_thread_is_running = true;
	event_t e;
	while (1) {
		/*
		* read parent order status events from algo:
		* partially filled, filled, canceled
		*/
		if (ERR_QUEUE_INTERRUPTED == read_event(_q_algo_to_upstream_r, &e))
			break;
		switch (e.event_type) {
		case EVENT_TYPE_ORDER_STATUS:
			order_handle_t poh = e.event_body.order_status_event.order_h;
			parent_order_t *po = (parent_order_t *)&_parent_orders->orders[poh];
			/*
			* INTEGRATION:
			* send new order status (parent_order—>order.status) upstream
			*/
			break;
		}
	}
	upstream_outgoing_thread_is_running = false;
	return 0;
}

/*
* create new parent order and send it to algo
*/
int16_t create_new_parent_order(
	algo_type_e algo_type,
	security_handle_t seh,
	qty_t qty,
	side_e side,
	order_type_e order_type,
	uint64_t unique_order_id)
{
	order_handle_t next_poh = _parent_orders->num_parent_orders;
	/* init parent order */
	volatile parent_order_t *po = &_parent_orders->orders[next_poh];
	po->order.status = ORDER_STATUS_NEW;
	po->num_child_orders = 0;
	po->order.id = unique_order_id;
	po->order.security_h = seh;
	po->order.side = side;
	po->order.type = order_type;
	po->order.qty = PARENT_ORDER_QTY;
	po->algo_type = algo_type;
	po->algo_context.algo_context_pov.accum_volume = 0;
	po->filled_qty = 0;
	MemoryBarrier();
	_parent_orders->num_parent_orders++;
	/* send new parent order handle to algo */
	event_t e;
	e.event_type = EVENT_TYPE_NEW_ORDER;
	e.event_body.new_order_event.order_h = next_poh;
	return write_event(_q_upstream_to_algo_w, &e);
}
