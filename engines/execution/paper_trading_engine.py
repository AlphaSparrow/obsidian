#!/usr/bin/env python3
"""
Obsidian Execution Layer: High-Fidelity Paper Trading Broker & OMS/EMS Engine
Provides real-time simulated order matching against live market bid/ask spreads,
slippage modeling, fee accounting, margin tracking, and portfolio valuation.
"""

import time
import math
from typing import Dict, List, Optional

class PaperTradingEngine:
    def __init__(self, initial_cash: float = 100000.0, taker_fee_bps: float = 5.0, maker_fee_bps: float = 2.0):
        self.initial_cash = initial_cash
        self.cash = initial_cash
        self.realized_pnl = 0.0
        self.unrealized_pnl = 0.0
        self.taker_fee_rate = taker_fee_bps / 10000.0
        self.maker_fee_rate = maker_fee_bps / 10000.0

        # Portfolio state
        self.positions: Dict[str, dict] = {}
        self.open_orders: List[dict] = []
        self.trade_blotter: List[dict] = []

        # Order counters
        self._next_order_id = 5001
        self._next_trade_id = 8001

        # Latest market prices cache
        self.current_prices: Dict[str, dict] = {}

    def get_equity(self) -> float:
        """Total portfolio equity = cash balance + total mark-to-market position asset value."""
        asset_value = sum(p["market_value"] for p in self.positions.values() if abs(p["qty"]) > 1e-6)
        return self.cash + asset_value

    def get_account_summary(self) -> dict:
        """Returns comprehensive balance and performance statistics."""
        asset_value = sum(p["market_value"] for p in self.positions.values() if abs(p["qty"]) > 1e-6)
        equity = self.cash + asset_value
        total_pnl = equity - self.initial_cash
        return_pct = (total_pnl / self.initial_cash) * 100.0

        winning_trades = [t for t in self.trade_blotter if t.get("realized_pnl", 0) > 0]
        losing_trades = [t for t in self.trade_blotter if t.get("realized_pnl", 0) < 0]

        gross_profit = sum(t["realized_pnl"] for t in winning_trades)
        gross_loss = abs(sum(t["realized_pnl"] for t in losing_trades))
        profit_factor = (gross_profit / gross_loss) if gross_loss > 0 else (99.9 if gross_profit > 0 else 1.0)
        win_rate = (len(winning_trades) / (len(winning_trades) + len(losing_trades)) * 100.0) if (winning_trades or losing_trades) else 0.0

        return {
            "initial_cash": self.initial_cash,
            "cash": round(self.cash, 2),
            "asset_value": round(asset_value, 2),
            "equity": round(equity, 2),
            "realized_pnl": round(self.realized_pnl, 2),
            "unrealized_pnl": round(self.unrealized_pnl, 2),
            "total_pnl": round(total_pnl, 2),
            "return_pct": round(return_pct, 2),
            "open_positions_count": len([p for p in self.positions.values() if abs(p["qty"]) > 1e-6]),
            "total_trades": len(self.trade_blotter),
            "win_rate_pct": round(win_rate, 1),
            "profit_factor": round(profit_factor, 2)
        }

    def on_tick(self, tick: dict):
        """Processes an incoming market tick to update mark-to-market positions and match resting orders."""
        sym = tick["symbol"]
        price = tick["price"]
        bid = tick.get("bid", price)
        ask = tick.get("ask", price)

        self.current_prices[sym] = {"price": price, "bid": bid, "ask": ask}

        # 1. Update mark-to-market for existing position
        if sym in self.positions:
            pos = self.positions[sym]
            if abs(pos["qty"]) > 1e-6:
                pos["current_price"] = price
                pos["market_value"] = pos["qty"] * price
                if pos["qty"] > 0:
                    pos["unrealized_pnl"] = pos["qty"] * (bid - pos["avg_entry_price"])
                else:
                    pos["unrealized_pnl"] = abs(pos["qty"]) * (pos["avg_entry_price"] - ask)

                cost_basis = abs(pos["qty"]) * pos["avg_entry_price"]
                pos["pnl_pct"] = (pos["unrealized_pnl"] / cost_basis * 100.0) if cost_basis > 0 else 0.0

        # Recalculate total unrealized PnL
        self.unrealized_pnl = sum(p["unrealized_pnl"] for p in self.positions.values() if abs(p["qty"]) > 1e-6)

        # 2. Check resting limit / stop orders
        remaining_orders = []
        for order in self.open_orders:
            if order["symbol"] != sym:
                remaining_orders.append(order)
                continue

            filled = False
            fill_price = 0.0

            if order["type"] == "LIMIT":
                if order["side"] == "BUY" and ask <= order["price"]:
                    filled = True
                    fill_price = ask
                elif order["side"] == "SELL" and bid >= order["price"]:
                    filled = True
                    fill_price = bid
            elif order["type"] == "STOP":
                if order["side"] == "SELL" and bid <= order["price"]:
                    filled = True
                    fill_price = bid
                elif order["side"] == "BUY" and ask >= order["price"]:
                    filled = True
                    fill_price = ask

            if filled:
                self._execute_fill(order["symbol"], order["side"], order["qty"], fill_price, is_maker=True)
            else:
                remaining_orders.append(order)

        self.open_orders = remaining_orders

    def place_order(self, symbol: str, side: str, order_type: str = "MARKET", qty: float = 1.0, price: Optional[float] = None) -> dict:
        """Places a paper trading order."""
        side = side.upper()
        order_type = order_type.upper()

        if qty <= 0:
            return {"status": "ERROR", "message": "Quantity must be greater than 0"}

        market_info = self.current_prices.get(symbol)
        if not market_info:
            return {"status": "ERROR", "message": f"No market price available for {symbol}"}

        # Market Order: immediate fill
        if order_type == "MARKET":
            fill_price = market_info["ask"] if side == "BUY" else market_info["bid"]
            # Apply slight execution slippage (0.5 bps)
            slippage = fill_price * 0.00005 * (1 if side == "BUY" else -1)
            fill_price += slippage

            trade = self._execute_fill(symbol, side, qty, fill_price, is_maker=False)
            return {"status": "FILLED", "order_id": trade["trade_id"], "trade": trade}

        # Limit / Stop Order: place in book
        elif order_type in ("LIMIT", "STOP"):
            if price is None or price <= 0:
                return {"status": "ERROR", "message": f"Valid price required for {order_type} order"}

            order_id = self._next_order_id
            self._next_order_id += 1

            order = {
                "order_id": order_id,
                "symbol": symbol,
                "side": side,
                "type": order_type,
                "qty": qty,
                "price": price,
                "created_at": time.strftime("%H:%M:%S")
            }
            self.open_orders.append(order)
            return {"status": "ACCEPTED", "order_id": order_id, "order": order}

        return {"status": "ERROR", "message": f"Unsupported order type: {order_type}"}

    def _execute_fill(self, symbol: str, side: str, qty: float, fill_price: float, is_maker: bool = False) -> dict:
        """Internal fill execution and portfolio rebalancing."""
        fee_rate = self.maker_fee_rate if is_maker else self.taker_fee_rate
        notional = qty * fill_price
        fee = notional * fee_rate

        trade_pnl = 0.0
        pos = self.positions.get(symbol, {
            "symbol": symbol,
            "qty": 0.0,
            "avg_entry_price": 0.0,
            "current_price": fill_price,
            "market_value": 0.0,
            "unrealized_pnl": 0.0,
            "pnl_pct": 0.0
        })

        if side == "BUY":
            self.cash -= (notional + fee)
            if pos["qty"] >= 0: # Adding to long position
                new_qty = pos["qty"] + qty
                pos["avg_entry_price"] = ((pos["qty"] * pos["avg_entry_price"]) + (qty * fill_price)) / new_qty
                pos["qty"] = new_qty
            else: # Covering short position
                cover_qty = min(abs(pos["qty"]), qty)
                trade_pnl = cover_qty * (pos["avg_entry_price"] - fill_price)
                self.realized_pnl += (trade_pnl - fee)
                pos["qty"] += qty
                if abs(pos["qty"]) < 1e-6:
                    pos["qty"] = 0.0
                    pos["avg_entry_price"] = 0.0
                    pos["market_value"] = 0.0
                    pos["unrealized_pnl"] = 0.0
                    pos["pnl_pct"] = 0.0
        else: # SELL
            self.cash += (notional - fee)
            if pos["qty"] <= 0: # Adding to short position
                new_qty = pos["qty"] - qty
                pos["avg_entry_price"] = ((abs(pos["qty"]) * pos["avg_entry_price"]) + (qty * fill_price)) / abs(new_qty)
                pos["qty"] = new_qty
            else: # Closing long position
                close_qty = min(pos["qty"], qty)
                trade_pnl = close_qty * (fill_price - pos["avg_entry_price"])
                self.realized_pnl += (trade_pnl - fee)
                pos["qty"] -= qty
                if abs(pos["qty"]) < 1e-6:
                    pos["qty"] = 0.0
                    pos["avg_entry_price"] = 0.0
                    pos["market_value"] = 0.0
                    pos["unrealized_pnl"] = 0.0
                    pos["pnl_pct"] = 0.0

        pos["current_price"] = fill_price
        pos["market_value"] = pos["qty"] * fill_price
        self.positions[symbol] = pos
        self.unrealized_pnl = sum(p["unrealized_pnl"] for p in self.positions.values() if abs(p["qty"]) > 1e-6)

        trade_id = self._next_trade_id
        self._next_trade_id += 1

        trade_entry = {
            "trade_id": trade_id,
            "timestamp": time.strftime("%H:%M:%S"),
            "symbol": symbol,
            "side": side,
            "qty": qty,
            "fill_price": round(fill_price, 4),
            "notional": round(notional, 2),
            "fee": round(fee, 4),
            "realized_pnl": round(trade_pnl, 2)
        }
        self.trade_blotter.insert(0, trade_entry) # Most recent first
        return trade_entry

    def close_position(self, symbol: str) -> dict:
        """Closes an open position at current market price."""
        pos = self.positions.get(symbol)
        if not pos or abs(pos["qty"]) < 1e-6:
            return {"status": "ERROR", "message": f"No open position in {symbol}"}

        side = "SELL" if pos["qty"] > 0 else "BUY"
        qty = abs(pos["qty"])
        return self.place_order(symbol, side, "MARKET", qty)

    def close_all_positions(self) -> List[dict]:
        """Liquidates all open positions."""
        results = []
        for sym, pos in list(self.positions.items()):
            if abs(pos["qty"]) > 1e-6:
                results.append(self.close_position(sym))
        return results

    def reset_account(self, initial_cash: Optional[float] = None):
        """Resets paper account to fresh initial state."""
        if initial_cash:
            self.initial_cash = initial_cash
        self.cash = self.initial_cash
        self.realized_pnl = 0.0
        self.unrealized_pnl = 0.0
        self.positions.clear()
        self.open_orders.clear()
        self.trade_blotter.clear()

if __name__ == "__main__":
    broker = PaperTradingEngine(initial_cash=100000.0)
    # Simulate tick
    broker.on_tick({"symbol": "BTC", "price": 75000.0, "bid": 74990.0, "ask": 75010.0})
    print("Initial Account:", broker.get_account_summary())

    # Buy 0.5 BTC
    res = broker.place_order("BTC", "BUY", "MARKET", 0.5)
    print("\nPlaced Market Buy:", res)
    print("Account Post Buy:", broker.get_account_summary())

    # BTC price moves to $76,500
    broker.on_tick({"symbol": "BTC", "price": 76500.0, "bid": 76490.0, "ask": 76510.0})
    print("\nAccount After BTC Gains:", broker.get_account_summary())

    # Close position
    res_close = broker.close_position("BTC")
    print("\nClosed Position:", res_close)
    print("Final Account State:", broker.get_account_summary())
