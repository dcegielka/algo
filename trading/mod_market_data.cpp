#include "../queue/queue.h"
#include "algo.h"

/*
 * thread that connects to market data source,
 * subscribes to set of symbols that match
 * currently active parent orders and sends market data to algo
 */
DWORD WINAPI market_data_incoming_thread(LPVOID par)
{
	int16_t r;
	market_data_incoming_thread_is_running = true;
	/* this is performance—critical thread, pin it to core */
	pin_thread_to_core(1);
	while (1) {
		event_t e;
		e.event_type = EVENT_TYPE_MARKET_DATA;
		/*
		 * INTEGRATION: listen to market data from the feed and
		 * send it to algo
		 */
		for (security_handle_t seh = 0; seh < _symbols->num_symbols; seh++) {
			if (-1 == _sec_subscriptions->_security_subscriptions_root[seh])
				continue; /* no subscribers for this security */
			e.event_body.mkt_data_event.security_h = seh;
			e.event_body.mkt_data_event.market_data_type =
				MARKET_DATA_TYPE_TRADE;
			/* DEMO: generate random market data */
			generate_market_data(&e, seh);
			/* start of the tick-to-trade perfromance tracker */
			LARGE_INTEGER tick_time;
			QueryPerformanceCounter(&tick_time);
			e.event_body.mkt_data_event.tick_timestamp = tick_time.QuadPart;
			/*
  			 * send market data event to algo: note that
 			 * we write to coflated queue with key=security handle
			 */
			r = write_event_conflated(
				_q_market_data_to_algo_w,
				&e,
				seh);
			if (ERR_QUEUE_INTERRUPTED == r)
				break;
		}
		if (is_queue_interrupted(_q_market_data_to_algo_w))
			break;
	}
	market_data_incoming_thread_is_running = false;
	return 0;
}
/*
 * thead to keep linked list of parent orders for each security;
 * when new parent order arrives, we insert a new node;
 * when parent order is filled, we remove the node
 */
DWORD WINAPI market_data_sub_unsub_thread(LPVOID par)
{
	market_data_sub_unsub_thread_is_running = true;
	event_t e;
	while (1) {
		/* read sub/unsub events from algo */
		int16_t r = read_event(_q_algo_to_market_data_r, &e);
		if (ERR_QUEUE_INTERRUPTED == r)
			break;
		security_handle_t seh =
			e.event_body.sub_unsub_event.security_h;
		order_handle_t poh =
			e.event_body.sub_unsub_event.parent_order_h;
		/* this needs to be in critical section, to prevent algo from reading
		 * partially written data (sub/unsum maintain linked list of
		 * subscribers and we cannot let other thread to see inconsistent data);
		 */
		lock(&_sec_subscriptions->_security_subscriptions_write_lock[seh]);
		switch (e.event_type)
		{
		case EVENT_TYPE_MARKET_DATA_SUB:
			market_data_sub(poh, seh);
			break;
		case EVENT_TYPE_MARKET_DATA_UNSUB:
			market_data_unsub(poh, seh);
			break;
		}
		unlock(&_sec_subscriptions->_security_subscriptions_write_lock[seh]);
		/*
		 * INTEGRATION: send sub/unsub command to external data feed
		 * ...
		 */
	}
	market_data_sub_unsub_thread_is_running = false;
	return 0;
}

/*
* create mock market data for given security and copy it to event
*/
int16_t generate_market_data(
	event_t *e,
	security_handle_t seh)
{
	e->event_body.mkt_data_event.price = 123;
	e->event_body.mkt_data_event.qty = 100;
	e->event_body.mkt_data_event.side = SIDE_BUY;
	e->event_body.mkt_data_event.security_h = seh;
	return 0;
}

