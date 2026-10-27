#!/usr/bin/env node
/*
 * 掌控板 BMP388 数据接收服务
 *
 *   POST /api/data     掌控板往这里推 JSON
 *   GET  /             网页
 *   GET  /api/latest   最近 N 个点（默认 1800），网页轮询用
 *   GET  /api/export   下载 data.csv
 *
 * 运行：node server.js     然后浏览器打开 http://localhost:8000
 */
'use strict';

const http = require('http');
const fs   = require('fs');
const path = require('path');

const PORT       = Number(process.env.PORT || 8000);
const MAX_POINTS = Number(process.env.MAX_POINTS || 72000);  // 内存里最多留这么多点
                                                             // 20Hz 采样时 72000 点 ≈ 1 小时
const MAX_BATCH  = 2000;   // 一个批量包最多接受多少个采样点
const INDEX_PATH = path.join(__dirname, 'index.html');

let points     = [];       // 内存环形缓冲
let seq        = 0;        // 收到的包序号
let lastRecvAt = 0;        // 最后一次收到数据的时间
let totalRecv  = 0;
let batches    = 0;        // 收到的批次数（一个批次含多个采样点）
let consecBad  = 0;        // 连续多少个包解析/范围不过

// ---------- 记录状态 ----------
// 只有点了“开始记录”才往 CSV 写。不点就只在内存里，随便看。
let recording  = false;
let recFile    = null;     // 当前正在写的文件完整路径
let recRows    = 0;        // 本次已写行数
let recStart   = 0;        // 本次开始时刻
let lastRecFile = null;    // 最近一次记录的文件（停止后仍可下载）

// 只在真正收不下时打日志（正常包不打，否则一片噪音）
function noteBad(req, raw, why) {
  consecBad++;
  if (consecBad <= 5 || consecBad % 30 === 0) {
    console.log(`[bad] ${why} (${req.socket.remoteAddress}): ${raw.slice(0, 160)}`);
  }
}

// ---------- CSV 记录 ----------
const CSV_HEAD = 'seq,time_iso,temp_c,press_hpa,alt_m,board_ok,board_err\n';

function recFileName() {
  const d = new Date(), p = (n) => String(n).padStart(2, '0');
  // 每次记录单独一个文件，不同批次的数据不会混在一起
  return `data_${d.getFullYear()}${p(d.getMonth() + 1)}${p(d.getDate())}_`
       + `${p(d.getHours())}${p(d.getMinutes())}${p(d.getSeconds())}.csv`;
}

function startRecording() {
  if (recording) return { ok: true, recording: true, file: path.basename(recFile) };
  recFile = path.join(__dirname, recFileName());
  try {
    fs.writeFileSync(recFile, CSV_HEAD);
  } catch (e) {
    recFile = null;
    return { ok: false, error: e.message };
  }
  recRows = 0;
  recStart = Date.now();
  recording = true;
  lastRecFile = recFile;
  console.log(`[rec] 开始记录 -> ${path.basename(recFile)}`);
  return { ok: true, recording: true, file: path.basename(recFile) };
}

function stopRecording() {
  if (!recording) return { ok: true, recording: false };
  recording = false;
  console.log(`[rec] 停止记录，共 ${recRows} 行 -> ${path.basename(recFile)}`);
  return { ok: true, recording: false, file: path.basename(recFile), rows: recRows };
}

// 没在记录就什么都不做——这是“点按钮才开始记”的关键
function appendCsv(r) {
  if (!recording || !recFile) return;
  const line = [r.seq, new Date(r.recvAt).toISOString(), r.temp, r.press, r.alt, r.ok, r.err].join(',') + '\n';
  fs.appendFile(recFile, line, (e) => { if (e) console.error('[csv]', e.message); });
  recRows++;
}

// ---------- 工具 ----------
function num(v, lo, hi) {
  const n = Number(v);
  if (!Number.isFinite(n) || n < lo || n > hi) return null;
  return n;
}

function sendJson(res, code, obj) {
  const body = JSON.stringify(obj);
  res.writeHead(code, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(body),
    'Cache-Control': 'no-store',
  });
  res.end(body);
}

function readBody(req, limit = 65536) {
  return new Promise((resolve, reject) => {
    let buf = '';
    req.on('data', (c) => {
      buf += c;
      if (buf.length > limit) { reject(new Error('body too large')); req.destroy(); }
    });
    req.on('end', () => resolve(buf));
    req.on('error', reject);
  });
}

