#include <bits/stdc++.h>
#include <chrono>
#include <thread>
#include <mutex>
using namespace std;

long long current_time(){
    auto now=chrono::system_clock::now();
    auto duration=now.time_since_epoch();
    return chrono::duration_cast<chrono::microseconds>(duration).count();
}

struct request{
    int order_id;
    string type;
    string stock;
    int quantity;
    float price;
    long long time;
    int demat_id;
};

struct trade{
    long long trade_id;
    int buy_order_id;
    int sell_order_id;
    int buy_demat_id;
    int sell_demat_id;
    string stock;
    int quantity;
    float price;
    long long time;
};

struct benchmark_record{
    int order_id;
    int demat_id;
    long long latency;
};

class account{
private:
    unordered_map<string,int>stocks;
    float cash;
    int demat_id;
public:
    account(float cash=0,int demat_id=0){
        this->cash=cash;
        this->demat_id=demat_id;
    }
    void add_money(float money){
        cash+=money;
    }
    void deduct_money(float money){
        cash=max(0.0f,cash-money);
    }
    void add_stocks(string stock,int quantity){
        stocks[stock]+=quantity;
    }
    void remove_stocks(string stock,int quantity){
        stocks[stock]-=max(0,quantity);
    }
    float get_cash() const {
        return cash;
    }
    int get_stock_quantity(const string& s) const {
        auto it=stocks.find(s);
        return it!=stocks.end()?it->second:0;
    }
    int get_demat_id() const {
        return demat_id;
    }
};

struct buy_compare{
    bool operator()(const shared_ptr<request>& a,const shared_ptr<request>& b) const{
        if(a->price==b->price){
            if(a->time==b->time) return a->order_id>b->order_id;
            return a->time>b->time;
        }
        return a->price<b->price;
    }
};

struct sell_compare{
    bool operator()(const shared_ptr<request>& a,const shared_ptr<request>& b) const{
        if(a->price==b->price){
            if(a->time==b->time) return a->order_id>b->order_id;
            return a->time>b->time;
        }
        return a->price>b->price;
    }
};

