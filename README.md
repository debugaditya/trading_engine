# C++ Trading Engine: Low-Latency Limit Order Book and Market Simulation

**Aditya Narayan Barmola**  
Netaji Subhas University of Technology (NSUT), New Delhi, India

---

## Abstract

This project implements a C++ electronic trading engine built around a price-time-priority limit order book. The system supports multiple stocks, limit BUY and SELL orders, partial fills, account-level cash and inventory settlement, order cancellation, order modification, trade recording, concurrent market simulation, portfolio accounting, market-return measurement, and latency benchmarking.

A synthetic market generates orders for `REL`, `TCS`, `TSL`, and `AMZ`. A separate trading algorithm observes the live order book and submits orders based on available liquidity. The resulting portfolio performance can be compared with the movement of the complete simulated market.

A dedicated HFT stress benchmark processes one million alternating one-share BUY and SELL orders through the matching engine and reports throughput and latency percentiles.

**Keywords:** C++, trading engine, limit order book, price-time priority, electronic trading, market simulation, low latency, benchmarking

---

# 1. Introduction

Electronic trading systems receive orders from market participants and continuously determine whether those orders can be matched against existing liquidity. A matching engine must maintain an ordered bid/ask book while correctly updating cash and inventory after every execution.

The goal of this project is to implement these core mechanics in C++ and evaluate the resulting system under both a realistic synthetic market workload and a high-frequency stress workload.

The project focuses on:

1. Price-time-priority order matching
2. Cash and inventory settlement
3. Concurrent market and strategy simulation
4. Portfolio and market-return analysis
5. Low-latency performance measurement

---

# 2. System Architecture

```text
                    +----------------------+
                    |   Market Simulator   |
                    +----------+-----------+
                               |
                               v
+----------------+    +----------------------+
| Trading Algo   | -> |    Matching Engine   |
+----------------+    +----------+-----------+
                                  |
                    +-------------+-------------+
                    |             |             |
                    v             v             v
              Trade Book     Accounts      Benchmarks
                    |             |
                    +------+------+
                           |
                           v
                Portfolio / Market Analytics
```

The market simulator and trading algorithm both interact with the same matching engine.

---

# 3. Core Data Model

## 3.1 Order

Each order contains:

```text
order_id
 type
stock
quantity
price
time
demat_id
```

The current implementation supports limit BUY and SELL orders.

## 3.2 Trade

Every execution records:

```text
trade_id
buy_order_id
sell_order_id
buy_demat_id
sell_demat_id
stock
quantity
price
time
```

## 3.3 Account

Each account maintains:

- Cash balance
- Stock holdings by symbol
- Demat/account ID

---

# 4. Order Book Design

The engine maintains independent BUY and SELL priority queues for every stock.

## 4.1 BUY Priority

BUY orders are ranked by:

1. Higher price
2. Earlier timestamp
3. Lower order ID as the final tie-breaker

```text
Highest BUY price
        |
        v
    Best Bid
```

## 4.2 SELL Priority

SELL orders are ranked by:

1. Lower price
2. Earlier timestamp
3. Lower order ID as the final tie-breaker

```text
Lowest SELL price
        |
        v
    Best Ask
```

This implements price-time priority.

---

# 5. Matching Engine

## 5.1 BUY Matching

A BUY order can execute when:

```text
BUY price >= Best Ask
```

The incoming order repeatedly consumes the best ask until the order is fully filled or the next ask is outside its limit price.

## 5.2 SELL Matching

A SELL order can execute when:

```text
SELL price <= Best Bid
```

The incoming order repeatedly consumes the best bid until the order is fully filled or the next bid is outside its limit price.

## 5.3 Execution Price

Trades execute at the price of the resting order on the opposite side of the book.

## 5.4 Partial Fills

Partial fills are supported.

```text
Incoming BUY: 100 shares

Best Ask: 40 shares
    -> Execute 40

Remaining BUY: 60 shares

Next Ask: 60 shares
    -> Execute 60
```

The engine continues matching until the incoming order is completely filled or no compatible order remains.

---

# 6. Account Settlement

Each account maintains:

```text
Cash balance
Stock holdings
Demat ID
```

## BUY Settlement

Before a BUY order is matched, the engine reserves:

```text
Quantity × Limit Price
```

For every execution:

```text
Buyer  -> receives shares
Seller -> receives trade value
```

Unused reserved cash is returned after the order finishes matching.

## SELL Settlement

Before a SELL order is submitted, the requested inventory is reserved.

For every execution:

```text
Seller -> receives trade value
Buyer  -> receives shares
```

Remaining inventory continues backing the unfilled resting order.

---

# 7. Order Management

## 7.1 Cancellation

Cancelling a live order restores its remaining reserved resource:

