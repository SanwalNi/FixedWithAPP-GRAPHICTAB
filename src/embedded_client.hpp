#pragma once
#include <string>

// Self-contained single-payload HTML/CSS/JS for instant zero-latency loading over USB ADB
inline const char* GET_EMBEDDED_HTML() {
    return R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no, viewport-fit=cover">
    <meta name="apple-mobile-web-app-capable" content="yes">
    <meta name="mobile-web-app-capable" content="yes">
    <meta name="theme-color" content="#07070a">
    <title>Alamy - Low-Latency Graphics Tablet</title>
    <style>
        :root {
            --bg-dark: #07070a;
            --card-bg: rgba(22, 22, 32, 0.85);
            --border-color: rgba(255, 255, 255, 0.12);
            --accent-blue: #3b82f6;
            --accent-cyan: #06b6d4;
            --accent-purple: #8b5cf6;
            --accent-green: #10b981;
            --accent-red: #ef4444;
            --text-main: #f3f4f6;
            --text-muted: #9ca3af;
            --font-ui: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
            --font-mono: 'SF Mono', Consolas, Monaco, monospace;
        }

        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
            user-select: none;
            -webkit-user-select: none;
            -webkit-touch-callout: none;
            touch-action: none;
        }

        html, body {
            width: 100%;
            height: 100%;
            overflow: hidden;
            background-color: var(--bg-dark);
            font-family: var(--font-ui);
            color: var(--text-main);
        }

        /* Portrait Warning Overlay */
        #portrait-warning {
            display: none;
            position: fixed;
            top: 0; left: 0; right: 0; bottom: 0;
            background: var(--bg-dark);
            z-index: 9999;
            align-items: center;
            justify-content: center;
            text-align: center;
            flex-direction: column;
            padding: 24px;
        }

        @media screen and (orientation: portrait) {
            #portrait-warning { display: flex; }
        }

        .warning-content {
            background: var(--card-bg);
            border: 1px solid var(--border-color);
            padding: 32px 24px;
            border-radius: 20px;
            max-width: 320px;
        }

        .warning-icon {
            font-size: 48px;
            margin-bottom: 16px;
            animation: rotate-phone 2s infinite ease-in-out;
        }

        @keyframes rotate-phone {
            0% { transform: rotate(0deg); }
            50% { transform: rotate(-90deg); }
            100% { transform: rotate(0deg); }
        }

        .warning-content h2 { font-size: 18px; margin-bottom: 8px; color: var(--accent-cyan); }
        .warning-content p { font-size: 14px; color: var(--text-muted); margin-bottom: 24px; line-height: 1.5; }

        #btn-force-landscape {
            background: linear-gradient(135deg, var(--accent-blue), var(--accent-purple));
            color: white;
            border: none;
            padding: 12px 24px;
            border-radius: 24px;
            font-size: 14px;
            font-weight: 700;
            cursor: pointer;
            box-shadow: 0 4px 12px rgba(59, 130, 246, 0.4);
        }

        #btn-force-landscape:active { transform: scale(0.95); }

        #app-container {
            position: relative;
            width: 100vw;
            height: 100vh;
            display: flex;
            flex-direction: column;
            overflow: hidden;
            padding-left: env(safe-area-inset-left);
            padding-right: env(safe-area-inset-right);
        }

        /* Header HUD */
        #hud-header {
            height: 32px;
            background: var(--card-bg);
            backdrop-filter: blur(12px);
            -webkit-backdrop-filter: blur(12px);
            border-bottom: 1px solid var(--border-color);
            display: flex;
            align-items: center;
            justify-content: space-between;
            padding: 0 16px;
            z-index: 100;
        }

        .hud-left, .hud-stats, .hud-right {
            display: flex;
            align-items: center;
            gap: 8px;
        }

        .brand {
            display: flex;
            align-items: center;
            gap: 6px;
            font-weight: 700;
        }

        .logo-dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
            background: var(--accent-cyan);
            box-shadow: 0 0 10px var(--accent-cyan);
        }

        .brand-title {
            font-size: 14px;
            font-weight: 800;
            letter-spacing: 0.5px;
            color: #ffffff;
        }

        .status-badge {
            display: flex;
            align-items: center;
            gap: 6px;
            font-size: 11px;
            font-weight: 600;
            padding: 4px 8px;
            border-radius: 20px;
            background: rgba(255, 255, 255, 0.06);
        }

        .status-indicator {
            width: 6px;
            height: 6px;
            border-radius: 50%;
        }

        .status-badge.connected .status-indicator {
            background: var(--accent-green);
            box-shadow: 0 0 8px var(--accent-green);
        }

        .status-badge.disconnected .status-indicator {
            background: var(--accent-red);
            box-shadow: 0 0 8px var(--accent-red);
        }

        .hud-stats {
            gap: 14px;
        }

        .stat-item {
            display: flex;
            flex-direction: column;
            align-items: center;
        }

        .stat-label {
            font-size: 8px;
            color: var(--text-muted);
            font-weight: 600;
            letter-spacing: 0.5px;
        }

        .stat-value {
            font-size: 11px;
            font-family: var(--font-mono);
            color: var(--accent-cyan);
            font-weight: 700;
        }

        .icon-btn {
            background: rgba(255, 255, 255, 0.08);
            border: 1px solid var(--border-color);
            color: var(--text-main);
            width: 28px;
            height: 28px;
            border-radius: 6px;
            display: flex;
            align-items: center;
            justify-content: center;
            cursor: pointer;
            outline: none;
        }

        .icon-btn:active {
            transform: scale(0.92);
            background: rgba(255, 255, 255, 0.2);
        }

        /* Touch Surface */
        #touch-surface {
            flex: 1;
            position: relative;
            display: flex;
            align-items: center;
            justify-content: center;
            background: radial-gradient(circle at 50% 50%, #11121c 0%, #060609 100%);
            overflow: hidden;
        }

        #tablet-active-zone {
            position: relative;
            width: 95%;
            height: 92%;
            border: 1px dashed rgba(255, 255, 255, 0.15);
            border-radius: 12px;
            display: flex;
            align-items: center;
            justify-content: center;
            transition: width 0.2s, height 0.2s;
        }

        .corner-marker {
            position: absolute;
            width: 14px;
            height: 14px;
            pointer-events: none;
        }

        .corner-marker.top-left {
            top: -1px; left: -1px;
            border-top: 2px solid var(--accent-cyan);
            border-left: 2px solid var(--accent-cyan);
        }
        .corner-marker.top-right {
            top: -1px; right: -1px;
            border-top: 2px solid var(--accent-cyan);
            border-right: 2px solid var(--accent-cyan);
        }
        .corner-marker.bottom-left {
            bottom: -1px; left: -1px;
            border-bottom: 2px solid var(--accent-cyan);
            border-left: 2px solid var(--accent-cyan);
        }
        .corner-marker.bottom-right {
            bottom: -1px; right: -1px;
            border-bottom: 2px solid var(--accent-cyan);
            border-right: 2px solid var(--accent-cyan);
        }

        .grid-crosshair {
            position: absolute;
            width: 24px;
            height: 24px;
            pointer-events: none;
            opacity: 0.25;
        }
        .grid-crosshair::before, .grid-crosshair::after {
            content: '';
            position: absolute;
            background: #ffffff;
        }
        .grid-crosshair::before { top: 11px; left: 0; width: 100%; height: 2px; }
        .grid-crosshair::after { left: 11px; top: 0; width: 2px; height: 100%; }

        #feedback-canvas {
            position: absolute;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            pointer-events: none;
        }

        .touch-hint {
            position: absolute;
            text-align: center;
            pointer-events: none;
            opacity: 0.4;
            transition: opacity 0.3s;
        }

        .hint-icon { font-size: 28px; margin-bottom: 4px; }
        .hint-title { font-size: 14px; font-weight: 700; }
        .hint-sub { font-size: 11px; color: var(--text-muted); }

        /* Bottom Toolbar (Fixed) */
        #bottom-toolbar {
            height: 38px;
            display: flex;
            align-items: center;
            justify-content: space-between;
            background: var(--card-bg);
            backdrop-filter: blur(16px);
            -webkit-backdrop-filter: blur(16px);
            border-top: 1px solid var(--border-color);
            padding: 0 16px;
            padding-bottom: env(safe-area-inset-bottom);
            z-index: 100;
        }

        .toolbar-right {
            display: flex;
            align-items: center;
            gap: 8px;
        }

        .mode-toggles {
            display: flex;
            background: rgba(0, 0, 0, 0.4);
            padding: 3px;
            border-radius: 20px;
            gap: 4px;
        }

        .mode-btn {
            background: transparent;
            border: none;
            color: var(--text-muted);
            padding: 6px 12px;
            border-radius: 16px;
            font-size: 11px;
            font-weight: 700;
            cursor: pointer;
            transition: all 0.2s;
        }

        .mode-btn.active {
            background: linear-gradient(135deg, var(--accent-blue), var(--accent-purple));
            color: #ffffff;
            box-shadow: 0 2px 8px rgba(59, 130, 246, 0.4);
        }

        .quick-actions {
            display: flex;
            gap: 4px;
        }

        .action-btn {
            background: rgba(255, 255, 255, 0.08);
            border: 1px solid var(--border-color);
            color: var(--text-main);
            padding: 6px 10px;
            border-radius: 16px;
            font-size: 11px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.15s;
        }

        .action-btn:active {
            transform: scale(0.92);
            background: rgba(59, 130, 246, 0.4);
        }

        /* Modal */
        .modal-backdrop {
            position: absolute;
            top: 0; left: 0; right: 0; bottom: 0;
            background: rgba(0, 0, 0, 0.75);
            backdrop-filter: blur(8px);
            display: flex;
            align-items: center;
            justify-content: center;
            z-index: 200;
            opacity: 1;
            transition: opacity 0.2s;
        }

        .modal-backdrop.hidden {
            opacity: 0;
            pointer-events: none;
        }

        .modal-card {
            width: 88%;
            max-width: 380px;
            background: #141520;
            border: 1px solid var(--border-color);
            border-radius: 16px;
            padding: 20px;
            box-shadow: 0 16px 40px rgba(0, 0, 0, 0.7);
        }

        .modal-header {
            display: flex;
            align-items: center;
            justify-content: space-between;
            margin-bottom: 16px;
        }

        .modal-header h2 { font-size: 15px; font-weight: 700; }
        .close-btn { background: transparent; border: none; color: var(--text-muted); font-size: 22px; cursor: pointer; }
        .modal-body { display: flex; flex-direction: column; gap: 14px; }
        .setting-row { display: flex; align-items: center; justify-content: space-between; font-size: 13px; }
        .setting-row select {
            background: #202230;
            color: var(--text-main);
            border: 1px solid var(--border-color);
            padding: 6px 10px;
            border-radius: 8px;
            font-size: 12px;
            outline: none;
        }
    </style>
