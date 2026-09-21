/* =================================================================
   OBSIDIAN TERMINAL — Application Logic
   =================================================================
   Handles: live clock, session ID, system log, navigation state,
   module card interactions, and keyboard shortcuts.
   ================================================================= */

(function () {
    'use strict';

    /* ─── CONSTANTS ──────────────────────────────────────────── */

    const MONTHS = [
        'JAN', 'FEB', 'MAR', 'APR', 'MAY', 'JUN',
        'JUL', 'AUG', 'SEP', 'OCT', 'NOV', 'DEC'
    ];

    const FKEY_MODULE_MAP = {
        'F1': null,          // HELP (no module)
        'F2': null,          // SEARCH
        'F3': 'news',
        'F4': 'portfolio',
        'F5': 'markets',
        'F6': 'risk',
        'F7': 'analytics',
        'F8': null,          // OPTIONS
    };

    const LOG_BOOT_MESSAGES = [
        { level: 'SYS', msg: 'Obsidian Terminal v0.1.0 initialized' },
        { level: 'SYS', msg: 'WebView2 runtime active' },
        { level: 'SYS', msg: 'Render pipeline ready' },
        { level: 'INF', msg: 'All modules standing by' },
        { level: 'SYS', msg: 'Session {SESSION} established' },
        { level: 'WRN', msg: 'Awaiting data feed connection\u2026' },
    ];


    /* ─── STATE ──────────────────────────────────────────────── */

    let activeModule = 'dashboard';
    let sessionId = generateSessionId();


    /* ─── UTILITIES ──────────────────────────────────────────── */

    function generateSessionId() {
        const chars = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
        let id = 'OBS-';
        for (let i = 0; i < 4; i++) {
            id += chars[Math.floor(Math.random() * chars.length)];
        }
        return id;
    }

    function pad(n) {
        return n < 10 ? '0' + n : '' + n;
    }

    function formatTime(date) {
        return pad(date.getHours()) + ':' + pad(date.getMinutes()) + ':' + pad(date.getSeconds());
    }

    function formatDate(date) {
        return pad(date.getDate()) + ' ' + MONTHS[date.getMonth()] + ' ' + date.getFullYear();
    }


    /* ─── CLOCK ──────────────────────────────────────────────── */

    function tickClock() {
        const now = new Date();
        const timeStr = formatTime(now);
        const dateStr = formatDate(now);

        const headerClock = document.getElementById('header-clock');
        const statusTime = document.getElementById('status-time');
        const statusDate = document.getElementById('status-date');

        if (headerClock) headerClock.textContent = timeStr;
        if (statusTime)  statusTime.textContent  = timeStr;
        if (statusDate)  statusDate.textContent   = dateStr;
    }


    /* ─── SESSION ────────────────────────────────────────────── */

    function initSession() {
        const el = document.getElementById('session-id');
        if (el) el.textContent = 'SESSION: ' + sessionId;
    }


    /* ─── SYSTEM LOG ─────────────────────────────────────────── */

    function createLogEntry(level, time, msg) {
        const entry = document.createElement('div');
        entry.className = 'log-entry';
        entry.innerHTML =
            '<span class="log-entry__level log-entry__level--' + level.toLowerCase() + '">' + level + '</span>' +
            '<span class="log-entry__time">' + time + '</span>' +
            '<span class="log-entry__sep">\u2500\u2500</span>' +
            '<span class="log-entry__msg">' + msg + '</span>';
        return entry;
    }

    function initSystemLog() {
        const body = document.getElementById('log-body');
        const countEl = document.getElementById('log-count');
        if (!body) return;

        const now = new Date();
        const timeStr = formatTime(now);

        LOG_BOOT_MESSAGES.forEach(function (item, index) {
            const msg = item.msg.replace('{SESSION}', sessionId);
            const entry = createLogEntry(item.level, timeStr, msg);
            entry.style.animationDelay = (index * 0.06) + 's';
            body.appendChild(entry);
        });

        if (countEl) countEl.textContent = '' + LOG_BOOT_MESSAGES.length;
    }

    function appendLog(level, msg) {
        const body = document.getElementById('log-body');
        const countEl = document.getElementById('log-count');
        if (!body) return;

        const entry = createLogEntry(level, formatTime(new Date()), msg);
        body.appendChild(entry);

        // Auto-scroll to bottom
        body.scrollTop = body.scrollHeight;

        // Update count
        if (countEl) {
            countEl.textContent = '' + body.children.length;
        }
    }


    /* ─── NAVIGATION ─────────────────────────────────────────── */

    function setActiveModule(moduleName) {
        if (moduleName === activeModule) return;

        // Update nav rail
        var navBtns = document.querySelectorAll('.nav-btn');
        navBtns.forEach(function (btn) {
            if (btn.dataset.module === moduleName) {
                btn.classList.add('nav-btn--active');
            } else {
                btn.classList.remove('nav-btn--active');
            }
        });

        // Update module cards
        var cards = document.querySelectorAll('.mod-card');
        cards.forEach(function (card) {
            if (card.dataset.module === moduleName) {
                card.classList.add('mod-card--selected');
            } else {
                card.classList.remove('mod-card--selected');
            }
        });

        var prevModule = activeModule;
        activeModule = moduleName;

        // Log navigation
        if (moduleName !== 'dashboard') {
            appendLog('INF', 'Navigated to ' + moduleName.toUpperCase() + ' module');
        }
    }

    function initNavigation() {
        // Nav rail clicks
        var navBtns = document.querySelectorAll('.nav-btn');
        navBtns.forEach(function (btn) {
            btn.addEventListener('click', function () {
                var mod = this.dataset.module;
                if (mod) setActiveModule(mod);
            });
        });

        // Module card clicks
        var cards = document.querySelectorAll('.mod-card');
        cards.forEach(function (card) {
            card.addEventListener('click', function () {
                var mod = this.dataset.module;
                if (mod) setActiveModule(mod);
            });
        });
    }


    /* ─── KEYBOARD SHORTCUTS ─────────────────────────────────── */

    function initKeyboard() {
        document.addEventListener('keydown', function (e) {
            // Function keys
            var fkey = e.key;  // 'F1', 'F2', etc.
            if (FKEY_MODULE_MAP.hasOwnProperty(fkey)) {
                e.preventDefault();
                var moduleName = FKEY_MODULE_MAP[fkey];

                // Visual feedback on fkey button
                var fkeyBtn = document.querySelector('.fkey[data-key="' + fkey + '"]');
                if (fkeyBtn) {
                    fkeyBtn.style.filter = 'brightness(1.6)';
                    setTimeout(function () {
                        fkeyBtn.style.filter = '';
                    }, 150);
                }

                if (moduleName) {
                    setActiveModule(moduleName);
                } else if (fkey === 'F1') {
                    appendLog('SYS', 'Help system not yet connected');
                } else if (fkey === 'F2') {
                    // Focus command input
                    var cmdInput = document.getElementById('cmd-input');
                    if (cmdInput) cmdInput.focus();
                }
                return;
            }

            // Escape — clear selection, return to dashboard
            if (e.key === 'Escape') {
                setActiveModule('dashboard');
                var cmdInput = document.getElementById('cmd-input');
                if (cmdInput && document.activeElement === cmdInput) {
                    cmdInput.blur();
                    cmdInput.value = '';
                }
            }
        });
    }


    /* ─── COMMAND INPUT ──────────────────────────────────────── */

    function initCommandInput() {
        var cmdInput = document.getElementById('cmd-input');
        if (!cmdInput) return;

        cmdInput.addEventListener('keydown', function (e) {
            if (e.key === 'Enter') {
                var cmd = this.value.trim().toUpperCase();
                if (cmd) {
                    appendLog('SYS', 'Command: ' + cmd);

                    // Simple command routing
                    var moduleNames = ['PORTFOLIO', 'MARKETS', 'ANALYTICS', 'RESEARCH', 'RISK', 'NEWS', 'SCREENER', 'ORDERS', 'WATCHLIST', 'SETTINGS', 'DASHBOARD'];
                    var moduleKeys  = ['portfolio', 'markets', 'analytics', 'research', 'risk', 'news', 'screener', 'orders', 'watchlist', 'settings', 'dashboard'];

                    var idx = moduleNames.indexOf(cmd);
                    if (idx !== -1) {
                        setActiveModule(moduleKeys[idx]);
                        appendLog('INF', 'Switched to ' + cmd);
                    } else if (cmd === 'HELP' || cmd === '?') {
                        appendLog('SYS', 'Available: PORTFOLIO, MARKETS, ANALYTICS, RESEARCH, RISK, NEWS, SCREENER, ORDERS, WATCHLIST');
                    } else if (cmd === 'CLEAR' || cmd === 'CLS') {
                        var body = document.getElementById('log-body');
                        if (body) body.innerHTML = '';
                        var countEl = document.getElementById('log-count');
                        if (countEl) countEl.textContent = '0';
                    } else {
                        appendLog('WRN', 'Unknown command: ' + cmd);
                    }

                    this.value = '';
                }
            }
        });
    }


    /* ─── FUNCTION KEY BUTTONS ───────────────────────────────── */

    function initFunctionKeys() {
        var fkeys = document.querySelectorAll('.fkey');
        fkeys.forEach(function (btn) {
            btn.addEventListener('click', function () {
                var key = this.dataset.key;
                if (!key) return;

                // Simulate keydown
                var event = new KeyboardEvent('keydown', {
                    key: key,
                    bubbles: true,
                    cancelable: true
                });
                document.dispatchEvent(event);
            });
        });
    }


    /* ─── INIT ───────────────────────────────────────────────── */

    function init() {
        initSession();
        tickClock();
        initSystemLog();
        initNavigation();
        initKeyboard();
        initCommandInput();
        initFunctionKeys();

        // Start clock ticker (updates every second)
        setInterval(tickClock, 1000);
    }

    // Boot when DOM is ready
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }

})();