```text
BUY  -> remaining reserved cash
SELL -> remaining reserved shares
```

The active order is then removed from the order map.

## 7.2 Modification

Orders can be modified through the engine by changing:

```text
type
stock
quantity
price
demat_id
```

The modified order receives a new timestamp and is reinserted into the correct priority queue.

The engine validates queue entries against the active order stored in the request map, preventing stale entries created by previous modifications or cancellations from being treated as live orders.

---

# 8. Market Simulation

The synthetic market contains:

```text
REL
TCS
TSL
AMZ
```

Each stock starts with an initial price of:

```text
$100.00
```

The initial price remains active until the stock experiences its first actual trade.

After the first trade, the simulator generates prices relative to the current order book.

## BUY Price Generation

If an ask exists:

```text
BUY = Best Ask + random movement
```

Otherwise:

```text
BUY = Best Bid + random movement
```

## SELL Price Generation

If a bid exists:

```text
SELL = Best Bid - random movement
```

Otherwise:

```text
SELL = Best Ask - random movement
```

Current price movement:

```text
$0.00 to $2.00
```

Decimal movements are supported and prices are rounded to two decimal places.

The simulator also introduces a 5–25 ms delay between generated orders.

---

# 9. Trading Algorithm

A separate strategy account observes the live order book.

The current strategy:

1. Selects a random stock.
2. Reads the best bid and best ask.
3. Skips the stock if both books are empty.
4. Buys at the ask if only an ask exists.
5. Sells at the bid if only a bid exists.
6. If both sides exist, compares their quantities.
7. Buys when bid quantity is larger.
8. Otherwise sells at the bid.

The strategy is intentionally simple and acts as a baseline participant interacting with the same order book as the simulated market.

---

# 10. Portfolio Evaluation

The strategy account starts with:

```text
Cash: $10,000

REL: 1,000 shares
TCS: 1,000 shares
TSL: 1,000 shares
AMZ: 1,000 shares
```

At the initial `$100` price:

```text
Initial Inventory Value
= 4 × 1,000 × $100
= $400,000
```

Therefore:

```text
Starting Portfolio Value
= $10,000 + $400,000
= $410,000
```

The engine reports:

```text
Initial Inventory Value
Final Inventory Value
Starting Portfolio Value
Final Portfolio Value
Trade Cash Flow P&L
Total P&L
Return %
```

Final inventory is marked using the final available market price for each stock.

---

# 11. Market Return

The engine also evaluates the total value of stock holdings across the complete simulated market.

The simulation starts with:

```text
5 accounts
4 stocks
1,000 shares per stock per account
$100 initial price
```

Therefore:

```text
Initial Market Value
= 5 × 4 × 1,000 × $100
= $2,000,000
```

The engine reports:

```text
Initial Market Value
Final Market Value
Market Value Change
Market Return %
```

This provides a market-level return that can be compared with the strategy portfolio return.

---

# 12. Benchmarking

The engine provides three performance measurements.

## 12.1 Market Benchmark

Reports:

- Order count
- Runtime
- Throughput
- Average latency
- P50 latency
- P95 latency
- P99 latency

This benchmark includes the simulator's intentional 5–25 ms delays and therefore does not represent raw matching-engine capacity.

## 12.2 Trading Algorithm Benchmark

Reports:

- Orders
- Trades
- Buy quantity
- Sell quantity
- Buy value
- Sell value
- Trade cash-flow P&L
- Starting cash
- Ending cash
- Inventory by stock
- Mark prices
- Initial inventory value
- Final inventory value
- Starting portfolio value
- Final portfolio value
- Total P&L
- Return
- P50/P95/P99 latency

## 12.3 HFT Stress Benchmark

A dedicated synthetic workload processes:

```text
1,000,000 orders
```

using alternating one-share SELL and BUY orders at:

```text
$150
```

It reports:

```text
Total Time
Throughput
Average Latency
P50
P95
P99
P99.9
```

---

# 13. Representative HFT Result

One observed run produced:

```text
ORDERS:            1,000,000
THROUGHPUT:        489,820 orders/s
AVERAGE LATENCY:   1.53 us
P50:               1.20 us
P95:               2.90 us
P99:               3.30 us
P99.9:            13.00 us
```

These values are environment-dependent and can vary with CPU architecture, compiler, optimization level, operating system, memory behavior, and system load.

## 13.1 Benchmark Environment

All reported benchmark results were performed locally on an **ASUS Vivobook laptop** rather than a dedicated server or workstation.

```text
Device:       ASUS Vivobook
Processor:    12th Gen Intel Core i5-12500H
RAM:          16 GB
Graphics:     Intel Iris Xe Graphics
OS:           64-bit Windows
```

