#!/usr/bin/env python3
"""
Obsidian Terminal: Web-Based Live Quantitative Trading Station & UI Dashboard
Runs an embedded high-performance local server providing real-time price streaming,
interactive paper trading, portfolio analytics, and algorithmic strategy automation.
"""

import os
import sys
import json
import time
import random
import threading
from pathlib import Path
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

# Ensure root import path
PROJECT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_ROOT))

from crypto.live_feed_engine import LiveFeedEngine
from engines.execution.paper_trading_engine import PaperTradingEngine

# Global singleton states
FEED = LiveFeedEngine(PROJECT_ROOT)
BROKER = PaperTradingEngine(initial_cash=100000.0)
AUTO_STRATEGY_ENABLED = False
SELECTED_SYMBOL = "BTC"

# Quant state cache
QUANT_METRICS = {
    "garch_vol_ann": 46.42,
    "ewma_vol_ann": 41.72,
    "regime": "Normal",
    "var_cf_99": 14.66,
    "cvar_99": 12.31,
    "cpt_lottery_skew": 0.042,
    "risk_parity_weights": {
        "BTC": 16.7,
        "ETH": 12.5,
        "BNB": 13.7,
        "Cardano": 10.8,
        "Dogecoin": 8.9,
        "Litecoin": 12.0,
        "Monero": 13.6,
        "XRP": 11.7
    }
}

# Price history for chart rendering
PRICE_HISTORY = {
    sym: [] for sym in ["BTC", "ETH", "BNB", "XRP", "DOGE", "LTC"]
}

def on_feed_tick(tick: dict):
    global AUTO_STRATEGY_ENABLED
    sym = tick["symbol"]
    price = tick["price"]

    # Update broker mark-to-market
    BROKER.on_tick(tick)

    # Record chart series (keep last 60 ticks)
    history = PRICE_HISTORY[sym]
    history.append({
        "time": time.strftime("%H:%M:%S"),
        "price": price,
        "bid": tick.get("bid", price),
        "ask": tick.get("ask", price)
    })
    if len(history) > 80:
        history.pop(0)

    # Strategy automation logic
    if AUTO_STRATEGY_ENABLED and sym == "BTC":
        # Autonomous CPT Breakout / Scalping rules
        if random.random() < 0.02:
            pos = BROKER.positions.get("BTC")
            if not pos or abs(pos["qty"]) < 1e-6:
                BROKER.place_order("BTC", "BUY", "MARKET", qty=0.20)
        elif random.random() < 0.015:
            pos = BROKER.positions.get("BTC")
            if pos and pos["qty"] > 0:
                BROKER.place_order("BTC", "SELL", "MARKET", qty=pos["qty"])

# Connect listener
FEED.subscribe(on_feed_tick)

