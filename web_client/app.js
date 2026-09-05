// Alamy Ultra-Low Latency Mobile Digitizer Client

(function() {
    'use strict';

    // Packet Enums (Must match protocol.hpp)
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

    const ToolType = {
        FINGER: 0,
        STYLUS: 1,
        ERASER: 2,
        MOUSE: 3
    };

    // State Variables
    let ws = null;
    let isConnected = false;
    let currentMode = 'tablet'; // 'tablet' or 'trackpad'
    let wakeLock = null;
    let packetCount = 0;
    let lastFpsTime = performance.now();
    let currentPressure = 0;
    let hasAttemptedLock = false; // Track if we've tried to lock orientation
    let activePointers = new Map(); // pointerId -> {x, y, startX, startY, time}

    // DOM Elements
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

    // Canvas Resize — BUG-13 FIX: reset transform before re-scaling
    // PERF: cache the active-zone rect - getBoundingClientRect() per pointer
    // event forces layout at up to 240Hz on the phone -> jank. Refresh on resize.
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
        ctx.setTransform(1, 0, 0, 1, 0, 0); // Reset accumulated transforms
        ctx.scale(dpr, dpr);
    }
    window.addEventListener('resize', () => {
        updateAspectRatioGuide();
        resizeCanvas();
    });

    window.addEventListener('orientationchange', () => {
        // Give the browser a moment to update dimensions after rotation
        setTimeout(() => {
            updateAspectRatioGuide();
            resizeCanvas();
        }, 100);
    });

    // Auto Fullscreen & Landscape Lock
    async function enforceLandscape() {
        hasAttemptedLock = true;
        try {
            if (!document.fullscreenElement) {
                await document.documentElement.requestFullscreen();
            }
            if (screen.orientation && screen.orientation.lock) {
                await screen.orientation.lock('landscape');
            }
        } catch (err) {
            console.log('Failed to enforce landscape:', err);
        }
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
            activeZone.style.aspectRatio = 'auto';
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

    // WebSocket Connection
    function connectWebSocket() {
        const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        const wsUrl = `${protocol}//${window.location.host}/ws`;

        connText.textContent = 'Connecting...';
        connStatus.className = 'status-badge disconnected';

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
    }

    // Screen Wake Lock — BUG-14 FIX: support enable/disable via checkbox
    async function requestWakeLock() {
        try {
            if ('wakeLock' in navigator) {
                wakeLock = await navigator.wakeLock.request('screen');
            }
        } catch (err) {
            console.log('Wake Lock error:', err);
        }
    }

    async function releaseWakeLock() {
        try {
            if (wakeLock) {
                await wakeLock.release();
                wakeLock = null;
            }
        } catch (err) {}
    }

    // Packed 16-byte TouchPacket Builder
    function sendPacket(packetType, toolType, fingerId, flags, normX, normY, pressureNorm, tiltX = 0, tiltY = 0) {
        if (!isConnected || ws.readyState !== WebSocket.OPEN) return;
        // FIX: Drop MOVE packets if the WebSocket buffer is backing up (> 4 packets queued).
        // Without this, slow network flushes cause burst delivery → the drawing app sees
        // sudden position jumps which look like broken lines. DOWN/UP always go through.
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
        
        // Microsecond timestamp
        const clientMicros = Math.round(performance.now() * 1000);
        view.setUint32(12, clientMicros, true);

        ws.send(buffer);
        packetCount++;
    }

    // Pressure Curve Mapping
    let lastPressurePct = -1;
    function updatePressureHud(pressure) {
        const pct = Math.round(pressure * 100);
        if (pct !== lastPressurePct) {
            lastPressurePct = pct;
            statPressure.textContent = `${pct}%`;
        }
    }

    function mapPressure(rawPressure) {
        if (rawPressure <= 0.001) return 0.5; // Default standard pressure if device doesn't support pressure
        const curve = cfgPressureCurve.value;
        if (curve === 'soft') {
            return Math.sqrt(rawPressure);
        } else if (curve === 'hard') {
            return Math.pow(rawPressure, 1.8);
        }
        return rawPressure;
    }

    // Touch & Pointer Event Handlers
    function getNormalizedCoords(e) {
        const rect = zoneRect;
        const normX = (e.clientX - rect.left) / rect.width;
        const normY = (e.clientY - rect.top) / rect.height;
        return {
            x: Math.max(0, Math.min(1, normX)),
            y: Math.max(0, Math.min(1, normY)),
            inBounds: normX >= 0 && normX <= 1 && normY >= 0 && normY <= 1
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
        refreshZoneRect();
        if (!hasAttemptedLock) {
            enforceLandscape();
        }

        touchHint.style.opacity = '0';
        activePointers.set(e.pointerId, {
            x: e.clientX,
            y: e.clientY,
            startX: e.clientX,
            startY: e.clientY,
            time: performance.now()
        });

        const coords = getNormalizedCoords(e);
        const tool = getToolType(e);
        const pressure = mapPressure(e.pressure);
        currentPressure = pressure;
        updatePressureHud(pressure);

        let flags = 0;
        if (e.buttons === 2 || e.button === 2) flags |= 0x01; // Barrel button / Right click

        sendPacket(PacketType.TOUCH_DOWN, tool, e.pointerId % 10, flags, coords.x, coords.y, pressure, e.tiltX || 0, e.tiltY || 0);

        if (cfgFeedback.checked) {
            drawTouchFeedback(coords.x, coords.y, pressure, true);
        }
    }

    function handlePointerMove(e) {
        if (!activePointers.has(e.pointerId)) return;

        // Extract coalesced high-frequency points if available (120Hz/240Hz digitizer API)
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
                if (activePointers.size === 0) touchHint.style.opacity = '0.3';
            }, 1000);
        }
    }

    // Visual Canvas Trail
    let lastDrawX = -1, lastDrawY = -1;
    let hasCanvasContent = false; // PERF-06: track if there's anything to fade
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

    // PERF-06 FIX: Only run fade animation when there's content to fade (saves battery)
    let fadeFrameCount = 0;
    function animateFade() {
        if (activePointers.size === 0) {
            lastDrawX = -1;
            lastDrawY = -1;
        }
        if (hasCanvasContent) {
            // BUG-13 FIX: Use CSS dimensions (not canvas pixel dimensions) through the scaled context
            const rect = zoneRect;
            ctx.fillStyle = 'rgba(7, 7, 10, 0.08)';
            ctx.fillRect(0, 0, rect.width, rect.height);
            fadeFrameCount++;
            // After ~120 frames of no input, consider canvas clear and stop fading
            if (activePointers.size === 0 && fadeFrameCount > 120) {
                hasCanvasContent = false;
                fadeFrameCount = 0;
            }
        }
        requestAnimationFrame(animateFade);
    }
    requestAnimationFrame(animateFade);

    // Rate & Latency HUD Timer
    setInterval(() => {
        const now = performance.now();
        const elapsed = (now - lastFpsTime) / 1000.0;
        const pps = Math.round(packetCount / elapsed);
        statRate.textContent = `${pps} Hz`;
        statLatency.textContent = isConnected ? '< 1 ms' : '--';
        packetCount = 0;
        lastFpsTime = now;
    }, 1000);

    // Event Listeners
    touchSurface.addEventListener('pointerdown', handlePointerDown, { passive: false });
    touchSurface.addEventListener('pointermove', handlePointerMove, { passive: false });
    touchSurface.addEventListener('pointerup', handlePointerUp, { passive: false });
    touchSurface.addEventListener('pointercancel', handlePointerUp, { passive: false });

    // Mode Buttons
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

    // Quick Actions
    document.getElementById('btn-undo').addEventListener('click', () => {
        sendPacket(PacketType.BUTTON_DOWN, ToolType.FINGER, 0, 10, 0, 0, 0); // Special code for Ctrl+Z
    });
    document.getElementById('btn-redo').addEventListener('click', () => {
        sendPacket(PacketType.BUTTON_DOWN, ToolType.FINGER, 0, 11, 0, 0, 0); // Ctrl+Y
    });
    document.getElementById('btn-eraser').addEventListener('click', () => {
        sendPacket(PacketType.BUTTON_DOWN, ToolType.FINGER, 0, 12, 0, 0, 0); // E
    });
    // FIX: Removed duplicate right-click listener (was registered twice, sent 2 right-clicks per tap)
    document.getElementById('btn-rightclick').addEventListener('click', () => {
        sendPacket(PacketType.BUTTON_DOWN, ToolType.MOUSE, 0, 1, 0, 0, 0);
        setTimeout(() => sendPacket(PacketType.BUTTON_UP, ToolType.MOUSE, 0, 1, 0, 0, 0), 50);
    });

    // Settings Modal
    btnSettings.addEventListener('click', () => settingsModal.classList.remove('hidden'));
    btnCloseModal.addEventListener('click', () => settingsModal.classList.add('hidden'));
    settingsModal.addEventListener('click', (e) => {
        if (e.target === settingsModal) settingsModal.classList.add('hidden');
    });

    cfgAspect.addEventListener('change', () => { updateAspectRatioGuide(); resizeCanvas(); });

    // BUG-14 FIX: Wire up wake lock checkbox
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

    // Initialization
    updateAspectRatioGuide();
    resizeCanvas();
    connectWebSocket();
})();