The HFT benchmark therefore represents the performance observed on this specific laptop configuration. It should not be interpreted as a hardware-independent measure of matching-engine performance.

---

# 14. Concurrency Model

The market simulator and trading algorithm run concurrently.

A mutex protects shared engine state including:

```text
Order books
Active orders
Accounts
Trade book
Benchmark records
```

The current implementation prioritizes correctness and simple synchronization rather than maximum parallelism.

---

# 15. Data Structures

| Component | Structure |
|---|---|
| Active orders | `map<int, shared_ptr<request>>` |
| BUY books | `map<string, priority_queue<...>>` |
| SELL books | `map<string, priority_queue<...>>` |
| Accounts | `map<int, account>` |
| Trades | `vector<trade>` |
| Benchmarks | `vector<benchmark_record>` |
| Account stocks | `unordered_map<string, int>` |

---

# 16. Execution Flow

```text
Initialize Accounts
        |
        v
Initialize Stocks at $100
        |
        v
Start Market Simulator + Trading Algorithm
        |
        v
Generate Orders
        |
        v
Cash / Inventory Validation
        |
        v
Order Book Matching
        |
        +-------------------+
        |                   |
        v                   v
     Execute             Rest Order
      Trade              on Book
        |                   |
        +---------+---------+
                  |
                  v
          Account Settlement
                  |
                  v
          Record Latency
                  |
                  v
       Portfolio / Market Analysis
                  |
                  v
           HFT Stress Test
```

---

# 17. Build and Run

## Requirements

- C++17-compatible compiler
- Standard C++ library

## Linux / macOS / MinGW

```bash
g++ -std=c++17 -O2 trading_engine_market_value.cpp -o trading_engine
./trading_engine
```

## Windows

```bash
g++ -std=c++17 -O2 trading_engine_market_value.cpp -o trading_engine.exe
trading_engine.exe
```

---

# 18. Current Limitations

The current implementation does not yet include:

- Market orders
- IOC/FOK orders
- Stop orders
- Pre-trade risk limits
- Position limits
- Kill switches
- TCP/UDP order gateways
- Market-data feeds
- Binary network protocols
- Persistent event logging
- Historical market-data replay
- Lock-free queues
- Per-symbol parallel matching
- FPGA/hardware acceleration

The current implementation also uses a global mutex, `std::map`, `priority_queue`, and dynamically allocated order objects. These provide a straightforward foundation but leave room for further low-latency optimization.

---

# 19. Roadmap

## Order Book

- [x] Limit order matching
- [x] Price-time priority
- [x] Partial fills
- [x] Order cancellation
- [x] Order modification
- [x] Trade book

## Simulation & Analytics

- [x] Synthetic market simulator
- [x] Automated trading strategy
- [x] Portfolio valuation
- [x] Strategy P&L
- [x] Whole-market return
- [x] Latency percentiles
- [x] HFT stress benchmark

## Trading Infrastructure

- [ ] Market-data feed
- [ ] Order gateway
- [ ] Risk engine
- [ ] Binary protocol
- [ ] Event logging and replay
- [ ] Lock-free queues
- [ ] Per-symbol parallelism
- [ ] Cache-aware optimization

---

# 20. Future Work

### Market Infrastructure

- Market-data publisher
- Historical market replay
- TCP/UDP order gateway
- Binary wire protocol

### Risk Management

- Maximum order size
- Maximum position
- Notional limits
- Price bands
- Kill switch

### Low-Latency Optimization

- Per-symbol concurrency
- Lock-free queues
- Object pools
- Reduced dynamic allocation
- Cache-aware data structures
- CPU affinity
- Detailed latency profiling

### Strategy Research

- Multiple strategies
- Order-flow imbalance
- Spread analysis
- Slippage analysis
- Sharpe ratio
- Maximum drawdown
- Tracking error
- Repeated Monte Carlo simulations

---

# 21. Project Goals

This project explores the systems engineering behind electronic trading:

```text
Order Book Design
        +
Price-Time Matching
        +
Account Settlement
        +
Concurrent Simulation
        +
Strategy Evaluation
        +
Latency Measurement
        +
Throughput Benchmarking
```

The longer-term goal is to extend the current in-memory engine toward a more complete low-latency trading infrastructure stack.

---

# 22. Conclusion

The project implements a complete simulated trading loop around a C++ price-time-priority limit order book.

It combines:

- Multi-stock order books
- Limit order matching
- Partial fills
- Account settlement
- Order cancellation
- Order modification
- Trade recording
- Synthetic market generation
- Automated strategy execution
- Portfolio/P&L analysis
- Market-return comparison
- Microsecond-scale latency measurement
- Million-order HFT stress testing

The current engine provides a foundation for further work in market-data processing, risk management, networking, concurrency, memory optimization, and low-latency systems design.
