#include "../queue/queue.h"
#include "algo.h"

/*
 * wrappers around queue API:
 * take event address and pass it to queue as void*
 */
int16_t read_event(queue_handle_t q, event_t *e)
{
	return read_from_queue(q, e);
}

int16_t read_events(
	queue_handle_t queues[],
	event_t events[],
	bool queue_got_data[],
	int num_of_queues)
{
	void *payloads[MAX_NUM_HANDLES];
	for (uint16_t i = 0; i < num_of_queues; i++) {
		payloads[i] = &events[i];
	}
	return read_from_queues(
		queues,
		num_of_queues,
		(void**)payloads, queue_got_data);
}

int16_t write_event(queue_handle_t q, event_t *e)
{
	return write_to_queue(q, 0, (void*)e);
}

int16_t write_event_conflated(
	queue_handle_t q,
	event_t *e,
	uint32_t key)
{
	return write_to_queue(q, key, (void*)e);
}
