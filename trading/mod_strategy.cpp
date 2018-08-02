#include "../queue/queue.h"
#include "algo.h"


/*
* algo thread; trades parent orders by slicing them into child orders and
* sending child orders for execution based on current market conditions;
*
* thread receives following events:
* - market data, on which we trade
* - child order statuses from downstream
* - new/canceled orders from upstream
*
* thread sends following events:
* - parent order updates to upstream
* - sub/unsub to market data
* - child orders to downstream
*/
DWORD WINAPI algo_thread(LPVOID par)
{
	algo_thread_is_running = true;
	/* this is performance—critical thread, pin it to core */
	pin_thread_to_core(2);
	/* this thread reads from following queues */
	const int num_of_queues = 3;
	queue_handle_t queues[num_of_queues] = {
		_q_downstream_to_algo_r,
		_q_market_data_to_algo_r,
		_q_upstream_to_algo_r
	};
	/* events that we receive from above queues */
	event_t events[num_of_queues];
	/* array of flags indicating which queue got data */
	bool got_data[num_of_queues];
	while (1) {
		if (MAX_TOTAL_CHILD_ORDERS == _child_orders->num_child_orders) {
			/* ran out of child orders handles */
			stop_algo();
			goto cleanup;
		}
		/* read incoming events */
		int16_t r = read_events(queues, events, got_data, num_of_queues);
		if (ERR_QUEUE_INTERRUPTED == r)
			break;
		/* see what queues received data and process it */
		for (uint16_t i = 0; i < num_of_queues; i++) {
			if (!got_data[i])
				continue;
			/* queue received data; process event */
			event_t *e = &events[i];
			security_handle_t seh;
			order_handle_t poh;
			/*
			* note the pattern in each case: we read
			* specific event type from union and work with it
			*/
			switch (e->event_type)
			{
			case EVENT_TYPE_NEW_ORDER:
				/*
				* new order from upstream;
				* init algo context, subscribe for market data
				*
				*/
				new_order_event_t e_new_order = e->event_body.new_order_event;
				algo_process_new_parent_order(e_new_order.order_h);
				break;
			case EVENT_TYPE_MARKET_DATA:
				/* received market data */
				mkt_data_event_t e_mkt_data = e->event_body.mkt_data_event;
				seh = e_mkt_data.security_h;
				// TODO implement lock—free multi—version subsciption table
				lock(&_sec_subscriptions->_security_subscriptions_write_lock[seh]);
				poh = _sec_subscriptions->_security_subscriptions_root[seh];
				if (-1 == poh) {
					/*
					* no one is subscribed to this market data;
					* this event must have been sent before unsub was processed
					*/
					unlock(&_sec_subscriptions->_security_subscriptions_write_lock[seh]);
					break;
				}
				/*
				* iterate over the list of parent orders that
				* are subscribed to this security
				*/
				do {
					seh = e_mkt_data.security_h;
					/*
					* show market data to parent order;
					* update accum volume counters, send child orders etc.
					*/
					qty_t qty = e_mkt_data.qty;
					price_t price = e_mkt_data.price;
					uint64_t ts = e_mkt_data.tick_timestamp;
					algo_process_market_data(poh, qty, price, ts);
					/* next parent order */
					poh = _sec_subscriptions->_security_subscriptions[seh][poh];
				} while (-1 != poh);
				unlock(&_sec_subscriptions->_security_subscriptions_write_lock[seh]);
				break;
			case EVENT_TYPE_ORDER_STATUS:
				/*
				* we have received updated child order status from
				* downstream, e.g. filled/rejected;
				* update algo context for its parent order:
				* update 'filled' counters etc
				*/
				order_status_event_t e_order_status =
					e->event_body.order_status_event;
				algo_process_order_status(e_order_status.order_h);
				break;
			}
		}
	}
cleanup:
	algo_thread_is_running = false;
	return 0;
}

/*
* algo received new parent order;
* update order status, subscribe for market data and init algo context
*/
int16_t algo_process_new_parent_order(order_handle_t new_poh)
{
	volatile parent_order_t *po = &_parent_orders->orders[new_poh];
	po->order.status = ORDER_STATUS_PENDING;
	/* subscribe for market data */
	event_t e;
	e.event_type = EVENT_TYPE_MARKET_DATA_SUB;
	e.event_body.sub_unsub_event.parent_order_h = new_poh;
	e.event_body.sub_unsub_event.security_h = po->order.security_h;
	write_event(_q_algo_to_market_data_w, &e);
	/* init algo context */
	switch (po->algo_type)
	{
	case ALGO_TYPE_POV:
		volatile algo_context_pov_t *pov_ctx =
			&po->algo_context.algo_context_pov;
		/* initialize POV algo counters */
		pov_ctx->accum_volume = 0;
		po->filled_qty = 0;
		break;
		/* INTEGRATION: insert new algo types here */
	}
	return 0;
}