class engine{
private:
    map<int,shared_ptr<request>>req;
    map<string,priority_queue<shared_ptr<request>,vector<shared_ptr<request>>,buy_compare>>buy;
    map<string,priority_queue<shared_ptr<request>,vector<shared_ptr<request>>,sell_compare>>sell;
    map<int,account>accounts;
    vector<trade>trade_book;
    vector<benchmark_record>benchmark_records;
    long long trade_id=1;
    mutex mtx;
    request empty_request(){
        return {-1,"","",0,0.0f,0,0};
    }
    shared_ptr<request> best_bid_locked(string stock){
        if(!buy.count(stock)) return nullptr;
        auto &pq=buy[stock];
        while(!pq.empty()){
            auto top_val=pq.top();
            auto it=req.find(top_val->order_id);
            if(it==req.end()||it->second.get()!=top_val.get()||top_val->quantity==0){
                pq.pop();
                continue;
            }
            return top_val;
        }
        return nullptr;
    }
    shared_ptr<request> best_ask_locked(string stock){
        if(!sell.count(stock)) return nullptr;
        auto &pq=sell[stock];
        while(!pq.empty()){
            auto top_val=pq.top();
            auto it=req.find(top_val->order_id);
            if(it==req.end()||it->second.get()!=top_val.get()||top_val->quantity==0){
                pq.pop();
                continue;
            }
            return top_val;
        }
        return nullptr;
    }
    void add_trade(int buy_order_id,int sell_order_id,int buy_demat_id,int sell_demat_id,string stock,int quantity,float price){
        trade t;
        t.trade_id=trade_id++;
        t.buy_order_id=buy_order_id;
        t.sell_order_id=sell_order_id;
        t.buy_demat_id=buy_demat_id;
        t.sell_demat_id=sell_demat_id;
        t.stock=stock;
        t.quantity=quantity;
        t.price=price;
        t.time=current_time();
        trade_book.push_back(t);
    }
    void add_benchmark(int order_id,int demat_id,chrono::steady_clock::time_point start){
        auto end=chrono::steady_clock::now();
        long long latency=chrono::duration_cast<chrono::nanoseconds>(end-start).count();
        benchmark_records.push_back({order_id,demat_id,latency});
    }
public:
    void add_accounts(account a){
        lock_guard<mutex>lock(mtx);
        accounts[a.get_demat_id()]=a;
    }
    void add_stocks(string s){
        lock_guard<mutex>lock(mtx);
        buy[s];
        sell[s];
    }
    request best_bid(string stock){
        lock_guard<mutex>lock(mtx);
        auto r=best_bid_locked(stock);
        return r?*r:empty_request();
    }
    request best_ask(string stock){
        lock_guard<mutex>lock(mtx);
        auto r=best_ask_locked(stock);
        return r?*r:empty_request();
    }
    void add_buy_order(request r){
        auto start=chrono::steady_clock::now();
        lock_guard<mutex>lock(mtx);
        account &a=accounts[r.demat_id];
        if(a.get_cash()<1.0f*r.quantity*r.price){
            add_benchmark(r.order_id,r.demat_id,start);
            return;
        }
        a.deduct_money(1.0f*r.quantity*r.price);
        float mon=1.0f*r.quantity*r.price;
        while(true){
            auto temp=best_ask_locked(r.stock);
            if(!temp||r.price<temp->price) break;
            int traded=min(temp->quantity,r.quantity);
            a.add_stocks(r.stock,traded);
            account &b=accounts[temp->demat_id];
            b.add_money(traded*temp->price);
            mon-=traded*temp->price;
            add_trade(r.order_id,temp->order_id,r.demat_id,temp->demat_id,r.stock,traded,temp->price);
            temp->quantity-=traded;
            r.quantity-=traded;
            if(temp->quantity==0){
                sell[r.stock].pop();
                req.erase(temp->order_id);
            }
            if(r.quantity==0) break;
        }
        float curr_money=1.0f*r.quantity*r.price;
        a.add_money(mon-curr_money);
        if(r.quantity==0){
            add_benchmark(r.order_id,r.demat_id,start);
            return;
        }
        auto order=make_shared<request>(r);
        req[r.order_id]=order;
        buy[r.stock].push(order);
        add_benchmark(r.order_id,r.demat_id,start);
    }
    void add_sell_order(request r){
        auto start=chrono::steady_clock::now();
        lock_guard<mutex>lock(mtx);
        account &a=accounts[r.demat_id];
        if(a.get_stock_quantity(r.stock)<r.quantity){
            add_benchmark(r.order_id,r.demat_id,start);
            return;
        }
        a.remove_stocks(r.stock,r.quantity);
        while(true){
            auto temp=best_bid_locked(r.stock);
            if(!temp||r.price>temp->price) break;
            int traded=min(temp->quantity,r.quantity);
            a.add_money(temp->price*traded);
            account &b=accounts[temp->demat_id];
            b.add_stocks(r.stock,traded);
            add_trade(temp->order_id,r.order_id,temp->demat_id,r.demat_id,r.stock,traded,temp->price);
            temp->quantity-=traded;
            r.quantity-=traded;
            if(temp->quantity==0){
                buy[r.stock].pop();
                req.erase(temp->order_id);
            }
            if(r.quantity==0) break;
        }
        if(r.quantity==0){
            add_benchmark(r.order_id,r.demat_id,start);
            return;
        }
        auto order=make_shared<request>(r);
        req[r.order_id]=order;
        sell[r.stock].push(order);
        add_benchmark(r.order_id,r.demat_id,start);
    }
    void cancel_order(request r){
        lock_guard<mutex>lock(mtx);
        auto it=req.find(r.order_id);
        if(it==req.end()) return;
        auto order=it->second;
        account &a=accounts[order->demat_id];
        if(order->type=="SELL") a.add_stocks(order->stock,order->quantity);
        else a.add_money(1.0f*order->quantity*order->price);
        req.erase(it);
    }
    void modify_order(vector<string>params,vector<string>values){
        lock_guard<mutex>lock(mtx);
        if(params.size()!=values.size()||params.empty()) return;
        int order_id=-1;
        for(size_t i=0;i<params.size();i++){
            if(params[i]=="order_id"){
                order_id=stoi(values[i]);
                break;
            }
        }
        if(order_id==-1||!req.count(order_id)) return;
        auto old_order=req[order_id];
        auto order=make_shared<request>(*old_order);
        for(size_t i=0;i<params.size();i++){
            if(params[i]=="type") order->type=values[i];
            else if(params[i]=="stock") order->stock=values[i];
            else if(params[i]=="quantity") order->quantity=stoi(values[i]);
            else if(params[i]=="price") order->price=stof(values[i]);
            else if(params[i]=="demat_id") order->demat_id=stoi(values[i]);
        }
        order->time=current_time();
        req[order_id]=order;
        if(order->type=="BUY") buy[order->stock].push(order);
        else if(order->type=="SELL") sell[order->stock].push(order);
    }
    long long get_trade_count(){
        lock_guard<mutex>lock(mtx);
        return trade_book.size();
    }
    void print_trade_book(){
        lock_guard<mutex>lock(mtx);
        cout<<"TRADE BOOK"<<'\n';
        for(auto &t:trade_book){
            cout<<t.trade_id<<" "<<t.stock<<" "<<t.quantity<<" @ "<<t.price<<'\n';
        }
        cout<<"TOTAL TRADES: "<<trade_book.size()<<'\n';
    }
    void print_benchmark_records(){
        lock_guard<mutex>lock(mtx);
        cout<<"BENCHMARK RECORDS"<<'\n';
        for(auto &b:benchmark_records){
            cout<<b.order_id<<" "<<b.latency/1000.0<<" us"<<'\n';
        }
    }
    void print_benchmarks(long long total_time){
        lock_guard<mutex>lock(mtx);
        if(benchmark_records.empty()) return;
        vector<long long>latencies;
        long long total_latency=0;
        for(auto &b:benchmark_records){
            latencies.push_back(b.latency);
            total_latency+=b.latency;
        }
        sort(latencies.begin(),latencies.end());
        double seconds=total_time/1000000.0;
        double throughput=latencies.size()/seconds;
        double average=1.0*total_latency/latencies.size()/1000.0;
        long long p50=latencies[(size_t)(0.50*(latencies.size()-1))];
        long long p95=latencies[(size_t)(0.95*(latencies.size()-1))];
        long long p99=latencies[(size_t)(0.99*(latencies.size()-1))];
        cout<<"BENCHMARKS"<<'\n';
        cout<<"ORDERS: "<<latencies.size()<<'\n';
        cout<<"TOTAL TIME: "<<seconds<<" s"<<'\n';
        cout<<"THROUGHPUT: "<<throughput<<" orders/s"<<'\n';
        cout<<"AVERAGE LATENCY: "<<average<<" us"<<'\n';
        cout<<"P50: "<<p50/1000.0<<" us"<<'\n';
        cout<<"P95: "<<p95/1000.0<<" us"<<'\n';
        cout<<"P99: "<<p99/1000.0<<" us"<<'\n';
    }
    void print_algo_benchmarks(int demat_id,float initial_cash,vector<string>&stocks,long long runtime){
        lock_guard<mutex>lock(mtx);
        vector<long long>latencies;
        long long buy_qty=0,sell_qty=0;
        double buy_value=0,sell_value=0;
        long long trade_count=0;
        for(auto &b:benchmark_records){
            if(b.demat_id==demat_id) latencies.push_back(b.latency);
        }
        vector<float>best_prices(stocks.size(),0.0f);
        vector<int>quantities(stocks.size(),0);
        double initial_inventory_value=0,final_inventory_value=0;
        double initial_market_value=0,final_market_value=0;
        for(auto &t:trade_book){
            if(t.buy_demat_id==demat_id){
                buy_qty+=t.quantity;
                buy_value+=1.0*t.quantity*t.price;
                trade_count++;
            }
            if(t.sell_demat_id==demat_id){
                sell_qty+=t.quantity;
                sell_value+=1.0*t.quantity*t.price;
                trade_count++;
            }
        }
        for(int i=0;i<stocks.size();i++){
            quantities[i]=accounts[demat_id].get_stock_quantity(stocks[i]);
        int total_market_quantity=0;
        for(auto &a:accounts) total_market_quantity+=a.second.get_stock_quantity(stocks[i]);
        initial_market_value+=1.0*total_market_quantity*100.0;
            auto bid=best_bid_locked(stocks[i]);
            if(bid) best_prices[i]=bid->price;
            else{
                auto ask=best_ask_locked(stocks[i]);
                if(ask) best_prices[i]=ask->price;
                else{
                    for(auto it=trade_book.rbegin();it!=trade_book.rend();it++){
                        if(it->stock==stocks[i]){
                            best_prices[i]=it->price;
                            break;
                        }
                    }
                }
            }
            initial_inventory_value+=1000.0*100.0;
            final_inventory_value+=1.0*quantities[i]*best_prices[i];
            final_market_value+=1.0*total_market_quantity*best_prices[i];
        }
        sort(latencies.begin(),latencies.end());
        double realized_pnl=sell_value-buy_value;
        double starting_portfolio=initial_cash+initial_inventory_value;
        double ending_cash=accounts[demat_id].get_cash();
        double ending_portfolio=ending_cash+final_inventory_value;
        double total_pnl=ending_portfolio-starting_portfolio;
        double return_pct=starting_portfolio>0?100.0*total_pnl/starting_portfolio:0;
        double seconds=runtime/1000000.0;
        double throughput=seconds>0?latencies.size()/seconds:0;
        double average=latencies.empty()?0.0:1.0*accumulate(latencies.begin(),latencies.end(),0LL)/latencies.size()/1000.0;
        cout<<fixed<<setprecision(2);
        cout<<"OUR ALGO BENCHMARKS"<<'\n';
        cout<<"ORDERS: "<<latencies.size()<<'\n';
        cout<<"TRADES: "<<trade_count<<'\n';
        cout<<"BUY QUANTITY: "<<buy_qty<<'\n';
        cout<<"SELL QUANTITY: "<<sell_qty<<'\n';
        cout<<"BUY VALUE: "<<buy_value<<'\n';
        cout<<"SELL VALUE: "<<sell_value<<'\n';
        cout<<"TRADE CASH FLOW P&L: "<<realized_pnl<<'\n';
        cout<<"STARTING CASH: "<<initial_cash<<'\n';
        cout<<"ENDING CASH: "<<ending_cash<<'\n';
        cout<<"INVENTORY"<<'\n';
        for(int i=0;i<stocks.size();i++){
            cout<<stocks[i]<<": "<<quantities[i]<<" @ MARK PRICE "<<best_prices[i]<<'\n';
        }
        cout<<"INITIAL INVENTORY VALUE: "<<initial_inventory_value<<'\n';
        cout<<"FINAL INVENTORY VALUE: "<<final_inventory_value<<'\n';
        cout<<"STARTING PORTFOLIO VALUE: "<<starting_portfolio<<'\n';
        cout<<"FINAL PORTFOLIO VALUE: "<<ending_portfolio<<'\n';
        cout<<"INITIAL MARKET VALUE: "<<initial_market_value<<'\n';
        cout<<"FINAL MARKET VALUE: "<<final_market_value<<'\n';
        double market_value_change=final_market_value-initial_market_value;
        double market_return=initial_market_value>0?100.0*market_value_change/initial_market_value:0;
        cout<<"MARKET VALUE CHANGE: "<<market_value_change<<'\n';
        cout<<"MARKET RETURN: "<<market_return<<" %"<<'\n';
        cout<<"TOTAL P&L: "<<total_pnl<<'\n';
        cout<<"RETURN: "<<return_pct<<" %"<<'\n';
        cout<<"RUNTIME: "<<seconds<<" s"<<'\n';
        cout<<"THROUGHPUT: "<<throughput<<" orders/s"<<'\n';
        cout<<"AVERAGE LATENCY: "<<average<<" us"<<'\n';
        if(!latencies.empty()){
            cout<<"P50: "<<latencies[(size_t)(0.50*(latencies.size()-1))]/1000.0<<" us"<<'\n';
            cout<<"P95: "<<latencies[(size_t)(0.95*(latencies.size()-1))]/1000.0<<" us"<<'\n';
            cout<<"P99: "<<latencies[(size_t)(0.99*(latencies.size()-1))]/1000.0<<" us"<<'\n';
        }
    }