// ---------- 路由 ----------
const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);

  // --- 掌控板推数据 ---
  if (req.method === 'POST' && url.pathname === '/api/data') {
    let raw;
    try { raw = await readBody(req); }
    catch (e) { return sendJson(res, 413, { ok: false, error: e.message }); }

    let j;
    try { j = JSON.parse(raw); }
    catch {
      noteBad(req, raw, 'json 解析失败');
      return sendJson(res, 400, { ok: false, error: 'invalid json' });
    }

    // 统一成一串采样点。两种格式都收：
    //   单点：{"temp":26.5,"press":1009.9,"alt":27.4}
    //   批量：{"hz":20,"temp":[...],"press":[...],"alt":[...]}
    const recvAt = Date.now();
    let samples;

    if (Array.isArray(j.temp)) {
      const n = j.temp.length;
      if (!n || n > MAX_BATCH || !Array.isArray(j.press) || !Array.isArray(j.alt) ||
          j.press.length !== n || j.alt.length !== n) {
        noteBad(req, raw, `批量包格式不对或三个数组长度不一致 (n=${n})`);
        return sendJson(res, 400, { ok: false, error: 'bad batch: temp/press/alt lengths must match' });
      }
      const hz = Math.min(Math.max(Number(j.hz) || 20, 1), 200);
      const dt = 1000 / hz;
      samples = new Array(n);
      for (let i = 0; i < n; i++) {
        samples[i] = {
          temp:  num(j.temp[i],  -60,  120),
          press: num(j.press[i], 200, 1200),
          alt:   num(j.alt[i], -500, 10000),
          // 板子只有一个包的到达时刻，把最后一个点锚在它上面，往前逐个推
          recvAt: Math.round(recvAt - (n - 1 - i) * dt),
        };
      }
    } else {
      samples = [{
        temp:  num(j.temp,  -60,  120),
        press: num(j.press, 200, 1200),
        alt:   num(j.alt, -500, 10000),
        recvAt,
      }];
    }

    // 整批里只要有一个点不合格就整批拒掉，免得半截数据混进 CSV
    if (samples.some(s => s.temp === null || s.press === null || s.alt === null)) {
      noteBad(req, raw, '字段超范围或缺失');
      return sendJson(res, 400, { ok: false, error: 'out of range or missing temp/press/alt' });
    }
    consecBad = 0;

    const okN = Number(j.ok) || 0, errN = Number(j.err) || 0;
    // 时间戳强制单调递增。批次因网络延迟到晚了会和上一批时间窗重叠，
    // 而不单调会让网页的二分查找（visible / 悬停）失效。
    let prevTs = points.length ? points[points.length - 1].recvAt : 0;
    for (const s of samples) {
      let ts = s.recvAt;
      if (ts <= prevTs) ts = prevTs + 1;
      prevTs = ts;
      const r = {
        seq: ++seq,
        recvAt: ts,
        temp: +s.temp.toFixed(2),
        press: +s.press.toFixed(2),
        alt: +s.alt.toFixed(1),
        ok: okN, err: errN,
      };
      points.push(r);
      appendCsv(r);
      totalRecv++;
    }
    if (points.length > MAX_POINTS) points.splice(0, points.length - MAX_POINTS);
    lastRecvAt = recvAt;

    batches++;
    if (batches === 1 || batches % 30 === 0) {
      const last = samples[samples.length - 1];
      console.log(`[recv] +${samples.length}点  ${new Date(recvAt).toLocaleTimeString()}  ` +
                  `${last.temp}°C ${last.press}hPa ${last.alt}m  ` +
                  `(累计 ${totalRecv} 点 / ${batches} 批)`);
    }
    return sendJson(res, 200, { ok: true, n: samples.length, seq });
  }

  // --- 开始/停止记录 ---
  if (req.method === 'POST' && url.pathname === '/api/record') {
    let raw;
    try { raw = await readBody(req); }
    catch (e) { return sendJson(res, 413, { ok: false, error: e.message }); }

    let want;
    try { want = JSON.parse(raw).on; }
    catch { return sendJson(res, 400, { ok: false, error: 'invalid json' }); }

    const r = want ? startRecording() : stopRecording();
    return sendJson(res, r.ok ? 200 : 500, r);
  }

  // --- 下载最近一次记录的 CSV ---
  if (req.method === 'GET' && url.pathname === '/api/download') {
    if (!lastRecFile || !fs.existsSync(lastRecFile)) {
      res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' });
      return res.end('还没有记录过。先点“开始记录”。');
    }
    res.writeHead(200, {
      'Content-Type': 'text/csv; charset=utf-8',
      'Content-Disposition': `attachment; filename="${path.basename(lastRecFile)}"`,
    });
    return fs.createReadStream(lastRecFile).pipe(res);
  }

  // --- 网页轮询 ---
  if (req.method === 'GET' && url.pathname === '/api/latest') {
    const sinceRaw = url.searchParams.get('since');
    let out;

    if (sinceRaw !== null) {
      // 增量模式：只回传比 since 新的点。
      // 20Hz 下这个很关键 —— 否则每 2 秒要重传上万点（好几 MB）。
      const since = Number(sinceRaw) || 0;
      let i = points.length;
      while (i > 0 && points[i - 1].seq > since) i--;
      out = points.slice(i);
    } else {
      const n = Math.min(Math.max(Number(url.searchParams.get('n')) || 1800, 1), MAX_POINTS);
      out = points.slice(-n);
    }

    return sendJson(res, 200, {
      now: Date.now(),
      lastRecvAt,
      totalRecv,
      batches,
      serverStart: START,
      recording,
      recFile: recFile ? path.basename(recFile) : (lastRecFile ? path.basename(lastRecFile) : null),
      recRows,
      recStart,
      points: out,
    });
  }

  // --- 网页 ---
  if (req.method === 'GET' && (url.pathname === '/' || url.pathname === '/index.html')) {
    return fs.readFile(INDEX_PATH, (e, buf) => {
      if (e) { res.writeHead(500); return res.end('cannot read index.html: ' + e.message); }
      res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' });
      res.end(buf);
    });
  }

  res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' });
  res.end('404');
});

const START = Date.now();
server.listen(PORT, '0.0.0.0', () => {
  console.log('');
  console.log('  掌控板 BMP388 数据服务已启动');
  console.log('  ─────────────────────────────────────');
  console.log(`  本机打开   http://localhost:${PORT}`);

  // 列出所有局域网 IPv4，方便填进板子代码
  const os = require('os');
  for (const list of Object.values(os.networkInterfaces())) {
    for (const i of list || []) {
      if (i.family === 'IPv4' && !i.internal) {
        console.log(`  板子填这个 http://${i.address}:${PORT}/api/data`);
      }
    }
  }
  console.log('  ─────────────────────────────────────');
  console.log(`  CSV 记录   点网页上的“开始记录”才开始写，存在本目录下`);
  console.log('');
});
