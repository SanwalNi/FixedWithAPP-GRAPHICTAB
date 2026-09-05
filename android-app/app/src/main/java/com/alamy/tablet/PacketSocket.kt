package com.alamy.tablet

import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong
import android.os.Handler
import android.os.Looper

/**
 * Raw-binary TCP transport over the `adb reverse` USB loopback tunnel.
 *
 * Design goals:
 *  - TCP_NODELAY on both ends: every stroke sample is flushed immediately.
 *  - Coalesce-never-drop: the writer thread drains whatever is queued at
 *    wake-up into ONE system write call (fewer syscalls, lower CPU). Under
 *    extreme backpressure we thin alternating MOVE samples (segments stay
 *    continuous - the PC draws straight lines between consecutive points);
 *    DOWN/UP packets are never dropped, so a stroke can never break.
 *  - Auto-reconnect watchdog with mid-stroke safety.
 */
class PacketSocket(
    private val host: String = "127.0.0.1",
    private val port: Int = 8080,
    private val listener: Listener
) {
    interface Listener {
        /** Called on the main thread. */
        fun onConnectionStateChanged(connected: Boolean)
        /** PC -> app config sync (mode / monitor). Called on the main thread. */
        fun onConfigSync(mode: Int, monitor: Int)
        /** Round-trip latency measurement result. Called on the main thread. */
        fun onPingResult(rttMs: Float)
    }

    companion object {
        private const val HIGH_WATER = 64      // packets queued -> start thinning MOVEs
        private const val MAX_BATCH = 15       // max packets merged per write call
        private const val RECONNECT_MS = 800L
        // If no byte arrives for this long while "connected", the tunnel is a
        // black hole (dead adbd forward / USB hiccup): force a reconnect.
        private const val WATCHDOG_TIMEOUT_NANOS = 2_500_000_000L
    }

    private val running = AtomicBoolean(false)
    private val connected = AtomicBoolean(false)
    private val outQueue = LinkedBlockingQueue<ByteArray>(4096)
    private val pendingPingSentAt = AtomicLong(0)
    private val lastRecvNanos = AtomicLong(0)
    private val main = Handler(Looper.getMainLooper())
    private var monitorThread: Thread? = null

    fun start() {
        if (!running.compareAndSet(false, true)) return
        monitorThread = Thread({ sessionLoop() }, "alamy-socket").apply {
            priority = Thread.MAX_PRIORITY
            start()
        }
    }

    fun stop() {
        running.set(false)
        monitorThread?.interrupt()
    }

    val isConnected: Boolean get() = connected.get()

    /** Queue a raw packet for transmission. Non-blocking. */
    fun send(packet: ByteArray) {
        if (!connected.get()) return
        outQueue.offer(packet)
    }

    // SESSION ---------------------------------------------------------------

    private fun sessionLoop() {
        while (running.get()) {
            var sock: Socket? = null
            try {
                sock = Socket()
                sock.tcpNoDelay = true
                sock.keepAlive = true
                sock.connect(InetSocketAddress(host, port), 2000)
                lastRecvNanos.set(System.nanoTime())
                val reader = Thread({ readLoop(sock) }, "alamy-reader")
                val writer = Thread({ writeLoop(sock) }, "alamy-writer")
                reader.priority = Thread.MAX_PRIORITY
                writer.priority = Thread.MAX_PRIORITY
                setConnected(true)
                reader.start()
                writer.start()
                // Watchdog: PING/PONG keeps a healthy link chattering every
                // 500 ms. If nothing arrives for 2.5 s the tunnel is a black
                // hole (sockets open, data never crosses) -> kill and redial.
                while (running.get() && reader.isAlive && writer.isAlive) {
                    if (System.nanoTime() - lastRecvNanos.get() > WATCHDOG_TIMEOUT_NANOS) {
                        try { sock.close() } catch (_: Exception) {}
                        break
                    }
                    Thread.sleep(400)
                }
                reader.join(1000)
                writer.interrupt()
                writer.join(500)
            } catch (_: Exception) {
                // fall through to reconnect
            } finally {
                try { sock?.close() } catch (_: Exception) {}
            }
            setConnected(false)
            if (running.get()) {
                try { Thread.sleep(RECONNECT_MS) } catch (_: InterruptedException) {}
            }
        }
    }

    private fun setConnected(state: Boolean) {
        if (connected.compareAndSet(!state, state)) {
            if (!state) outQueue.clear()
            else pendingPingSentAt.set(0) // resume latency probes after (re)connect
            main.post { listener.onConnectionStateChanged(state) }
        }
    }

    // WRITE -----------------------------------------------------------------

    private fun writeLoop(sock: Socket) {
        val out = BufferedOutputStream(sock.getOutputStream(), 256)
        val batch = ArrayList<ByteArray>(MAX_BATCH)
        var skipAlternate = false
        try {
            while (running.get() && !sock.isClosed) {
                val first = outQueue.poll(100, TimeUnit.MILLISECONDS) ?: continue
                batch.clear()
                batch.add(first)

                // Drain whatever else is ready right now -> one syscall per batch.
                var depth = outQueue.size
                while (depth-- > 0 && batch.size < MAX_BATCH) {
                    val p = outQueue.poll() ?: break
                    // Backpressure thinning: drop alternating MOVE/HOVER packets.
                    // Segments remain continuous; DOWN/UP are never touched.
                    val type = p[0].toInt()
                    if (outQueue.size > HIGH_WATER && (type == Protocol.TOUCH_MOVE || type == Protocol.HOVER)) {
                        skipAlternate = !skipAlternate
                        if (skipAlternate) continue
                    }
                    batch.add(p)
                }

                var bytes = 0
                for (p in batch) bytes += p.size
                val buf = ByteArray(bytes)
                var off = 0
                for (p in batch) {
                    System.arraycopy(p, 0, buf, off, p.size)
                    off += p.size
                }
                out.write(buf)
                out.flush()
            }
        } catch (_: Exception) {
            try { sock.close() } catch (_: Exception) {}
        }
    }

    // READ ------------------------------------------------------------------

    private fun readLoop(sock: Socket) {
        val inp = BufferedInputStream(sock.getInputStream(), 256)
        val pkt = ByteArray(Protocol.PACKET_SIZE)
        try {
            while (running.get() && !sock.isClosed) {
                var off = 0
                while (off < Protocol.PACKET_SIZE) {
                    val n = inp.read(pkt, off, Protocol.PACKET_SIZE - off)
                    if (n < 0) throw java.io.EOFException()
                    off += n
                }
                lastRecvNanos.set(System.nanoTime())
                handleIncoming(pkt)
            }
        } catch (_: Exception) {
            try { sock.close() } catch (_: Exception) {}
        }
    }

    private fun handleIncoming(p: ByteArray) {
        when (p[0].toInt()) {
            Protocol.PONG -> {
                val sentAt = pendingPingSentAt.getAndSet(0)
                if (sentAt != 0L) {
                    val rtt = (System.nanoTime() - sentAt) / 1_000_000f
                    main.post { listener.onPingResult(rtt) }
                }
            }
            Protocol.PING -> {
                // PC latency probe: echo the timestamp straight back.
                val ts = readUint32(p, 12).toLong() and 0xFFFFFFFFL
                send(Protocol.pong(ts))
            }
            Protocol.CONFIG_SYNC -> {
                val mode = p[3].toInt()
                val monitor = p[2].toInt()
                main.post { listener.onConfigSync(mode, monitor) }
            }
        }
    }

    private fun readUint32(p: ByteArray, off: Int): Int =
        (p[off].toInt() and 0xFF) or
        ((p[off + 1].toInt() and 0xFF) shl 8) or
        ((p[off + 2].toInt() and 0xFF) shl 16) or
        ((p[off + 3].toInt() and 0xFF) shl 24)

    // PING ------------------------------------------------------------------

    /** Send a latency probe (called periodically from the UI). */
    fun sendPing() {
        if (!connected.get()) return
        if (pendingPingSentAt.compareAndSet(0, System.nanoTime())) {
            send(Protocol.ping(Protocol.nowMicros()))
        }
    }
}