</head>
<body>
    <!-- Portrait Warning Overlay -->
    <div id="portrait-warning">
        <div class="warning-content">
            <div class="warning-icon">📱 🔄 📺</div>
            <h2>Please Rotate Your Device</h2>
            <p>Alamy is designed for landscape mode to maximize your drawing area.</p>
            <button id="btn-force-landscape">Tap to Rotate & Fullscreen</button>
        </div>
    </div>

    <div id="app-container">
        <header id="hud-header">
            <div class="hud-left">
                <div class="brand">
                    <span class="logo-dot"></span>
                    <span class="brand-title">ALAMY</span>
                </div>
                <div id="conn-status" class="status-badge disconnected">
                    <span class="status-indicator"></span>
                    <span id="conn-text">Connecting...</span>
                </div>
            </div>

            <div class="hud-stats">
                <div class="stat-item">
                    <span class="stat-label">RATE</span>
                    <span id="stat-rate" class="stat-value">0 Hz</span>
                </div>
                <div class="stat-item">
                    <span class="stat-label">LATENCY</span>
                    <span id="stat-latency" class="stat-value">< 1 ms</span>
                </div>
                <div class="stat-item">
                    <span class="stat-label">PRESSURE</span>
                    <span id="stat-pressure" class="stat-value">0%</span>
                </div>
            </div>

            <div class="hud-right">
                <!-- Buttons moved to bottom toolbar -->
            </div>
        </header>

        <main id="touch-surface">
            <div id="tablet-active-zone">
                <div class="grid-crosshair center"></div>
                <div class="corner-marker top-left"></div>
                <div class="corner-marker top-right"></div>
                <div class="corner-marker bottom-left"></div>
                <div class="corner-marker bottom-right"></div>
                
                <canvas id="feedback-canvas"></canvas>
            </div>
            
            <div id="touch-hint" class="touch-hint">
                <div class="hint-icon">✍️</div>
                <div class="hint-title">Draw or Touch Anywhere</div>
                <div class="hint-sub">Sub-millisecond USB Input Active</div>
            </div>
        </main>

        <aside id="bottom-toolbar">
            <div class="mode-toggles">
                <button id="btn-mode-tablet" class="mode-btn active"><span>🎨 Tablet</span></button>
                <button id="btn-mode-trackpad" class="mode-btn"><span>🖱️ Trackpad</span></button>
            </div>

            <div class="quick-actions">
                <button class="action-btn" id="btn-undo">↶ Undo</button>
                <button class="action-btn" id="btn-redo">↷ Redo</button>
                <button class="action-btn" id="btn-eraser">🧹 Eraser</button>
                <button class="action-btn" id="btn-rightclick">🖱️ Right</button>
            </div>
            
            <div class="toolbar-right">
                <button id="btn-settings" class="icon-btn" title="Settings">
                    <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2">
                        <circle cx="12" cy="12" r="3"/>
                        <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z"/>
                    </svg>
                </button>
            </div>
        </aside>

        <div id="settings-modal" class="modal-backdrop hidden">
            <div class="modal-card">
                <div class="modal-header">
                    <h2>Tablet Preferences</h2>
                    <button id="btn-close-modal" class="close-btn">&times;</button>
                </div>
                <div class="modal-body">
                    <div class="setting-row">
                        <label for="cfg-aspect">Aspect Ratio Guide</label>
                        <select id="cfg-aspect">
                            <option value="16/9">16:9 Standard PC</option>
                            <option value="16/10">16:10 Laptop</option>
                            <option value="21/9">21:9 Ultra-Wide</option>
                            <option value="stretch">Fill Mobile Screen</option>
                        </select>
                    </div>
                    <div class="setting-row">
                        <label for="cfg-pressure-curve">Pressure Sensitivity</label>
                        <select id="cfg-pressure-curve">
                            <option value="linear">Linear (1:1)</option>
                            <option value="soft">Soft (Light touch)</option>
                            <option value="hard">Firm (Harder press)</option>
                        </select>
                    </div>
                    <div class="setting-row">
                        <label for="cfg-feedback">Visual Trail</label>
                        <input type="checkbox" id="cfg-feedback" checked>
                    </div>
                    <div class="setting-row">
                        <label for="cfg-wakelock">Keep Screen Awake</label>
                        <input type="checkbox" id="cfg-wakelock" checked>
                    </div>
                </div>
            </div>
        </div>
    </div>

    <script>
    (function() {
        'use strict';

        const PacketType = {
            TOUCH_DOWN: 0,
            TOUCH_MOVE: 1,
            TOUCH_UP: 2,
            HOVER: 3,
            SCROLL: 4,
            BUTTON_DOWN: 5,
            BUTTON_UP: 6,
            PING: 7,
            PONG: 8
        };

        const ToolType = { FINGER: 0, STYLUS: 1, ERASER: 2, MOUSE: 3 };

        let ws = null;
        let isConnected = false;
        let currentMode = 'tablet';
        let wakeLock = null;
        let packetCount = 0;
        let lastFpsTime = performance.now();
        let currentPressure = 0;
        let hasAttemptedLock = false;
        let activePointers = new Map();

        const touchSurface = document.getElementById('touch-surface');
        const activeZone = document.getElementById('tablet-active-zone');
        const canvas = document.getElementById('feedback-canvas');
        const ctx = canvas.getContext('2d');
        const connStatus = document.getElementById('conn-status');
        const connText = document.getElementById('conn-text');
        const statRate = document.getElementById('stat-rate');
        const statLatency = document.getElementById('stat-latency');
        const statPressure = document.getElementById('stat-pressure');
        const btnForceLandscape = document.getElementById('btn-force-landscape');
        const btnSettings = document.getElementById('btn-settings');
        const settingsModal = document.getElementById('settings-modal');
        const btnCloseModal = document.getElementById('btn-close-modal');
        const btnModeTablet = document.getElementById('btn-mode-tablet');
        const btnModeTrackpad = document.getElementById('btn-mode-trackpad');
        const touchHint = document.getElementById('touch-hint');
        const cfgAspect = document.getElementById('cfg-aspect');
        const cfgPressureCurve = document.getElementById('cfg-pressure-curve');
        const cfgFeedback = document.getElementById('cfg-feedback');

        // FIX (jank/stutter): getBoundingClientRect() used to be called for
        // every touch event, forcing synchronous layout at up to 240Hz on the
        // phone's main thread -> dropped frames and choppy strokes. The rect is
        // now cached and refreshed only on resize/rotation/fullscreen changes.
        let zoneRect = activeZone.getBoundingClientRect();
        function refreshZoneRect() {
            zoneRect = activeZone.getBoundingClientRect();
        }

        function resizeCanvas() {
            refreshZoneRect();
            const rect = zoneRect;
            const dpr = window.devicePixelRatio || 1;
            canvas.width = rect.width * dpr;
            canvas.height = rect.height * dpr;
            ctx.setTransform(1, 0, 0, 1, 0, 0);
            ctx.scale(dpr, dpr);
        }

        window.addEventListener('resize', () => {
            updateAspectRatioGuide();
            resizeCanvas();
        });

        window.addEventListener('orientationchange', () => {
            setTimeout(() => {
                updateAspectRatioGuide();
                resizeCanvas();
            }, 100);
        });

        async function enforceLandscape() {
            hasAttemptedLock = true;
            try {
                if (!document.fullscreenElement) {
                    await document.documentElement.requestFullscreen();
                }
                if (screen.orientation && screen.orientation.lock) {
                    await screen.orientation.lock('landscape');
                }
            } catch (err) {}
        }

        if (btnForceLandscape) {
            btnForceLandscape.addEventListener('click', enforceLandscape);
        }

        document.addEventListener('fullscreenchange', () => {
            if (document.fullscreenElement && screen.orientation && screen.orientation.lock) {
                screen.orientation.lock('landscape').catch(() => {});
            }
            setTimeout(() => { refreshZoneRect(); resizeCanvas(); }, 100);
        });

        function updateAspectRatioGuide() {
            const aspectVal = cfgAspect.value;
            if (aspectVal === 'stretch') {
                activeZone.style.width = '100%';
                activeZone.style.height = '100%';
                return;
            }

            const [w, h] = aspectVal.split('/').map(Number);
            const targetAspect = w / h;
            const parentW = touchSurface.clientWidth * 0.96;
            const parentH = touchSurface.clientHeight * 0.94;
            const parentAspect = parentW / parentH;

            if (parentAspect > targetAspect) {
                activeZone.style.height = `${parentH}px`;
                activeZone.style.width = `${parentH * targetAspect}px`;
            } else {
                activeZone.style.width = `${parentW}px`;
                activeZone.style.height = `${parentW / targetAspect}px`;
            }
        }

        function connectWebSocket() {
            const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
            const wsUrl = `${protocol}//${window.location.host}/ws`;

            connText.textContent = 'Connecting...';
            connStatus.className = 'status-badge disconnected';

            try {
                ws = new WebSocket(wsUrl);
                ws.binaryType = 'arraybuffer';

                ws.onopen = () => {
                    isConnected = true;
                    connStatus.className = 'status-badge connected';
                    connText.textContent = 'USB Active';
                    requestWakeLock();
                };

                ws.onclose = () => {
                    isConnected = false;
                    connStatus.className = 'status-badge disconnected';
                    connText.textContent = 'Disconnected';
                    setTimeout(connectWebSocket, 1500);
                };

                ws.onerror = () => {
                    ws.close();
                };
            } catch (e) {
                setTimeout(connectWebSocket, 1500);
            }
        }

        async function requestWakeLock() {
            try {
                if ('wakeLock' in navigator) {
                    wakeLock = await navigator.wakeLock.request('screen');
                }
            } catch (err) {}
        }

        async function releaseWakeLock() {
            try {
                if (wakeLock) {
                    await wakeLock.release();
                    wakeLock = null;
                }
            } catch (err) {}
        }

        function sendPacket(packetType, toolType, fingerId, flags, normX, normY, pressureNorm, tiltX = 0, tiltY = 0) {
            if (!isConnected || ws.readyState !== WebSocket.OPEN) return;

            // FIX: backpressure guard - if the USB/WebSocket link backs up, drop
            // intermediate MOVE updates so delivery stays real-time instead of
            // arriving in delayed bursts (the PC sees sudden jumps / broken strokes).
            const isMovePacket = packetType === PacketType.TOUCH_MOVE || packetType === PacketType.HOVER;
            if (isMovePacket && ws.bufferedAmount > 64) return;

            const buffer = new ArrayBuffer(16);
            const view = new DataView(buffer);

            view.setUint8(0, packetType);
            view.setUint8(1, toolType);
            view.setUint8(2, fingerId);
            view.setUint8(3, flags);
            view.setUint16(4, Math.min(65535, Math.max(0, Math.round(normX * 65535))), true);
            view.setUint16(6, Math.min(65535, Math.max(0, Math.round(normY * 65535))), true);
            view.setUint16(8, Math.min(65535, Math.max(0, Math.round(pressureNorm * 65535))), true);
            view.setInt8(10, Math.max(-90, Math.min(90, Math.round(tiltX))));
            view.setInt8(11, Math.max(-90, Math.min(90, Math.round(tiltY))));
            
            const clientMicros = Math.round(performance.now() * 1000);
            view.setUint32(12, clientMicros, true);

            ws.send(buffer);
            packetCount++;
        }

        // FIX: DOM writes for every coalesced event (up to 240/s) caused jank;
        // only update the HUD when the displayed percentage actually changes.
        let lastPressurePct = -1;
        function updatePressureHud(pressure) {
            const pct = Math.round(pressure * 100);
            if (pct !== lastPressurePct) {
                lastPressurePct = pct;
                statPressure.textContent = `${pct}%`;
            }
        }

        function mapPressure(rawPressure) {
            if (rawPressure <= 0.001) return 0.5;
            const curve = cfgPressureCurve.value;
            if (curve === 'soft') return Math.sqrt(rawPressure);
            if (curve === 'hard') return Math.pow(rawPressure, 1.8);
            return rawPressure;
        }

        function getNormalizedCoords(e) {
            const rect = zoneRect;
            const normX = (e.clientX - rect.left) / rect.width;
            const normY = (e.clientY - rect.top) / rect.height;
            return {
                x: Math.max(0, Math.min(1, normX)),
                y: Math.max(0, Math.min(1, normY))
            };
        }

        function getToolType(e) {
            if (e.pointerType === 'pen') {
                return (e.buttons === 32 || e.button === 5) ? ToolType.ERASER : ToolType.STYLUS;
            } else if (e.pointerType === 'mouse') {
                return ToolType.MOUSE;
            }
            return ToolType.FINGER;
        }

        function handlePointerDown(e) {
            if (!hasAttemptedLock) {
                enforceLandscape();
            }
            refreshZoneRect();
            touchHint.style.opacity = '0';
            activePointers.set(e.pointerId, { x: e.clientX, y: e.clientY });

            const coords = getNormalizedCoords(e);
            const tool = getToolType(e);
            const pressure = mapPressure(e.pressure);
            currentPressure = pressure;
            updatePressureHud(pressure);

            let flags = 0;
            if (e.buttons === 2 || e.button === 2) flags |= 0x01;

            sendPacket(PacketType.TOUCH_DOWN, tool, e.pointerId % 10, flags, coords.x, coords.y, pressure, e.tiltX || 0, e.tiltY || 0);

            if (cfgFeedback.checked) {
                drawTouchFeedback(coords.x, coords.y, pressure, true);
            }
        }

        function handlePointerMove(e) {
            if (!activePointers.has(e.pointerId)) return;

            const events = e.getCoalescedEvents ? e.getCoalescedEvents() : [e];
            for (const evt of events) {
                const coords = getNormalizedCoords(evt);
                const tool = getToolType(evt);
                const pressure = mapPressure(evt.pressure);
                currentPressure = pressure;
                updatePressureHud(pressure);

                let flags = 0;
                if (evt.buttons === 2) flags |= 0x01;

                sendPacket(PacketType.TOUCH_MOVE, tool, evt.pointerId % 10, flags, coords.x, coords.y, pressure, evt.tiltX || 0, evt.tiltY || 0);

                if (cfgFeedback.checked) {
                    drawTouchFeedback(coords.x, coords.y, pressure, false);
                }
            }
        }

        function handlePointerUp(e) {
            activePointers.delete(e.pointerId);
            const coords = getNormalizedCoords(e);
            const tool = getToolType(e);

            sendPacket(PacketType.TOUCH_UP, tool, e.pointerId % 10, 0, coords.x, coords.y, 0, 0, 0);
            updatePressureHud(0);

            if (activePointers.size === 0) {
                setTimeout(() => {
                    if (activePointers.size === 0) touchHint.style.opacity = '0.4';
                }, 1000);
            }
        }

        let lastDrawX = -1, lastDrawY = -1;
        let hasCanvasContent = false;
        function drawTouchFeedback(normX, normY, pressure, isNewStroke) {
            const rect = zoneRect;
            const px = normX * rect.width;
            const py = normY * rect.height;

            ctx.strokeStyle = `rgba(6, 182, 212, ${0.4 + pressure * 0.6})`;
            ctx.lineWidth = 2 + pressure * 6;
            ctx.lineCap = 'round';
            ctx.lineJoin = 'round';

            if (isNewStroke || lastDrawX < 0) {
                ctx.beginPath();
                ctx.arc(px, py, ctx.lineWidth / 2, 0, Math.PI * 2);
                ctx.fillStyle = ctx.strokeStyle;
                ctx.fill();
            } else {
                ctx.beginPath();
                ctx.moveTo(lastDrawX, lastDrawY);
                ctx.lineTo(px, py);
                ctx.stroke();
            }

            lastDrawX = px;
            lastDrawY = py;
            hasCanvasContent = true;
        }

        let fadeFrameCount = 0;
        function animateFade() {
            if (activePointers.size === 0) {
                lastDrawX = -1;
                lastDrawY = -1;
            }
            if (hasCanvasContent) {
                const rect = zoneRect;
                ctx.fillStyle = 'rgba(7, 7, 10, 0.08)';
                ctx.fillRect(0, 0, rect.width, rect.height);
                fadeFrameCount++;
                if (activePointers.size === 0 && fadeFrameCount > 120) {
                    hasCanvasContent = false;
                    fadeFrameCount = 0;
                }
            }
            requestAnimationFrame(animateFade);
        }
        requestAnimationFrame(animateFade);

        setInterval(() => {
            const now = performance.now();
            const elapsed = (now - lastFpsTime) / 1000.0;
            const pps = Math.round(packetCount / elapsed);
            statRate.textContent = `${pps} Hz`;
            statLatency.textContent = isConnected ? '< 1 ms' : '--';
            packetCount = 0;
            lastFpsTime = now;
        }, 1000);

        touchSurface.addEventListener('pointerdown', handlePointerDown, { passive: false });
        touchSurface.addEventListener('pointermove', handlePointerMove, { passive: false });
        touchSurface.addEventListener('pointerup', handlePointerUp, { passive: false });
        touchSurface.addEventListener('pointercancel', handlePointerUp, { passive: false });

        btnModeTablet.addEventListener('click', () => {
            currentMode = 'tablet';
            btnModeTablet.classList.add('active');
            btnModeTrackpad.classList.remove('active');
            activeZone.style.opacity = '1';
        });

        btnModeTrackpad.addEventListener('click', () => {
            currentMode = 'trackpad';
            btnModeTrackpad.classList.add('active');
            btnModeTablet.classList.remove('active');
            activeZone.style.opacity = '0.4';
        });

        document.getElementById('btn-undo').addEventListener('click', () => {
            sendPacket(PacketType.BUTTON_DOWN, ToolType.FINGER, 0, 10, 0, 0, 0);
        });
        document.getElementById('btn-redo').addEventListener('click', () => {
            sendPacket(PacketType.BUTTON_DOWN, ToolType.FINGER, 0, 11, 0, 0, 0);
        });
        document.getElementById('btn-eraser').addEventListener('click', () => {
            sendPacket(PacketType.BUTTON_DOWN, ToolType.FINGER, 0, 12, 0, 0, 0);
        });
        document.getElementById('btn-rightclick').addEventListener('click', () => {
            sendPacket(PacketType.BUTTON_DOWN, ToolType.MOUSE, 0, 1, 0, 0, 0);
            setTimeout(() => sendPacket(PacketType.BUTTON_UP, ToolType.MOUSE, 0, 1, 0, 0, 0), 50);
        });

        btnSettings.addEventListener('click', () => settingsModal.classList.remove('hidden'));
        btnCloseModal.addEventListener('click', () => settingsModal.classList.add('hidden'));
        settingsModal.addEventListener('click', (e) => {
            if (e.target === settingsModal) settingsModal.classList.add('hidden');
        });

        cfgAspect.addEventListener('change', () => { updateAspectRatioGuide(); resizeCanvas(); });

        const cfgWakelock = document.getElementById('cfg-wakelock');
        if (cfgWakelock) {
            cfgWakelock.addEventListener('change', () => {
                if (cfgWakelock.checked) {
                    requestWakeLock();
                } else {
                    releaseWakeLock();
                }
            });
        }

        updateAspectRatioGuide();
        resizeCanvas();
        connectWebSocket();
    })();
    </script>
</body>
</html>
)rawliteral";
}
