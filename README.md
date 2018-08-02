# WORK IN PROGRESS!!!

# HOW TO CREATE AN ALGO TRADING SYSTEM IN UNDER 2,000 LINES OF CODE

# What is Algorithmic Trading? 
Algo trading is a process of automatically generating trade orders, sending them to execution venues, keeping track of their execution statuses, canceling orders, generating reports etc. Typical exemple of an algo trading system is order execution, when large parent orders are sliced into smaller child orders in order to minimize the market impact. Such algo trading systems make money on comissions for executing client orders. Another example of algo trading is market making, which makes money on spread. There are also algo trading systems that generate P/L by trying to predict market and take advantage of price dislocations, for example pair trading.

All of the above algo trading systems share common structure, which we will illustrate using algo execution system as an example:
http://asciiflow.com/

```
+---------+      +----------+             +--------------+        +------------+      +-----------+
|         +------>          +------------>+              +-------->            <------+           |
| FIX IN  |      | UPSTREAM |             |  STRATEGIES  |        | DOWNSTREAM |      |  FIX OUT  |
|         <------+          <-------------+              <--------+            +------>           |
+---------+      +----------+             +----+---^-----+        +------------+      +-----------+
                                               |   |
                                               |   |
                                               |   |
                                           +---v---+---+
                                           |           |
                                           |  MARKET   |
                                           |  DATA     |
                                           |           |
                                           +----+--^---+
                                                |  |
                                                |  |
                                              +-v--+------------+
                                              |                 |
                                              |  Data Feed(s)   |
                                              |                 |
                                              +-----------------+
```

The  building blocks of an algo trading system are:

* UPSTREAM: reading parent orders, for example from FIX gateway
* DOWNSTREAM: send child orders for execution, for example, to FIX gateway
* MARKET DATE: source of market data
* STRATEGIES: algo trading strategies that 

Typical event flow:

* parent orders arrive upstream from incoming FIX gateway
* a particular execution strategy is activated; the strategy
	* subscribes to necessary market data feeds
	* breaks parent order down into smaller child orders
	* sends child orders downstream for execution to a outgoing FIX gateway
	* monitors child orders statuses and adjusts limit orders if necessary
* when parent order is filled (i.e. all child orders have been filled), strategy unsubscribes from market data feeds and is deactivated

OR 

- upstream 
	- recieves incoming 'new' and 'cancel' parent orders from upstream (e.g. FIX) and passes them down to algo for execution
- algo: algorithmic trading strategies
	- reades market data
	- reads incoming parent orders from upstream
	- slices parent orders and sends child orders downstream for execution
	- sends parent order notifications statues upstream (partial fill, fill, reject etc)
- market data: 
	- feeds algo with trades and quotes
	- manages symbol subscriptions for parent orders
- downstream: has access to execution venues (e.g. FIX)
	- executes child orders
	- sends order status notifications to algo, so it can decide when parent order is filled

# Our approach to building an algo trading system
The main challenge is performance. System has to process incoming market data and generate outgoing orders as quick as possible in order to be first in line to place order at the exchange, in order to get most favourable price. "Tick-to-trade" measures this type of latency - how quickly the system reacts to the market data. Minimizing tick-to-trade value and its variance is our primary goal. Chosing non-GC language eliminated one source of latency - the unpredictable GC. Of course, we could have still gone with GC language and pre-allocate our data to reduce or completely eliminate allocations, but then it becomes unclear why chose GC language in first place. We chose C as its code is closest to the wire. We still pre-allocate in order to have data stored in nice contiguous memory blocks, which makes data traversal simple and cache-friendly. As a bonus, there are no memory leaks. Any modern mobile phone has enough memory to run a reasonably well written algo trading system - following configuration requires about 130MB of memory:

* 1,000 parent orders x 1,000 child orders per each parent order (total 1,000,000 orders)

If we scale it up to 10,000x10,000 orders, this will take about 13GB - a fraction of memory on modern servers, especially ones that are runnig algo trading code. 

In order to achieve low tick-to-trade latency numbers we have to parallelize the code, i.e. to load available cores and minimize the context switches. Pinning performance-critical threads to dedicated (isolated) cores and eliminating system calls on the hot path should keep the context switches to the minimum. 

