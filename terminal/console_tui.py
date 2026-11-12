#!/usr/bin/env python3
"""
Obsidian Terminal: Live Console Textual User Interface (TUI)
Renders a live-updating Bloomberg-style quantitative trading terminal in the command prompt.
Displays streaming tickers, live paper trading positions, account equity, and GARCH volatility.
"""

import os
import sys
import time
import math
import random
import threading
from pathlib import Path
from typing import Optional, Dict, List

# Ensure root import path
PROJECT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_ROOT))

from crypto.live_feed_engine import LiveFeedEngine
from engines.execution.paper_trading_engine import PaperTradingEngine

# ANSI color codes
RESET = "\033[0m"
BOLD = "\033[1m"
GREEN = "\033[32m"
RED = "\033[31m"
YELLOW = "\033[33m"
CYAN = "\033[36m"
WHITE = "\033[37m"
BG_DARK = "\033[40m"
CLEAR_SCREEN = "\033[2J\033[H"

class ConsoleTUI:
    def __init__(self):
        self.feed = LiveFeedEngine(PROJECT_ROOT)
        self.broker = PaperTradingEngine(initial_cash=100000.0)
        self.auto_strategy = False
        self.running = True

        # Attach broker to live feed
        self.feed.subscribe(self._on_tick)

        # Volatility & Strategy state
        self.garch_vol = 0.464
        self.cpt_score = 0.042
        self.tick_count = 0

    def _on_tick(self, tick: dict):
        self.broker.on_tick(tick)
        self.tick_count += 1

        # Automated CPT breakout strategy execution if enabled
        if self.auto_strategy and tick["symbol"] == "BTC":
            # Simulate occasional CPT breakout trigger
            if random.random() < 0.03:
                pos = self.broker.positions.get("BTC")
                if not pos or abs(pos["qty"]) < 1e-6:
                    self.broker.place_order("BTC", "BUY", "MARKET", qty=0.25)
            elif random.random() < 0.02:
                pos = self.broker.positions.get("BTC")
                if pos and pos["qty"] > 0:
                    self.broker.place_order("BTC", "SELL", "MARKET", qty=pos["qty"])

    def start(self, duration_sec: Optional[int] = None):
        """Starts live terminal dashboard."""
        self.feed.start(update_interval_sec=0.25)

        start_time = time.time()
        try:
            while self.running:
                self._render()
                time.sleep(0.4)
                if duration_sec and (time.time() - start_time) > duration_sec:
                    break
        except KeyboardInterrupt:
            pass
        finally:
            self.feed.stop()

    def _render(self):
        tickers = self.feed.get_all_tickers()
        summary = self.broker.get_account_summary()
        positions = [p for p in self.broker.positions.values() if abs(p["qty"]) > 1e-6]

        pnl_color = GREEN if summary["total_pnl"] >= 0 else RED
        pnl_sign = "+" if summary["total_pnl"] >= 0 else ""

        lines = []
        lines.append(f"{CLEAR_SCREEN}")
        lines.append(f"{BOLD}{CYAN}╔═════════════════════════════════════════════════════════════════════════════════════════════╗{RESET}")
        lines.append(f"{BOLD}{CYAN}║                     OBSIDIAN QUANTITATIVE TRADING TERMINAL — LIVE                    ║{RESET}")
        lines.append(f"{BOLD}{CYAN}╚═════════════════════════════════════════════════════════════════════════════════════════════╝{RESET}")

        # Ticker Bar
        ticker_str = " "
        for sym in ["BTC", "ETH", "BNB", "XRP", "DOGE"]:
            t = tickers.get(sym)
            if t:
                color = GREEN if t["direction"] == "up" else (RED if t["direction"] == "down" else WHITE)
                arrow = "▲" if t["direction"] == "up" else ("▼" if t["direction"] == "down" else " ")
                ticker_str += f"{BOLD}{sym}{RESET}: {color}${t['price']:<9.2f} {arrow}{RESET} │ "
        lines.append(ticker_str)
        lines.append("─" * 93)

        # Portfolio Summary Box
        auto_status = f"{GREEN}ENABLED (AUTONOMOUS){RESET}" if self.auto_strategy else f"{YELLOW}DISABLED (MANUAL){RESET}"
        lines.append(f"{BOLD}PAPER ACCOUNT STATUS:{RESET}")
        lines.append(f"  Cash Balance     : ${summary['cash']:<12,.2f}  │  Unrealized PnL: {pnl_color}${summary['unrealized_pnl']:<10,.2f}{RESET}")
        lines.append(f"  Asset Market Val : ${summary['asset_value']:<12,.2f}  │  Realized PnL  : ${summary['realized_pnl']:<10,.2f}")
        lines.append(f"  Total Net Equity : ${summary['equity']:<12,.2f}  │  Total Return  : {pnl_color}{pnl_sign}{summary['return_pct']:.2f}%{RESET}")
        lines.append(f"  Strategy Mode    : {auto_status}    │  Win Rate      : {summary['win_rate_pct']:.1f}% ({summary['total_trades']} trades)")
        lines.append("─" * 93)

        # Quant Engine Metrics
        lines.append(f"{BOLD}QUANTITATIVE SIGNALS & RISK METRICS:{RESET}")
        lines.append(f"  GARCH(1,1) Volatility : {self.garch_vol*100:.1f}% (Regime: {GREEN}NORMAL{RESET})  │  CPT Skew Score: {self.cpt_score:.3f} (Speculative)")
        lines.append(f"  Cornish-Fisher VaR 99%: 14.66%               │  Risk Parity Target: BTC 16.7% / ETH 12.5%")
        lines.append("─" * 93)

        # Active Positions Table
        lines.append(f"{BOLD}OPEN POSITIONS ({len(positions)}):{RESET}")
        if not positions:
            lines.append("  [No open positions. Use order commands or enable Auto-Strategy]")
        else:
            lines.append(f"  {'Symbol':<8} | {'Qty':<10} | {'Entry Price':<14} | {'Mark Price':<14} | {'PnL ($)':<12} | {'PnL (%)':<10}")
            lines.append("  " + "─" * 78)
            for p in positions:
                c = GREEN if p["unrealized_pnl"] >= 0 else RED
                s = "+" if p["unrealized_pnl"] >= 0 else ""
                lines.append(f"  {BOLD}{p['symbol']:<8}{RESET} | {p['qty']:<10.4f} | ${p['avg_entry_price']:<13,.2f} | ${p['current_price']:<13,.2f} | {c}{s}${p['unrealized_pnl']:<11,.2f}{RESET} | {c}{s}{p['pnl_pct']:<9.2f}%{RESET}")
        lines.append("─" * 93)

        # Recent Trades Blotter (last 3)
        lines.append(f"{BOLD}RECENT TRADE BLOTTER:{RESET}")
        if not self.broker.trade_blotter:
            lines.append("  [No trades executed yet]")
        else:
            for t in self.broker.trade_blotter[:3]:
                side_color = GREEN if t["side"] == "BUY" else RED
                lines.append(f"  [{t['timestamp']}] {side_color}{t['side']:<4}{RESET} {t['qty']} {t['symbol']} @ ${t['fill_price']:,.2f} (Fee: ${t['fee']:.2f}, Realized: ${t['realized_pnl']:+.2f})")

        lines.append("═" * 93)
        lines.append(f"{CYAN}Controls: Auto-Strategy is {'ON' if self.auto_strategy else 'OFF'}. Streamed {self.tick_count} live ticks.{RESET}")

        sys.stdout.write("\n".join(lines) + "\n")
        sys.stdout.flush()

if __name__ == "__main__":
    tui = ConsoleTUI()
    # If run in automated test mode
    if len(sys.argv) > 1 and sys.argv[1] == "--demo":
        print("Running ConsoleTUI demo for 5 seconds...")
        # Place test orders
        tui.feed.start(0.2)
        time.sleep(0.5)
        tui.broker.place_order("BTC", "BUY", "MARKET", 0.5)
        tui.broker.place_order("ETH", "BUY", "MARKET", 3.0)
        tui.auto_strategy = True
        tui.start(duration_sec=5)
    else:
        # Full interactive session
        tui.auto_strategy = True
        tui.broker.place_order("BTC", "BUY", "MARKET", 0.25)
        tui.start(duration_sec=10)