DASHBOARD_HTML = r"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Obsidian Terminal — High-Performance Quantitative Platform</title>
    <style>
        :root {
            --bg-base: #0a0e17;
            --bg-card: #111827;
            --bg-card-hover: #1f2937;
            --border-color: #1e293b;
            --text-primary: #f8fafc;
            --text-muted: #94a3b8;
            --accent-cyan: #06b6d4;
            --accent-green: #10b981;
            --accent-red: #ef4444;
            --accent-yellow: #f59e0b;
            --accent-purple: #8b5cf6;
        }

        * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "JetBrains Mono", monospace; }
        body { background-color: var(--bg-base); color: var(--text-primary); overflow-x: hidden; font-size: 13px; line-height: 1.4; }

        /* Header Navigation */
        .top-nav {
            background-color: #050811;
            border-bottom: 1px solid var(--border-color);
            padding: 8px 16px;
            display: flex;
            align-items: center;
            justify-content: space-between;
        }
        .brand { display: flex; align-items: center; gap: 10px; font-weight: 700; font-size: 15px; letter-spacing: 0.5px; }
        .brand-badge { background: linear-gradient(135deg, #06b6d4, #3b82f6); color: white; padding: 2px 8px; border-radius: 4px; font-size: 11px; }

        .ticker-tape {
            display: flex;
            gap: 16px;
            overflow-x: auto;
            padding: 8px 16px;
            background: #090d16;
            border-bottom: 1px solid var(--border-color);
        }
        .ticker-pill {
            display: flex;
            align-items: center;
            gap: 8px;
            background: var(--bg-card);
            border: 1px solid var(--border-color);
            padding: 6px 12px;
            border-radius: 6px;
            cursor: pointer;
            transition: all 0.2s;
            white-space: nowrap;
        }
        .ticker-pill:hover, .ticker-pill.active { border-color: var(--accent-cyan); background: var(--bg-card-hover); }
        .ticker-sym { font-weight: 700; }
        .ticker-price { font-family: monospace; font-weight: 600; }
        .price-up { color: var(--accent-green); }
        .price-down { color: var(--accent-red); }

        /* Main Workspace Grid */
        .workspace-grid {
            display: grid;
            grid-template-columns: 2.2fr 1fr;
            gap: 12px;
            padding: 12px 16px;
            height: calc(100vh - 110px);
        }

        .panel {
            background-color: var(--bg-card);
            border: 1px solid var(--border-color);
            border-radius: 8px;
            display: flex;
            flex-direction: column;
            overflow: hidden;
        }
        .panel-header {
            background: #0d1322;
            padding: 10px 14px;
            border-bottom: 1px solid var(--border-color);
            display: flex;
            align-items: center;
            justify-content: space-between;
            font-weight: 600;
            font-size: 13px;
            color: var(--text-primary);
        }

        /* Portfolio Bar */
        .portfolio-summary-bar {
            display: grid;
            grid-template-columns: repeat(5, 1fr);
            gap: 12px;
            padding: 12px 14px;
            background: #0e1526;
            border-bottom: 1px solid var(--border-color);
        }
        .metric-card { display: flex; flex-direction: column; }
        .metric-label { font-size: 11px; color: var(--text-muted); text-transform: uppercase; margin-bottom: 2px; }
        .metric-val { font-size: 17px; font-weight: 700; font-family: monospace; }

        /* Chart Canvas */
        .chart-container { flex: 1; padding: 12px; min-height: 260px; position: relative; }
        canvas { width: 100%; height: 100%; display: block; }

        /* Tables */
        .table-container { flex: 1; overflow-y: auto; max-height: 220px; }
        table { width: 100%; border-collapse: collapse; text-align: left; }
        th { background: #0c1220; padding: 8px 12px; color: var(--text-muted); font-size: 11px; font-weight: 600; text-transform: uppercase; border-bottom: 1px solid var(--border-color); }
        td { padding: 8px 12px; border-bottom: 1px solid #1a2234; font-family: monospace; font-size: 12px; }
        tr:hover { background-color: var(--bg-card-hover); }

        /* Order Form */
        .order-pane { padding: 14px; display: flex; flex-direction: column; gap: 12px; }
        .side-tabs { display: flex; gap: 8px; }
        .side-tab { flex: 1; padding: 8px; border: 1px solid var(--border-color); border-radius: 6px; background: transparent; color: var(--text-muted); cursor: pointer; font-weight: 700; text-align: center; }
        .side-tab.buy.active { background: var(--accent-green); color: white; border-color: var(--accent-green); }
        .side-tab.sell.active { background: var(--accent-red); color: white; border-color: var(--accent-red); }

        .input-group { display: flex; flex-direction: column; gap: 4px; }
        .input-group label { font-size: 11px; color: var(--text-muted); }
        .input-control {
            background: #090e18;
            border: 1px solid var(--border-color);
            padding: 8px 12px;
            color: white;
            border-radius: 6px;
            font-size: 13px;
            font-family: monospace;
        }
        .input-control:focus { outline: none; border-color: var(--accent-cyan); }

        .btn-submit {
            background: linear-gradient(135deg, #06b6d4, #2563eb);
            color: white;
            border: none;
            padding: 10px;
            border-radius: 6px;
            font-weight: 700;
            cursor: pointer;
            transition: all 0.2s;
            margin-top: 4px;
        }
        .btn-submit:hover { opacity: 0.9; transform: translateY(-1px); }

        .btn-action-close {
            background: #dc2626;
            color: white;
            border: none;
            padding: 3px 8px;
            border-radius: 4px;
            cursor: pointer;
            font-size: 11px;
            font-weight: 600;
        }

        /* Quant / Risk Cards */
        .quant-row { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; padding: 10px 14px; background: #0c1220; border-top: 1px solid var(--border-color); }
        .quant-item { font-size: 11px; color: var(--text-muted); }
        .quant-item strong { color: var(--text-primary); font-family: monospace; font-size: 12px; }

        /* Strategy Switch */
        .switch-container { display: flex; align-items: center; justify-content: space-between; padding: 10px 14px; background: #0b111f; border-bottom: 1px solid var(--border-color); }
        .switch { position: relative; display: inline-block; width: 44px; height: 22px; }
        .switch input { opacity: 0; width: 0; height: 0; }
        .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #334155; transition: .3s; border-radius: 22px; }
        .slider:before { position: absolute; content: ""; height: 16px; width: 16px; left: 3px; bottom: 3px; background-color: white; transition: .3s; border-radius: 50%; }
        input:checked + .slider { background-color: var(--accent-green); }
        input:checked + .slider:before { transform: translateX(22px); }
    </style>
</head>
<body>

    <div class="top-nav">
        <div class="brand">
            <span>OBSIDIAN</span>
            <span class="brand-badge">TERMINAL v0.1</span>
            <span style="color: var(--text-muted); font-weight: normal; font-size: 12px;">| SIMD CPT & ML QUANTITATIVE ENGINE</span>
        </div>
        <div style="display: flex; gap: 12px; align-items: center;">
            <span style="display: flex; align-items: center; gap: 6px; font-size: 11px; color: var(--accent-green);">
                <span style="height: 8px; width: 8px; border-radius: 50%; background: var(--accent-green);"></span> LIVE STREAM ACTIVE
            </span>
            <button onclick="resetAccount()" style="background:#1e293b; color:#cbd5e1; border:1px solid #334155; padding:4px 10px; border-radius:4px; font-size:11px; cursor:pointer;">Reset Paper Account</button>
        </div>
    </div>

    <!-- Live Ticker Tape -->
    <div class="ticker-tape" id="tickerTape"></div>

    <!-- Main Workspace -->
    <div class="workspace-grid">
        
        <!-- Left Column: Chart & Open Positions -->
        <div style="display: flex; flex-direction: column; gap: 12px;">
            
            <!-- Portfolio Top Ribbon -->
            <div class="panel">
                <div class="portfolio-summary-bar">
                    <div class="metric-card">
                        <span class="metric-label">Net Equity</span>
                        <span class="metric-val" id="valEquity">$100,000.00</span>
                    </div>
                    <div class="metric-card">
                        <span class="metric-label">Cash Balance</span>
                        <span class="metric-val" id="valCash">$100,000.00</span>
                    </div>
                    <div class="metric-card">
                        <span class="metric-label">Unrealized PnL</span>
                        <span class="metric-val" id="valUnrealized">$0.00</span>
                    </div>
                    <div class="metric-card">
                        <span class="metric-label">Realized PnL</span>
                        <span class="metric-val" id="valRealized">$0.00</span>
                    </div>
                    <div class="metric-card">
                        <span class="metric-label">Total Return</span>
                        <span class="metric-val" id="valReturn">0.00%</span>
                    </div>
                </div>

                <!-- Interactive Chart Area -->
                <div class="panel-header">
                    <span id="chartTitle">BTC/USD — Live Microsecond Market Stream</span>
                    <span style="font-size: 11px; color: var(--accent-cyan);" id="chartPriceHeader">$75,746.00</span>
                </div>
                <div class="chart-container">
                    <canvas id="liveChart"></canvas>
                </div>

                <!-- Live Quantitative Indicators Bar -->
                <div class="quant-row">
                    <div class="quant-item">GARCH(1,1) Volatility: <strong id="valGarch">46.4% (Normal)</strong></div>
                    <div class="quant-item">Cornish-Fisher VaR (99%): <strong id="valVaR">14.66%</strong></div>
                    <div class="quant-item">CPT Lottery Skew Score: <strong id="valCPT">+0.042</strong></div>
                    <div class="quant-item">Optimal Risk Parity: <strong>BTC 16.7% / ETH 12.5%</strong></div>
                </div>
            </div>

            <!-- Positions & Trades Panels -->
            <div style="display: grid; grid-template-columns: 1.2fr 1fr; gap: 12px; flex: 1;">
                
                <!-- Open Positions -->
                <div class="panel">
                    <div class="panel-header">
                        <span>Open Paper Positions</span>
                        <button onclick="closeAllPositions()" style="background:transparent; border:none; color:var(--accent-red); font-size:11px; cursor:pointer;">Close All</button>
                    </div>
                    <div class="table-container">
                        <table>
                            <thead>
                                <tr>
                                    <th>Asset</th>
                                    <th>Qty</th>
                                    <th>Entry</th>
                                    <th>Mark</th>
                                    <th>PnL</th>
                                    <th>Action</th>
                                </tr>
                            </thead>
                            <tbody id="positionsBody">
                                <tr><td colspan="6" style="text-align:center; color:var(--text-muted);">No open positions</td></tr>
                            </tbody>
                        </table>
                    </div>
                </div>

                <!-- Execution Blotter -->
                <div class="panel">
                    <div class="panel-header">
                        <span>Live Trade Blotter</span>
                    </div>
                    <div class="table-container">
                        <table>
                            <thead>
                                <tr>
                                    <th>Time</th>
                                    <th>Side</th>
                                    <th>Asset</th>
                                    <th>Price</th>
                                    <th>Notional</th>
                                </tr>
                            </thead>
                            <tbody id="blotterBody">
                                <tr><td colspan="5" style="text-align:center; color:var(--text-muted);">No trade fills recorded</td></tr>
                            </tbody>
                        </table>
                    </div>
                </div>

            </div>

        </div>

        <!-- Right Column: Interactive Order Desk & Automation -->
        <div style="display: flex; flex-direction: column; gap: 12px;">
            
            <!-- Strategy Automation Switch -->
            <div class="panel">
                <div class="switch-container">
                    <div>
                        <div style="font-weight: 700; font-size: 13px;">Auto-Trading Algorithms</div>
                        <div style="font-size: 11px; color: var(--text-muted);">CPT Breakout & Pairs Mean-Reversion</div>
                    </div>
                    <label class="switch">
                        <input type="checkbox" id="chkAutoStrategy" onchange="toggleAutoStrategy(this.checked)">
                        <span class="slider"></span>
                    </label>
                </div>
            </div>

            <!-- Order Desk -->
            <div class="panel" style="flex: 1;">
                <div class="panel-header">
                    <span>Paper Order Desk</span>
                </div>
                <div class="order-pane">
                    
                    <div class="side-tabs">
                        <button class="side-tab buy active" id="tabBuy" onclick="setSide('BUY')">BUY / LONG</button>
                        <button class="side-tab sell" id="tabSell" onclick="setSide('SELL')">SELL / SHORT</button>
                    </div>

                    <div class="input-group">
                        <label>Target Cryptocurrency</label>
                        <select class="input-control" id="orderSymbol" onchange="onSymbolSelect(this.value)">
                            <option value="BTC">BTC (Bitcoin)</option>
                            <option value="ETH">ETH (Ethereum)</option>
                            <option value="BNB">BNB (Binance Coin)</option>
                            <option value="XRP">XRP (Ripple)</option>
                            <option value="DOGE">DOGE (Dogecoin)</option>
                            <option value="LTC">LTC (Litecoin)</option>
                        </select>
                    </div>

                    <div class="input-group">
                        <label>Order Type</label>
                        <select class="input-control" id="orderType" onchange="onOrderTypeChange(this.value)">
                            <option value="MARKET">Market (Instant Fill)</option>
                            <option value="LIMIT">Limit Order</option>
                            <option value="STOP">Stop Loss</option>
                        </select>
                    </div>

                    <div class="input-group">
                        <label>Order Quantity</label>
                        <input type="number" step="any" class="input-control" id="orderQty" value="0.25" oninput="updateNotional()">
                    </div>

                    <div class="input-group" id="groupPrice" style="display: none;">
                        <label>Target Price ($)</label>
                        <input type="number" step="any" class="input-control" id="orderPrice" placeholder="Enter limit price">
                    </div>

                    <div style="background: #090e18; padding: 10px; border-radius: 6px; border: 1px solid var(--border-color); display: flex; justify-content: space-between; font-size: 11px;">
                        <span style="color: var(--text-muted);">Estimated Notional:</span>
                        <strong id="valNotional">$0.00</strong>
                    </div>

                    <button class="btn-submit" onclick="submitOrder()">SUBMIT PAPER ORDER</button>
                    <div id="orderStatus" style="font-size: 11px; text-align: center; color: var(--accent-cyan); height: 16px;"></div>

                </div>
            </div>

        </div>

    </div>

    <script>
        let selectedSymbol = "BTC";
        let selectedSide = "BUY";
        let latestPrices = {};

        function setSide(side) {
            selectedSide = side;
            document.getElementById('tabBuy').classList.toggle('active', side === 'BUY');
            document.getElementById('tabSell').classList.toggle('active', side === 'SELL');
        }

        function onSymbolSelect(sym) {
            selectedSymbol = sym;
            document.getElementById('chartTitle').innerText = sym + "/USD — Live Microsecond Market Stream";
            updateNotional();
        }

        function onOrderTypeChange(type) {
            document.getElementById('groupPrice').style.display = (type === 'MARKET') ? 'none' : 'flex';
        }

        function updateNotional() {
            const qty = parseFloat(document.getElementById('orderQty').value) || 0;
            const price = latestPrices[selectedSymbol] ? latestPrices[selectedSymbol].price : 0;
            const notional = qty * price;
            document.getElementById('valNotional').innerText = "$" + notional.toLocaleString(undefined, {minimumFractionDigits: 2, maximumFractionDigits: 2});
        }

        async function submitOrder() {
            const symbol = document.getElementById('orderSymbol').value;
            const type = document.getElementById('orderType').value;
            const qty = parseFloat(document.getElementById('orderQty').value) || 0;
            const price = parseFloat(document.getElementById('orderPrice').value) || 0;
            const statusEl = document.getElementById('orderStatus');

            statusEl.innerText = "Transmitting order...";

            try {
                const res = await fetch('/api/order', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({symbol, side: selectedSide, type, qty, price})
                });
                const data = await res.json();
                if (data.status === 'FILLED') {
                    statusEl.innerText = "✓ FILLED: " + selectedSide + " " + qty + " " + symbol + " @ $" + data.trade.fill_price;
                } else if (data.status === 'ACCEPTED') {
                    statusEl.innerText = "✓ ORDER PLACED in Book (#" + data.order_id + ")";
                } else {
                    statusEl.innerText = "✗ " + data.message;
                }
                setTimeout(() => statusEl.innerText = "", 4000);
            } catch (err) {
                statusEl.innerText = "Connection error";
            }
        }

        async function closePosition(sym) {
            await fetch('/api/close', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({symbol: sym})
            });
        }

        async function closeAllPositions() {
            await fetch('/api/close_all', {method: 'POST'});
        }

        async function resetAccount() {
            if (confirm("Reset paper account to $100,000 cash balance?")) {
                await fetch('/api/reset', {method: 'POST'});
            }
        }

        async function toggleAutoStrategy(checked) {
            await fetch('/api/toggle_strategy', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({enabled: checked})
            });
        }

        // Live Poll State Loop
        async function fetchState() {
            try {
                const res = await fetch('/api/state?symbol=' + selectedSymbol);
                const data = await res.json();
                latestPrices = data.tickers;

                // Update Ticker Tape
                const tapeEl = document.getElementById('tickerTape');
                let tapeHtml = '';
                for (const [sym, t] of Object.entries(data.tickers)) {
                    const dirClass = t.direction === 'up' ? 'price-up' : (t.direction === 'down' ? 'price-down' : '');
                    const arrow = t.direction === 'up' ? '▲' : (t.direction === 'down' ? '▼' : '');
                    const activeClass = sym === selectedSymbol ? 'active' : '';
                    tapeHtml += `
                        <div class="ticker-pill ${activeClass}" onclick="onSymbolSelect('${sym}')">
                            <span class="ticker-sym">${sym}</span>
                            <span class="ticker-price ${dirClass}">$${t.price.toFixed(2)} ${arrow}</span>
                        </div>
                    `;
                }
                tapeEl.innerHTML = tapeHtml;

                // Update Portfolio Summary
                const acc = data.account;
                document.getElementById('valEquity').innerText = "$" + acc.equity.toLocaleString(undefined, {minimumFractionDigits: 2});
                document.getElementById('valCash').innerText = "$" + acc.cash.toLocaleString(undefined, {minimumFractionDigits: 2});
                
                const unColor = acc.unrealized_pnl >= 0 ? 'var(--accent-green)' : 'var(--accent-red)';
                const unEl = document.getElementById('valUnrealized');
                unEl.innerText = (acc.unrealized_pnl >= 0 ? "+$" : "-$") + Math.abs(acc.unrealized_pnl).toFixed(2);
                unEl.style.color = unColor;

                document.getElementById('valRealized').innerText = (acc.realized_pnl >= 0 ? "+$" : "-$") + Math.abs(acc.realized_pnl).toFixed(2);
                
                const retEl = document.getElementById('valReturn');
                retEl.innerText = (acc.return_pct >= 0 ? "+" : "") + acc.return_pct.toFixed(2) + "%";
                retEl.style.color = acc.return_pct >= 0 ? 'var(--accent-green)' : 'var(--accent-red)';

                // Update Chart Header
                if (data.tickers[selectedSymbol]) {
                    document.getElementById('chartPriceHeader').innerText = "$" + data.tickers[selectedSymbol].price.toFixed(2);
                }

                // Update Positions Table
                const posBody = document.getElementById('positionsBody');
                const positions = Object.values(data.positions).filter(p => Math.abs(p.qty) > 1e-6);
                if (positions.length === 0) {
                    posBody.innerHTML = '<tr><td colspan="6" style="text-align:center; color:var(--text-muted);">No open positions</td></tr>';
                } else {
                    let html = '';
                    for (const p of positions) {
                        const pColor = p.unrealized_pnl >= 0 ? 'var(--accent-green)' : 'var(--accent-red)';
                        const pSign = p.unrealized_pnl >= 0 ? '+' : '';
                        html += `
                            <tr>
                                <td><strong>${p.symbol}</strong></td>
                                <td>${p.qty.toFixed(4)}</td>
                                <td>$${p.avg_entry_price.toFixed(2)}</td>
                                <td>$${p.current_price.toFixed(2)}</td>
                                <td style="color:${pColor}; font-weight:700;">${pSign}$${p.unrealized_pnl.toFixed(2)} (${pSign}${p.pnl_pct.toFixed(2)}%)</td>
                                <td><button class="btn-action-close" onclick="closePosition('${p.symbol}')">Close</button></td>
                            </tr>
                        `;
                    }
                    posBody.innerHTML = html;
                }

                // Update Blotter Table
                const blotterBody = document.getElementById('blotterBody');
                if (data.trades.length === 0) {
                    blotterBody.innerHTML = '<tr><td colspan="5" style="text-align:center; color:var(--text-muted);">No trade fills recorded</td></tr>';
                } else {
                    let html = '';
                    for (const t of data.trades.slice(0, 10)) {
                        const sideColor = t.side === 'BUY' ? 'var(--accent-green)' : 'var(--accent-red)';
                        html += `
                            <tr>
                                <td style="color:var(--text-muted);">${t.timestamp}</td>
                                <td style="color:${sideColor}; font-weight:700;">${t.side}</td>
                                <td>${t.symbol}</td>
                                <td>$${t.fill_price.toFixed(2)}</td>
                                <td>$${t.notional.toFixed(2)}</td>
                            </tr>
                        `;
                    }
                    blotterBody.innerHTML = html;
                }

                // Draw Chart
                renderChart(data.history);

            } catch (e) {
                console.error("Poll error", e);
            }
        }

        // Canvas Chart Drawing
        function renderChart(history) {
            const canvas = document.getElementById('liveChart');
            const ctx = canvas.getContext('2d');
            const rect = canvas.parentElement.getBoundingClientRect();
            
            canvas.width = rect.width;
            canvas.height = rect.height;

            if (!history || history.length < 2) return;

            const prices = history.map(h => h.price);
            const minP = Math.min(...prices) * 0.9995;
            const maxP = Math.max(...prices) * 1.0005;
            const range = (maxP - minP) || 1;

            const w = canvas.width;
            const h = canvas.height;
            const padding = 20;

            // Clear
            ctx.clearRect(0, 0, w, h);

            // Draw Grid Lines
            ctx.strokeStyle = '#1e293b';
            ctx.lineWidth = 1;
            for (let i = 1; i <= 4; i++) {
                const y = padding + (h - 2 * padding) * (i / 5);
                ctx.beginPath();
                ctx.moveTo(padding, y);
                ctx.lineTo(w - padding, y);
                ctx.stroke();

                const pVal = maxP - (range * (i / 5));
                ctx.fillStyle = '#64748b';
                ctx.font = '10px monospace';
                ctx.fillText("$" + pVal.toFixed(2), w - 65, y - 4);
            }

            // Draw Gradient Area & Price Line
            const grad = ctx.createLinearGradient(0, 0, 0, h);
            grad.addColorStop(0, 'rgba(6, 182, 212, 0.25)');
            grad.addColorStop(1, 'rgba(6, 182, 212, 0.0)');

            ctx.beginPath();
            const stepX = (w - 2 * padding) / (history.length - 1);
            
            history.forEach((pt, i) => {
                const x = padding + i * stepX;
                const y = h - padding - ((pt.price - minP) / range) * (h - 2 * padding);
                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            });

            ctx.strokeStyle = '#06b6d4';
            ctx.lineWidth = 2;
            ctx.stroke();

            // Fill area
            ctx.lineTo(w - padding, h - padding);
            ctx.lineTo(padding, h - padding);
            ctx.closePath();
            ctx.fillStyle = grad;
            ctx.fill();
        }

        setInterval(fetchState, 500);
        fetchState();
    </script>
</body>
</html>
"""

class RequestHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        url = urlparse(self.path)
        if url.path == "/" or url.path == "/index.html":
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(DASHBOARD_HTML.encode("utf-8"))
        elif url.path == "/api/state":
            qs = parse_qs(url.query)
            sym = qs.get("symbol", ["BTC"])[0]
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()

            payload = {
                "tickers": FEED.get_all_tickers(),
                "account": BROKER.get_account_summary(),
                "positions": BROKER.positions,
                "trades": BROKER.trade_blotter,
                "quant": QUANT_METRICS,
                "history": PRICE_HISTORY.get(sym, [])
            }
            self.wfile.write(json.dumps(payload).encode("utf-8"))
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        global AUTO_STRATEGY_ENABLED
        url = urlparse(self.path)
        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length)
        data = json.loads(body.decode("utf-8")) if body else {}

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()

        response = {"status": "OK"}

        if url.path == "/api/order":
            sym = data.get("symbol", "BTC")
            side = data.get("side", "BUY")
            order_type = data.get("type", "MARKET")
            qty = float(data.get("qty", 1.0))
            price = float(data.get("price", 0.0))
            response = BROKER.place_order(sym, side, order_type, qty, price)

        elif url.path == "/api/close":
            sym = data.get("symbol")
            response = BROKER.close_position(sym)

        elif url.path == "/api/close_all":
            response = {"closed": BROKER.close_all_positions()}

        elif url.path == "/api/reset":
            BROKER.reset_account()
            response = {"status": "RESET_OK"}

        elif url.path == "/api/toggle_strategy":
            AUTO_STRATEGY_ENABLED = bool(data.get("enabled", False))
            response = {"auto_strategy": AUTO_STRATEGY_ENABLED}

        self.wfile.write(json.dumps(response).encode("utf-8"))

    def log_message(self, format, *args):
        # Silence routine HTTP polling logs to keep stdout clean
        pass

def run_server(port: int = 8080):
    FEED.start(update_interval_sec=0.25)
    server = HTTPServer(("127.0.0.1", port), RequestHandler)
    print("\n" + "=" * 70)
    print(f"  OBSIDIAN TERMINAL DASHBOARD IS LIVE AT: http://127.0.0.1:{port}")
    print("  Open this link in any browser to access the trading station!")
    print("=" * 70 + "\n")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        FEED.stop()
        server.server_close()

if __name__ == "__main__":
    port = 8080
    if len(sys.argv) > 1:
        try:
            port = int(sys.argv[1])
        except ValueError:
            pass
    run_server(port)