Our tick-to-trade latency is:

* 98%: 1-16 microseconds,
* 2%: 16-32 microseconds

for a few hundred active parent orders.

The next big question is - how threads will communicate and how they will access shared data structures? Standard way of inter-thread communication is to establish a message queue. Standard solutions from the Java world - Disruptor and Chronicle Queue. Beside the fact that they are Java, there are reasons why they don't work for us:

* Disruptor is ring-buffer based (which is what we want) but it is intra-proc only (threads from two processes can not talk to each other).
* Chronicle queue supports inter-process communication via shared memory (which is what we want), but its queue is unbounded. Chronicle Queue relies on the OS swapping capabilities to maintain an "unbounded" memory area.
* Neither solution provides support for conflated queue - queue where data can be overwritten if reader is too slow. When client writes to conflated queue, it checks if this element is already sitting in the queue (but hasn't been consumed by reader yet). If there is such element, it is simply updated. If there is no such element, then new element is added to the queue. Typical example is market data - if algo can't keep up with market data, there is no reason to feed it with stale quotes. If quote or trade in the queue has not been consumed by algo, then we simply update it with new numbers.

We have implemented our own queue, which

* allows two processes to communicate,
* is based on ring-buffer,
* supports conflated mode,
* fixed size event (this  eliminates great deal of complexity),
* is non-blocking.

There are various flavors of queues - single writer, multiple readers etc. Our core implementation contains "Single Writer / Single Reader" queue. When we have to add more writers (as in case of Strategy thread, which has three writers - incoming orders, market data and execution statuses, and sigle reader), we can implement it with three separate queues, that are polled by strategy thread. This allows us to keep core implementation simple and push complexity to higher level as needed.

Events (e.g. new order event, market data event etc) are defined as C structures. There is an umbrella event_t structure that contains a union representing all possible types of events and an enum indicating the type of event. Reading data from the queue consists of reading a byte array from the queue slot and casting it to event_t. Then consumer does switch based on event type and pulls particular event data from the union. Serialization/deserialization is as simple as:

```
event_t event;  // event structure is allocated on stack
read_from_queue(queue, &event);  // pass address of event to queue reader
// based on event_type switch to desired union member
switch (event.event_type) {
case EVENT_TYPE_NEW_ORDER:
	// this event is of new_order_event_t type
	new_order_event_t new_order_event = event.event_body.new_order_event;
	...
```

Doing it in Java or any other high level language requires considerably more effort, since object . 


try to do this in Java!:
	data serialization in queues - cast.
	fixed size event - union. 

# Summary of design principles

- no dynamic memory allocations; all data structures are pre-allocated
- when we need to keep data in lists, e.g. list of parent order subscribed to market data, we use preallocated linked lists (x[i] points to j; x[j] points to k etc)
- performance critical threads are bound to an isolated CPU to eliminate context switches and keep data in caches
- data is stored in arrays of structures; code uses handles to identify data - handle is simply an index in the array
- data is aligned along cache line boundaries to eliminate false sharing
- threads exchange short messages, e.g. handles and statuses
- when threads should synchronize on accessing common data structure, they use busy loop and CAS (Complare And Swap) to lock the data

# scaling
each algo engine is reading incoming parent orders from upstream, e.g. FIX gateway; in order to scale the system, we can 
- start a pool of algo engines, each listening for upstream orders on separate port
- use round robin or similar technique to dispatch incoming client orders to algo engines
- this provides:
	- better core utilization; there are few performance-critical threads in each algo process that need to be bound to cores; therefore on a box with many cores (e.g. 24) we can run several algo processes
	- ability to distribute load across machines


---

# data structures

- array of parent orders
- array of child orders
- array of symbols (security master)
- array of securities pointing to linked lists of parent orders, that are subscribed to this security
- each child order references its parent order
- each parent order references an array of its child orders

# code

read_from_queue() and write_to_queue() code sample

Reader can read from more than one queue - read_from_queues() function takes an array of queues at input parameter and returns whenever data becomes available in one or more queues.

naming conventions
files

# Where do we go from here?

tooling - need out of the box instrumentation to measure and monitor performance stats
different data feeds, execution venues
scaling (running multiple algo workers)
give an idea what will it take to transform reference implementation into working solution