/*
* this function runs on algo thread;
* algo received new market data;
* allow corresponding algo type process it, for example POV updates the
* accumulated volume counter and when it reaches threshold —
* send a child order downstream
*/
int16_t algo_process_market_data(
	order_handle_t poh,
	qty_t qty,
	price_t price,
	uint64_t tick_timestamp)
{
	volatile parent_order_t *po = &_parent_orders->orders[poh];
	if (po->sent_qty == po->order.qty) {
		/*
		* we have sent enough child orders to fill parent;
		* now wait for all child orders to be executed
		*/
		return 0;
	}
	switch (po->algo_type)
	{
	case ALGO_TYPE_POV:
		volatile algo_context_pov_t *ctx_pov =
			&po->algo_context.algo_context_pov;
		ctx_pov->accum_volume += qty;
		if (POV_THRESHOLD < ctx_pov->accum_volume) {
			/*
			* accumulated enough volume;
			* grab next available child order and fill it up
			*/
			order_handle_t coh = _child_orders->num_child_orders;
			_child_orders->num_child_orders++;
			volatile child_order_t *co = &_child_orders->orders[coh];
			co->order.status = ORDER_STATUS_NEW;
			co->order.side = po->order.side;
			/* take market price */
			co->order.type = ORDER_TYPE_MARKET;
			co->order.price = price;
			/* link child order back to parent */
			co->parent_order_h = poh;
			/*
			* if adding full slice overshoots the total order size,
			* then reduce slice size to match parent order qty
			*/
			qty_t qty = po->order.qty < po->sent_qty + SLICE_OTY ?
				po->order.qty - po->sent_qty :
				SLICE_OTY;
			po->sent_qty += qty;
			co->order.qty = qty;
			co->order.security_h = po->order.security_h;
			co->tick_timestamp = tick_timestamp;
			MemoryBarrier();
			/* add child order to parent order */
			po->child_orders_h[po->num_child_orders] = coh;
			/* TODO check if exceeded max child order handles ... */
			po->num_child_orders++;
			/* send child order downstream */
			event_t e;
			e.event_type = EVENT_TYPE_NEW_ORDER;
			e.event_body.new_order_event.order_h = coh;
			write_event(_q_algo_to_downstream_w, &e);
		}
		break;
		/* INTEGRATION: insert new algo types here */
	}
	return 0;
}

/*
* this function runs on algo thread;
* process child order status from downstream;
* if order was filled, update algo context;
* if parent order has been filled, then update order status
* and send notification upstream
*/
int16_t algo_process_order_status(order_handle_t coh)
{
	volatile child_order_t *co = &_child_orders->orders[coh];
	volatile parent_order_t *po = &_parent_orders->orders[co->parent_order_h];
	event_t e;
	switch (co->order.status) {
		/* INTEGRATION:
		* currently we receive only 'filled' messages;
		* expand this switch to include other message types
		*/
	case ORDER_STATUS_FILLED:
		/*
		* child order was filled; increment filled counter;
		* see if parent order is filled '
		*/
		po->filled_qty += co->order.qty;
		if (po->filled_qty == po->order.qty) {
			/* parent is filled; set order status to filled */
			po->order.status = ORDER_STATUS_FILLED;
			/* send filled status upstream for the parent order */
			e.event_type = EVENT_TYPE_ORDER_STATUS;
			e.event_body.order_status_event.order_h = co->parent_order_h;
			write_event(_q_algo_to_upstream_w, &e);
			/* unsubscribe from market data */
			e.event_type = EVENT_TYPE_MARKET_DATA_UNSUB;
			e.event_body.sub_unsub_event.security_h = po->order.security_h;
			e.event_body.sub_unsub_event.parent_order_h = co->parent_order_h;
			return write_event(_q_algo_to_market_data_w, &e);
		}
		else {
			/* send partially filled status upstream for the parent order */
			e.event_type = EVENT_TYPE_ORDER_STATUS;
			po->order.status = ORDER_STATUS_PARTIALLY_FILLED;
			e.event_body.order_status_event.order_h = co->parent_order_h;
			return write_event(_q_algo_to_upstream_w, &e);
		}
		break;
	}
	return 0;
}
