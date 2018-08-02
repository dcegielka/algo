// shortcut selector
function $(id){return document.getElementById(id);}

var Histogram = (function(){

    function Histogram(svg){
        this.svg = svg;

        var svg = d3.select(this.svg);

        var clientWidth = this.svg.clientWidth;

        var margin = {top: 20, right: 20, bottom: 30, left: 40},
        width = clientWidth - margin.left - margin.right,
        height = +svg.attr("height") - margin.top - margin.bottom;

        var x = d3.scaleBand().rangeRound([0, width]).padding(0.1),
            y = d3.scaleLinear().rangeRound([height, 0]);

        var g = svg.append("g")
            .attr("transform", "translate(" + margin.left + "," + margin.top + ")");

      //  x.domain(data.map(function(d) { return d.category; }));
      //  y.domain([0, d3.max(data, function(d) { return d.frequency; })]);

        var xAxis = g.append("g")
            .attr("class", "axis axis--x")
            .attr("transform", "translate(0," + height + ")")
            .call(d3.axisBottom(x));

        var yAxis = g.append("g")
            .attr("class", "axis axis--y");

            yAxis.call(d3.axisLeft(y))
            .append("text")
            .attr("transform", "rotate(-90)")
            .attr("y", 6)
            .attr("dy", "0.71em")
            .attr("text-anchor", "end")
            .text("Frequency");
        
            g.append("rect")
            .attr("class", "bar");


        this.yMax = 0;
        this.render = function(data){
            //recalcualte axis scale
            x.domain(data.map(function(d) { return d.category; }));
            this.yMax = Math.max(this.yMax,d3.max(data, function(d) { return d.frequency; }));
            y.domain([0,this.yMax]);

            xAxis.call(d3.axisBottom(x));
            yAxis.call(d3.axisLeft(y));

            var bars = g.selectAll(".bar")
            .data(data);

            bars.exit().remove();

            bars.enter().append("rect")
                .attr("class", "bar")
            .merge(bars)
                .attr("x", function(d) { return x(d.category); })
                .attr("width", x.bandwidth())
                .transition()
                .duration(800)
                .attr("y", function(d) { return y(d.frequency); })
                .attr("height", function(d) { return height - y(d.frequency); })
        }

    }
    return Histogram;
})();

var ValueTable = (function(){
    
    function _formatThousands(value) {
        var sign = '';

        if (value < 0) sign = '-';

        value = Math.abs(value);
        if (value < 1000) return sign + value;
        value = value + "";
        var result = "";
        for (var i = 0; i < value.length; i++) {
            if ((value.length - i) % 3 == 0 && i > 0)
                result += ',';
            result += value[i];
        }
        return sign + result; 
    }


    function ValueTable(){}

    ValueTable.prototype.render = function(data){
        var keys = Object.keys(data);
        for(var i=0; i< keys.length; i++){
            var key = keys[i]; 
            $('th-'+key).innerHTML = _formatThousands(+data[keys[i]]);
        }
    };
    return ValueTable;
})();