    void print_hft_benchmarks(long long total_time){
        lock_guard<mutex>lock(mtx);
        if(benchmark_records.empty()) return;
        vector<long long>latencies;
        long long total_latency=0;
        for(auto &b:benchmark_records){
            latencies.push_back(b.latency);
            total_latency+=b.latency;
        }
        sort(latencies.begin(),latencies.end());
        double seconds=total_time/1000000.0;
        double throughput=latencies.size()/seconds;
        double average=1.0*total_latency/latencies.size();
        long long p50=latencies[(size_t)(0.50*(latencies.size()-1))];
        long long p95=latencies[(size_t)(0.95*(latencies.size()-1))];
        long long p99=latencies[(size_t)(0.99*(latencies.size()-1))];
        long long p999=latencies[(size_t)(0.999*(latencies.size()-1))];
        cout<<"HFT STRESS BENCHMARK"<<'\n';
        cout<<"ORDERS: "<<latencies.size()<<'\n';
        cout<<"TOTAL TIME: "<<seconds<<" s"<<'\n';
        cout<<"THROUGHPUT: "<<throughput<<" orders/s"<<'\n';
        cout<<"AVERAGE LATENCY: "<<average<<" ns ("<<average/1000.0<<" us)"<<'\n';
        cout<<"P50: "<<p50<<" ns ("<<p50/1000.0<<" us)"<<'\n';
        cout<<"P95: "<<p95<<" ns ("<<p95/1000.0<<" us)"<<'\n';
        cout<<"P99: "<<p99<<" ns ("<<p99/1000.0<<" us)"<<'\n';
        cout<<"P99.9: "<<p999<<" ns ("<<p999/1000.0<<" us)"<<'\n';
    }

};