int16_t market_data_sub(order_handle_t poh, security_handle_t seh)
{
	if (-1 == _sec_subscriptions->_security_subscriptions_root[seh]) {
		/* there are no subscriptions for this security yet;
		* set this poh as a root and point it at -1 (end of list)
		*/
		_sec_subscriptions->_security_subscriptions_root[seh] = poh;
		_sec_subscriptions->_security_subscriptions[seh][poh] = -1;
	}
	else {
		order_handle_t root_poh = _sec_subscriptions->_security_subscriptions_root[seh];
		if (poh < root_poh) {
			/*
			inserting at the beginning of the list:
			root poh=2
			|
			v
			| | |4| |6| |-|
			|O|1|2|3|4|5|6|7

			insert poh subscriber 1:
			root poh=l
			|
			v
			| |2|4| |6| |-|
			|O|1|2|3|4|5|6|7
			*/
			_sec_subscriptions->_security_subscriptions_root[seh] = poh;
			_sec_subscriptions->_security_subscriptions[seh][poh] = root_poh;
		}
		else {
			/*
			inserting in the middle of the list:
			root poh=2
			| | |4| |6| |-|
			|0|1|2|3|4|5|6|7

			insert poh subscriber 3:
			root poh=2
			| | |3|4|6| |-|
			|0|1|2|3|4|5|6|7
			*/
			order_handle_t oh = root_poh;
			bool found = false;
			while (-1 != _sec_subscriptions->_security_subscriptions[seh][oh]) {
				if (poh < (order_handle_t)_sec_subscriptions->_security_subscriptions[seh][oh]) {
					order_handle_t next_oh =
						(order_handle_t)_sec_subscriptions->_security_subscriptions[seh][oh];
					_sec_subscriptions->_security_subscriptions[seh][oh] = poh;
					_sec_subscriptions->_security_subscriptions[seh][poh] = next_oh;
					found = true;
					break;
				}
				oh = _sec_subscriptions->_security_subscriptions[seh][oh]; ;
			}
			if (!found) {
				/*
				inserting at the end of the list
				root poh=2
				| | |4| |6| |-|
				|O|1|2|3l4|5|6|7

				insert poh subscriber 7:
				root poh=2
				| | |3|4|6| |7|-
				|0|1|2|3|4|5|6|7
				*/
				_sec_subscriptions->_security_subscriptions[seh][oh] = poh;
				_sec_subscriptions->_security_subscriptions[seh][poh] = -1;
			}
		}
	}
	return 0;
}

int16_t market_data_unsub(order_handle_t poh, security_handle_t seh)
{
	order_handle_t root_poh = _sec_subscriptions->_security_subscriptions_root[seh];
	if (poh == root_poh) {
		/*
		removing element from the beginning of the list:
		root poh=2
		| | |4| |6| |-|
		[O|l|2|3|4|5|6|7

		after removing poh 2:
		root poh=4
		| | | | |6| |-|
		|O|1|2|3|4|5|6|7
		*/
		order_handle_t next_poh = _sec_subscriptions->_security_subscriptions[seh][poh];
		_sec_subscriptions->_security_subscriptions_root[seh] = next_poh;
		_sec_subscriptions->_security_subscriptions[seh][poh] = 0;
	}
	else {
		/*
		removing element from the middle of the list:
		root poh=2
		| |2|3|4|6| |—|
		|O|l|2|3|4|5|6|7

		after removing poh 3:
		root poh=2
		| | |4| |6| |-|
		|O|1|2|3|4|5|6|7
		*/
		order_handle_t next_poh = root_poh;
		bool found = false;
		order_handle_t prev_poh = next_poh;
		while (-1 != (int)_sec_subscriptions->_security_subscriptions[seh][next_poh]) {
			if (poh == (order_handle_t)_sec_subscriptions->_security_subscriptions[seh][next_poh]) {
				_sec_subscriptions->_security_subscriptions[seh][next_poh] =
					_sec_subscriptions->_security_subscriptions[seh][poh];
				MemoryBarrier();
				found = true;
				break;
			}
			prev_poh = next_poh;
			next_poh = _sec_subscriptions->_security_subscriptions[seh][next_poh];
		}
		if (!found) {
			/*
			removing element from the end of the list:
			root poh=2
			| | |3|4|6| |7|-
			|O|1|2|3|4|5|6|7

			after removing poh 7:
			root poh=2
			| | |3|4|6| |-|
			|0|1|2|3|4|5|6|7
			*/
			_sec_subscriptions->_security_subscriptions[seh][prev_poh] = -1;
			MemoryBarrier();
		}
	}
	return 0;
}
