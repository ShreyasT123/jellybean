import net from "net";

const HOST = "127.0.0.1";
const PORT = 9000;
const MODEL_ID = "transformer";
const SHAPE = [1, 128, 512];
const NUM_REQUESTS = 240;
const CONCURRENCY = 8;

const INPUT_ELEMS = 1 * 128 * 512;

// Pre-generate request frames to avoid serialization overhead in timing loops
const modelIdBytes = Buffer.from(MODEL_ID, "utf-8");
const headerBuf = Buffer.alloc(1 + modelIdBytes.length + 4);
headerBuf.writeUInt8(modelIdBytes.length, 0);
modelIdBytes.copy(headerBuf, 1);
headerBuf.writeUInt32LE(INPUT_ELEMS, 1 + modelIdBytes.length);

const payloadBuf = Buffer.alloc(INPUT_ELEMS * 4);
for (let i = 0; i < INPUT_ELEMS; i++) {
  payloadBuf.writeFloatLE(Math.random(), i * 4);
}

const requestFrame = Buffer.concat([headerBuf, payloadBuf]);

function sendRequest() {
  return new Promise((resolve) => {
    const socket = new net.Socket();
    const t0 = performance.now();
    
    socket.connect(PORT, HOST, () => {
      socket.write(requestFrame);
    });
    
    let chunks = [];
    let receivedBytes = 0;
    let expectedBytes = 13; // default header size (1 + 8 + 4)
    let parsedHeader = false;
    let ok = false;

    socket.on("data", (chunk) => {
      chunks.push(chunk);
      receivedBytes += chunk.length;
      
      if (!parsedHeader && receivedBytes >= 13) {
        const fullBuf = Buffer.concat(chunks);
        const status = fullBuf.readUInt8(0);
        ok = (status === 1);
        const outputElems = fullBuf.readUInt32LE(9);
        expectedBytes = 13 + outputElems * 4;
        parsedHeader = true;
      }
      
      if (parsedHeader && receivedBytes >= expectedBytes) {
        socket.destroy();
      }
    });
    
    socket.on("close", () => {
      const t1 = performance.now();
      resolve({ ok, latencyMs: t1 - t0 });
    });
    
    socket.on("error", () => {
      resolve({ ok: false, latencyMs: 0 });
    });
  });
}

function percentile(arr, p) {
  if (arr.length === 0) return 0;
  const sorted = [...arr].sort((a, b) => a - b);
  const index = (sorted.length - 1) * p;
  const lower = Math.floor(index);
  const upper = Math.ceil(index);
  const weight = index - lower;
  return sorted[lower] * (1 - weight) + sorted[upper] * weight;
}

async function run() {
  console.log(`Starting Bun.js parallel benchmark: concurrency=${CONCURRENCY}, requests=${NUM_REQUESTS}, shape=[${SHAPE.join(",")}]`);
  
  // Warmup
  await sendRequest();
  
  const latencies = [];
  let success = 0;
  let failures = 0;
  
  const t_start = performance.now();
  
  // Process requests with concurrency limit
  const activeWorkers = [];
  let requestsSent = 0;
  
  async function worker() {
    while (requestsSent < NUM_REQUESTS) {
      requestsSent++;
      const result = await sendRequest();
      if (result.ok) {
        success++;
        latencies.push(result.latencyMs);
      } else {
        failures++;
      }
    }
  }
  
  for (let i = 0; i < CONCURRENCY; i++) {
    activeWorkers.push(worker());
  }
  
  await Promise.all(activeWorkers);
  
  const t_end = performance.now();
  const wall_s = (t_end - t_start) / 1000;
  
  const avg_lat = latencies.reduce((a, b) => a + b, 0) / latencies.length;
  const p50_lat = percentile(latencies, 0.5);
  const p95_lat = percentile(latencies, 0.95);
  const p99_lat = percentile(latencies, 0.99);
  const throughput = latencies.length / wall_s;
  
  const results = {
    settings: {
      shape: SHAPE.join(","),
      requests: NUM_REQUESTS,
      concurrency: CONCURRENCY,
      debug: false
    },
    results: {
      jellybean_async_tcp: {
        ok: success,
        fail: failures,
        throughput_rps: parseFloat(throughput.toFixed(2)),
        latency_ms_avg: parseFloat(avg_lat.toFixed(2)),
        latency_ms_p50: parseFloat(p50_lat.toFixed(2)),
        latency_ms_p95: parseFloat(p95_lat.toFixed(2)),
        latency_ms_p99: parseFloat(p99_lat.toFixed(2)),
        wall_s: parseFloat(wall_s.toFixed(3))
      }
    }
  };
  
  console.log("\nBenchmark Finished! Output written to newbench.json:");
  console.log(JSON.stringify(results, null, 2));
  
  await Bun.write("newbench.json", JSON.stringify(results, null, 2));
}

run();