void hft_benchmark(){
    engine e;
    account buyer(1000000000,101);
    account seller(1000000000,102);
    buyer.add_stocks("REL",10000000);
    seller.add_stocks("REL",10000000);
    e.add_accounts(buyer);
    e.add_accounts(seller);
    e.add_stocks("REL");
    const int orders=1000000;
    auto start=chrono::steady_clock::now();
    for(int i=1;i<=orders;i++){
        request r;
        r.order_id=i;
        r.stock="REL";
        r.quantity=1;
        r.price=150.0f;
        r.time=current_time();
        if(i&1){
            r.type="SELL";
            r.demat_id=102;
            e.add_sell_order(r);
        }
        else{
            r.type="BUY";
            r.demat_id=101;
            e.add_buy_order(r);
        }
    }
    auto end=chrono::steady_clock::now();
    long long total_time=chrono::duration_cast<chrono::microseconds>(end-start).count();
    e.print_hft_benchmarks(total_time);
}

void market_sim(engine &e,vector<account>&acc,vector<string>&st){
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<int>account_dist(0,acc.size()-1);
    uniform_int_distribution<int>stock_dist(0,st.size()-1);
    uniform_int_distribution<int>type_dist(0,1);
    uniform_int_distribution<int>quantity_dist(1,50);
    uniform_real_distribution<float>move_dist(0.0f,2.0f);
    uniform_int_distribution<int>wait_dist(5,25);
    map<string,float>initial_price;
    map<string,bool>traded;
    for(auto &stock:st){
        initial_price[stock]=100.0f;
        traded[stock]=false;
    }
    int order_id=1;
    for(int i=0;i<500;i++){
        this_thread::sleep_for(chrono::milliseconds(wait_dist(gen)));
        int account_id=account_dist(gen);
        int stock_id=stock_dist(gen);
        int type_id=type_dist(gen);
        string stock=st[stock_id];
        request bid=e.best_bid(stock);
        request ask=e.best_ask(stock);
        request r;
        r.order_id=order_id++;
        r.type=type_id==0?"BUY":"SELL";
        r.stock=stock;
        r.quantity=quantity_dist(gen);
        r.time=current_time();
        r.demat_id=acc[account_id].get_demat_id();
        float move=move_dist(gen);
        if(!traded[stock]) r.price=initial_price[stock];
        else if(r.type=="BUY"){
            if(ask.order_id!=-1) r.price=ask.price+move;
            else if(bid.order_id!=-1) r.price=bid.price+move;
            else r.price=initial_price[stock];
        }
        else{
            if(bid.order_id!=-1) r.price=max(0.01f,bid.price-move);
            else if(ask.order_id!=-1) r.price=max(0.01f,ask.price-move);
            else r.price=initial_price[stock];
        }
        r.price=round(r.price*100.0f)/100.0f;
        long long before=e.get_trade_count();
        if(r.type=="BUY") e.add_buy_order(r);
        else e.add_sell_order(r);
        if(e.get_trade_count()>before) traded[stock]=true;
    }
}

