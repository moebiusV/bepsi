const fs = require("fs");
const path = require("path");
const v8 = require("v8");
const { isServiceEnabled } = require("./env");
const { getSparkHealth } = require("./listeners/sparkL");
const { getSolanaHealth } = require("./listeners/solanaL");

// ---------------------------------------------------------------- process + host stats
// CPU is sampled once a minute, so /health reports a steady 1-minute average no matter who polls it.
const procStatsOs = require("os");
const procStatsFs = require("fs");
const PROC_STATS_SAMPLE_MS = 60 * 1000;

const readHostCpu = () => {
  const f = procStatsFs.readFileSync("/proc/stat", "utf8").split("\n")[0].trim().split(/\s+/).slice(1).map(Number);
  return { idle: f[3] + (f[4] || 0), total: f.reduce((a, b) => a + b, 0) };
};
const safeReadHostCpu = () => {
  try {
    return readHostCpu();
  } catch {
    return null;
  }
};
const procCpu = { pct: null, hostPct: null, last: process.cpuUsage(), lastAt: Date.now(), lastHost: safeReadHostCpu() };
// Stats must never take the app down: a failed read just leaves the last values in place.
const sampleCpu = () => {
  try {
    sampleCpuUnsafe();
  } catch {}
};
const sampleCpuUnsafe = () => {
  const now = Date.now();
  const used = process.cpuUsage(procCpu.last);
  const host = readHostCpu();
  procCpu.pct = +(((used.user + used.system) / 1000 / Math.max(1, now - procCpu.lastAt)) * 100).toFixed(1);
  const dTotal = procCpu.lastHost ? host.total - procCpu.lastHost.total : 0;
  procCpu.hostPct = dTotal > 0 ? +((1 - (host.idle - procCpu.lastHost.idle) / dTotal) * 100).toFixed(1) : null;
  procCpu.last = process.cpuUsage();
  procCpu.lastAt = now;
  procCpu.lastHost = host;
};
setTimeout(sampleCpu, 10 * 1000).unref();
setInterval(sampleCpu, PROC_STATS_SAMPLE_MS).unref();

// { process: {cpuPct, rssMB}, host: {cpuPct, load1, memAvailableMB, memTotalMB, tempC} }
const procStats = () => {
  try {
    return procStatsUnsafe();
  } catch (e) {
    return { statsError: e.message };
  }
};
const procStatsUnsafe = () => {
  const mem = {};
  for (const line of procStatsFs.readFileSync("/proc/meminfo", "utf8").split("\n")) {
    const [k, v] = line.split(":");
    if (k === "MemAvailable" || k === "MemTotal") mem[k] = Math.round(parseInt(v) / 1024);
  }
  let tempC = null;
  try {
    tempC = +(parseInt(procStatsFs.readFileSync("/sys/class/thermal/thermal_zone0/temp", "utf8")) / 1000).toFixed(1);
  } catch {}
  return {
    process: { cpuPct: procCpu.pct, rssMB: +(process.memoryUsage().rss / 1048576).toFixed(1) },
    host: { cpuPct: procCpu.hostPct, load1: +procStatsOs.loadavg()[0].toFixed(2), memAvailableMB: mem.MemAvailable, memTotalMB: mem.MemTotal, tempC },
  };
};

const startedAt = Date.now();
const MB = 1024 * 1024;

// Memory guard: the app has a leak (seen during network trouble) that grows the heap
// until V8 aborts. Before that happens, save one heap snapshot for diagnosis and exit
// cleanly so pm2 restarts us.
const HEAP_CHECK_INTERVAL_MS = 60 * 1000;
const HEAP_LOG_EVERY_N_CHECKS = 5;
const HEAP_SNAPSHOT_THRESHOLD_MB = parseInt(process.env.HEAP_SNAPSHOT_THRESHOLD_MB || "150");
const HEAP_SNAPSHOT_DIR = process.env.HEAP_SNAPSHOT_DIR || path.join(__dirname, "..", "heapsnapshots");
const MAX_HEAP_SNAPSHOTS = 2;

let unhandledRejections = 0;
let lastUnhandledRejection = null;