var algoMonitor = (function(window, document){
    // constants
    var URL_PERFORMANCE = 'http://localhost:8888/perf';
    var POLL_FREQUENCY = 2000; // milliseconds
    var HTTP_READY_STATES = {
        UNSENT:0,
        OPENED:1,
        HEADERS_RECEIVED:2,
        LOADING:3,
        DONE:4
    };
	
	// singleton transport
	var transport = new XMLHttpRequest();
    
    function httpGet(url){
        return new Promise(function(resolve, reject){
			transport.open('GET',URL_PERFORMANCE);
            transport.onload = function(){
                if(this.readyState != HTTP_READY_STATES.DONE ) return;
                try {
                    resolve(JSON.parse(transport.responseText));
                } catch(ex){
                    console.log('response parsing error');
                    reject(ex);
                }
            }
            transport.send();
        });
    }

    function transformHistorData(data){
        var newData = [];
        var keys = Object.keys(data);
        for(var i=0; i<keys.length; i++){
            newData.push({
                category: keys[i],
                frequency: data[keys[i]]
            });
        }
        return newData;
    }

    function AlgoMonitor(){
    }

    AlgoMonitor.prototype.start = function(){
        console.log('Starting algo monitor');

        //instantiate histograms
        var upstreamToAlgo_ql = new Histogram($('ql-upstream_to_algo'));
        var marketDataToAlgo_ql = new Histogram($('ql-market_data_to_algo'));
        var algoToDownStream_ql = new Histogram($('ql-algo_to_downstream'));
		
        var downstreamToAlgo_ql = new Histogram($('ql-downstream_to_algo'));
        var algoToMarket_ql = new Histogram($('ql-algo_to_market_data'));
        var algoToUpstream_ql = new Histogram($('ql-algo_to_upstream'));

        var upstreamToAlgo_tq = new Histogram($('tq-upstream_to_algo'));
        var marketDataToAlgo_tq = new Histogram($('tq-market_data_to_algo'));
        var algoToDownStream_tq = new Histogram($('tq-algo_to_downstream'));
		
        var downstreamToAlgo_tq = new Histogram($('tq-downstream_to_algo'));
        var algoToMarket_tq = new Histogram($('tq-algo_to_market_data'));
        var algoToUpstream_tq = new Histogram($('tq-algo_to_upstream'));

        var tickToTrade = new Histogram($('tick_to_trade_histogram'));

        var valueTable = new ValueTable();
		var ordersTable = new ValueTable();
        
        var pollTimer = {};
        var poll = function(){
            pollTimer.lastPoll = new Date().getTime();
            console.log('polling');
            httpGet(URL_PERFORMANCE)
                .then(function(result){
                    
                    // feed histograms
                    upstreamToAlgo_ql.render(transformHistorData(result['queue_length_histogram']['upstream_to_algo']));
                    marketDataToAlgo_ql.render(transformHistorData(result['queue_length_histogram']['market_data_to_algo']));
                    algoToDownStream_ql.render(transformHistorData(result['queue_length_histogram']['algo_to_downstream']));

                    downstreamToAlgo_ql.render(transformHistorData(result['queue_length_histogram']['downstream_to_algo']));
                    algoToMarket_ql.render(transformHistorData(result['queue_length_histogram']['algo_to_market_data']));
                    algoToUpstream_ql.render(transformHistorData(result['queue_length_histogram']['algo_to_upstream']));

                    upstreamToAlgo_tq.render(transformHistorData(result['time_in_queue_histogram']['upstream_to_algo']));
                    marketDataToAlgo_tq.render(transformHistorData(result['time_in_queue_histogram']['market_data_to_algo']));
                    algoToDownStream_tq.render(transformHistorData(result['time_in_queue_histogram']['algo_to_downstream']));

                    downstreamToAlgo_tq.render(transformHistorData(result['time_in_queue_histogram']['downstream_to_algo']));
                    algoToMarket_tq.render(transformHistorData(result['time_in_queue_histogram']['algo_to_market_data']));
                    algoToUpstream_tq.render(transformHistorData(result['time_in_queue_histogram']['algo_to_upstream']));

                    tickToTrade.render(transformHistorData(result['tick_to_trade_histogram']));
                    
                    // render value table
                    valueTable.render(result['throughput']);
					ordersTable.render(result['orders_stat']);
                    
                    //refresh
                    setTimeout(poll,POLL_FREQUENCY);
                }).catch(function(ex){
                    console.log(ex);
                    setTimeout(poll,POLL_FREQUENCY);
                });
        };
        poll();

        var check = function(){
            var now = new Date().getTime();
            var lapse = now - pollTimer.lastPoll;
            if(lapse > 4000) {
                console.log('Poll lapsed for: ', lapse,' ms, restarting');    
                setTimeout(function(){poll()},1);    
            }
            
            setTimeout(check,POLL_FREQUENCY+1000);
        }
        check();
    };
    return new AlgoMonitor();
})(window, document);