void trading_algo(engine &e,int demat_id,vector<string>&st){
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<int>stock_dist(0,st.size()-1);
    uniform_int_distribution<int>quantity_dist(1,20);
    uniform_int_distribution<int>wait_dist(5,20);
    int order_id=1000001;
    for(int i=0;i<200;i++){
        this_thread::sleep_for(chrono::milliseconds(wait_dist(gen)));
        string stock=st[stock_dist(gen)];
        request bid=e.best_bid(stock);
        request ask=e.best_ask(stock);
        if(bid.order_id==-1&&ask.order_id==-1) continue;
        request r;
        r.order_id=order_id++;
        r.stock=stock;
        r.quantity=quantity_dist(gen);
        r.time=current_time();
        r.demat_id=demat_id;
        if(bid.order_id==-1){
            r.type="BUY";
            r.price=ask.price;
            e.add_buy_order(r);
        }
        else if(ask.order_id==-1){
            r.type="SELL";
            r.price=bid.price;
            e.add_sell_order(r);
        }
        else if(bid.quantity>ask.quantity){
            r.type="BUY";
            r.price=ask.price;
            e.add_buy_order(r);
        }
        else{
            r.type="SELL";
            r.price=bid.price;
            e.add_sell_order(r);
        }
    }
}

int main(){
    account a(10000,1);
    account b(10000,2);
    account c(10000,3);
    account d(10000,4);
    account us(10000,5);
    vector<string>st={"REL","TCS","TSL","AMZ"};
    for(auto &s:st){
        a.add_stocks(s,1000);
        b.add_stocks(s,1000);
        c.add_stocks(s,1000);
        d.add_stocks(s,1000);
        us.add_stocks(s,1000);
    }
    engine e;
    e.add_accounts(a);
    e.add_accounts(b);
    e.add_accounts(c);
    e.add_accounts(d);
    e.add_accounts(us);
    for(auto &s:st) e.add_stocks(s);
    vector<account>acc={a,b,c,d};
    auto start=chrono::steady_clock::now();
    thread simulator(market_sim,ref(e),ref(acc),ref(st));
    thread algo(trading_algo,ref(e),5,ref(st));
    simulator.join();
    algo.join();
    auto end=chrono::steady_clock::now();
    long long total_time=chrono::duration_cast<chrono::microseconds>(end-start).count();
    e.print_trade_book();
    e.print_benchmarks(total_time);
    e.print_algo_benchmarks(5,10000,st,total_time);
    hft_benchmark();
}