const memoryStats = () => {
  const m = process.memoryUsage();
  return {
    heapUsedMB: +(m.heapUsed / MB).toFixed(1),
    heapTotalMB: +(m.heapTotal / MB).toFixed(1),
    heapLimitMB: +(v8.getHeapStatistics().heap_size_limit / MB).toFixed(1),
    rssMB: +(m.rss / MB).toFixed(1),
    externalMB: +(m.external / MB).toFixed(1),
  };
};

const listHeapSnapshots = () => {
  try {
    return fs.readdirSync(HEAP_SNAPSHOT_DIR).filter((f) => f.endsWith(".heapsnapshot")).sort();
  } catch {
    return [];
  }
};

const captureHeapSnapshotAndExit = (stats) => {
  console.error(`[Health] ⚠️ heapUsed ${stats.heapUsedMB}MB exceeds ${HEAP_SNAPSHOT_THRESHOLD_MB}MB (limit ${stats.heapLimitMB}MB), capturing heap snapshot then restarting`);
  try {
    fs.mkdirSync(HEAP_SNAPSHOT_DIR, { recursive: true });
    // Keep the newest snapshots only; each one is roughly the size of the heap.
    const existing = listHeapSnapshots();
    for (const old of existing.slice(0, Math.max(0, existing.length - (MAX_HEAP_SNAPSHOTS - 1)))) {
      fs.unlinkSync(path.join(HEAP_SNAPSHOT_DIR, old));
    }
    const file = path.join(HEAP_SNAPSHOT_DIR, `bepsi-${new Date().toISOString().replace(/[:.]/g, "-")}.heapsnapshot`);
    const t = Date.now();
    v8.writeHeapSnapshot(file);
    console.error(`[Health] Heap snapshot written to ${file} in ${Math.round((Date.now() - t) / 1000)}s`);
  } catch (error) {
    console.error(`[Health] Heap snapshot failed:`, error.message);
  }
  process.exit(1);
};

const startMemoryGuard = () => {
  let checks = 0;
  let overThresholdChecks = 0;
  console.log(`[Health] Memory guard on: snapshot + restart above ${HEAP_SNAPSHOT_THRESHOLD_MB}MB heap (V8 limit ${memoryStats().heapLimitMB}MB)`);

  setInterval(() => {
    const stats = memoryStats();
    checks++;
    if (checks % HEAP_LOG_EVERY_N_CHECKS === 0) {
      console.log(`[Health] heapUsed=${stats.heapUsedMB}MB heapTotal=${stats.heapTotalMB}MB rss=${stats.rssMB}MB external=${stats.externalMB}MB`);
    }
    // Require two consecutive readings so a single burst of garbage can't trigger a restart.
    overThresholdChecks = stats.heapUsedMB > HEAP_SNAPSHOT_THRESHOLD_MB ? overThresholdChecks + 1 : 0;
    if (overThresholdChecks >= 2) {
      captureHeapSnapshotAndExit(stats);
    }
  }, HEAP_CHECK_INTERVAL_MS);
};

// A rejected promise nobody handled used to kill the whole app (Node's default), taking
// every payment listener down with it. Log it and keep running instead.
const installProcessHandlers = () => {
  process.on("unhandledRejection", (reason) => {
    unhandledRejections++;
    lastUnhandledRejection = { at: Date.now(), message: String(reason?.message || reason) };
    console.error("[Health] Unhandled promise rejection (app kept running):", reason);
  });
};

const setupHealthEndpoint = (app) => {
  app.get("/health", (req, res) => {
    const spark = isServiceEnabled("spark") ? getSparkHealth() : null;
    const sparkHealthy = !spark || (spark.pinsDetecting === spark.pinsExpected && spark.lastPollCompletedAt && Date.now() - spark.lastPollCompletedAt < 60 * 1000);
    res.json({
      status: sparkHealthy ? "ok" : "degraded",
      now: Date.now(),
      startedAt,
      uptimeSec: Math.round((Date.now() - startedAt) / 1000),
      pid: process.pid,
      memory: memoryStats(),
      ...procStats(),
      heapSnapshotThresholdMB: HEAP_SNAPSHOT_THRESHOLD_MB,
      heapSnapshots: listHeapSnapshots(),
      unhandledRejections,
      lastUnhandledRejection,
      spark,
      solana: isServiceEnabled("solana") ? getSolanaHealth() : null,
    });
  });
};

module.exports = {
  installProcessHandlers,
  startMemoryGuard,
  setupHealthEndpoint,
};
