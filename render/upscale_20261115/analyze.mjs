// Per-note spectral analysis of ScaleRender WAVs. Usage: bun analyze.mjs <label> <dir>
// Reads <label>.wav (24-bit PCM stereo) and <label>.csv (fret,pitch,onset_sample).
// Each analysis window is 16384 samples (0.341 s). Windows stay inside the 0.9 s slot:
//   attack 0.00-0.341 s (pluck + early decay), sustain 0.10-0.441 s, tail 0.55-0.891 s.
import { readFileSync, writeFileSync } from "node:fs";
const label = process.argv[2];
const dir = process.argv[3];
const FS = 48000, N = 16384;
const ms = (t) => Math.round(t * FS);

function readWav(path) {
  const b = readFileSync(path);
  let p = 12, fmt = null, data = null;
  while (p + 8 <= b.length) {
    const id = b.toString("ascii", p, p + 4), sz = b.readUInt32LE(p + 4);
    if (id === "fmt ") fmt = { ch: b.readUInt16LE(p + 10), bits: b.readUInt16LE(p + 22) };
    if (id === "data") { data = { off: p + 8, sz }; break; }
    p += 8 + sz + (sz & 1);
  }
  const bps = fmt.bits / 8, frames = Math.floor(data.sz / (bps * fmt.ch));
  const mono = new Float64Array(frames);
  for (let i = 0; i < frames; i++) {
    let s = 0;
    for (let c = 0; c < fmt.ch; c++) {
      const o = data.off + (i * fmt.ch + c) * bps;
      if (bps === 3) { let v = b[o] | (b[o + 1] << 8) | (b[o + 2] << 16); if (v & 0x800000) v -= 0x1000000; s += v / 8388608; }
      else if (bps === 4) s += b.readFloatLE(o);
      else if (bps === 2) s += b.readInt16LE(o) / 32768;
    }
    mono[i] = s / fmt.ch;
  }
  return mono;
}

function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k++) {
        const a = i + k, c = a + len / 2;
        const tr = re[c] * cr - im[c] * ci, ti = re[c] * ci + im[c] * cr;
        re[c] = re[a] - tr; im[c] = im[a] - ti; re[a] += tr; im[a] += ti;
        const ncr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = ncr;
      }
    }
  }
}

// 4-term Blackman-Harris: about -92 dB sidelobes, so the floor is not window leakage.
const win = new Float64Array(N);
for (let i = 0; i < N; i++) {
  const x = 2 * Math.PI * i / (N - 1);
  win[i] = 0.35875 - 0.48829 * Math.cos(x) + 0.14128 * Math.cos(2 * x) - 0.01168 * Math.cos(3 * x);
}

const x = readWav(dir + "/" + label + ".wav");
function spectrum(start) {
  const re = new Float64Array(N), im = new Float64Array(N);
  for (let i = 0; i < N; i++) re[i] = (x[start + i] ?? 0) * win[i];
  fft(re, im);
  const P = new Float64Array(N / 2);
  for (let k = 0; k < N / 2; k++) P[k] = re[k] * re[k] + im[k] * im[k];
  return P;
}

const db = (p) => 10 * Math.log10(Math.max(p, 1e-30));
const binHz = FS / N;
const rows = readFileSync(dir + "/" + label + ".csv", "utf8").trim().split("\n").slice(1).map(l => l.split(",").map(Number));

// Mean power of non-partial bins 60 Hz..12 kHz for one window. Returns relative (to the fundamental) and absolute levels.
function window_(start, pitch) {
  const Q = spectrum(start);
  const f0 = 440 * Math.pow(2, (pitch - 69) / 12);
  let pk = Math.round(f0 / binHz), pp = 0;
  for (let k = pk - 4; k <= pk + 4; k++) if (Q[k] > pp) { pp = Q[k]; pk = k; }
  const fundPow = Q.slice(pk - 4, pk + 5).reduce((a, b) => a + b, 0);
  const part = new Uint8Array(N / 2);
  for (let h = 1; h * pk < N / 2 - 4; h++) for (let k = h * pk - 4; k <= h * pk + 4; k++) part[k] = 1;
  let s = 0, n = 0;
  for (let k = Math.round(60 / binHz); k < Math.round(12000 / binHz); k++) if (!part[k]) { s += Q[k]; n++; }
  const noisePow = s / n;
  return { rel: +(db(noisePow) - db(fundPow)).toFixed(1), abs: +db(noisePow).toFixed(1), fundAbs: +db(fundPow).toFixed(1) };
}

const results = [];
for (const [fret, pitch, onset] of rows) {
  const f0 = 440 * Math.pow(2, (pitch - 69) / 12);
  const P = spectrum(onset + ms(0.10));
  let peakBin = Math.round(f0 / binHz), peakP = 0;
  for (let k = peakBin - 4; k <= peakBin + 4; k++) if (P[k] > peakP) { peakP = P[k]; peakBin = k; }
  const fundPow = P.slice(peakBin - 4, peakBin + 5).reduce((a, b) => a + b, 0);
  const fundDb = db(fundPow);
  const fundHz = peakBin * binHz;

  const harm = [];
  const isPartialBin = new Uint8Array(N / 2);
  for (let h = 1; h <= 40; h++) {
    const c = Math.round(h * fundHz / binHz);
    if (c + 4 >= N / 2) break;
    for (let k = c - 4; k <= c + 4; k++) isPartialBin[k] = 1;
    if (h >= 2 && h <= 8) harm.push(+(db(P.slice(c - 3, c + 4).reduce((a, b) => a + b, 0)) - fundDb).toFixed(1));
  }

  const floorBins = [];
  for (let k = Math.round(60 / binHz); k < N / 2; k++) if (!isPartialBin[k]) floorBins.push(P[k]);
  floorBins.sort((a, b) => a - b);
  const floorDb = +(db(floorBins[floorBins.length >> 1]) - fundDb).toFixed(1);

  const hiBand = (lo, hi) => { let s = 0; for (let k = Math.round(lo / binHz); k < Math.round(hi / binHz); k++) s += P[k]; return +(db(s) - fundDb).toFixed(1); };

  // Inharmonic peaks above -50 dB rel fundamental, 60 Hz..12 kHz, outside all harmonic neighbourhoods.
  const thr = fundDb - 50;
  let inh = 0, inhFirst = null; const inhList = [];
  for (let k = Math.round(60 / binHz) + 1; k < Math.round(12000 / binHz) - 1; k++) {
    if (isPartialBin[k]) continue;
    if (P[k] > P[k - 1] && P[k] >= P[k + 1] && db(P[k]) > thr) { inh++; inhList.push([+(k * binHz).toFixed(0), +(db(P[k]) - fundDb).toFixed(1)]); if (inhFirst === null) inhFirst = +(k * binHz).toFixed(0); }
  }

  // Click: largest sample step in the first 50 ms, vs the largest sample in the sustain window.
  let peakAmp = 0, clickStep = 0;
  for (let i = onset + ms(0.10); i < onset + ms(0.44); i++) peakAmp = Math.max(peakAmp, Math.abs(x[i]));
  for (let i = onset + 1; i < onset + ms(0.05); i++) clickStep = Math.max(clickStep, Math.abs(x[i] - x[i - 1]));
  const clickRelDb = +(20 * Math.log10(Math.max(clickStep, 1e-12) / Math.max(peakAmp, 1e-12))).toFixed(1);
  const clickAbsDbFS = +(20 * Math.log10(Math.max(clickStep, 1e-12))).toFixed(1);

  const att = window_(onset, pitch), sus = window_(onset + ms(0.10), pitch), tail = window_(onset + ms(0.55), pitch);
  results.push({
    fret, pitch, nominalHz: +f0.toFixed(2), fundHz: +fundHz.toFixed(2), centsOff: +(1200 * Math.log2(fundHz / f0)).toFixed(1),
    fundAbsSus: +fundDb.toFixed(1), harm2to8Rel: harm, floorMedianRel: floorDb, above12kRel: hiBand(12000, FS / 2), above16kRel: hiBand(16000, FS / 2),
    inharmonicAboveM50: inh, inharmonicFirstHz: inhFirst, inhList,
    noiseAttackRel: att.rel, noiseSustainRel: sus.rel, noiseTailRel: tail.rel,
    noiseSustainAbs: sus.abs, fundAttackAbs: att.fundAbs, fundSustainAbs: sus.fundAbs, fundTailAbs: tail.fundAbs, noiseTailAbs: tail.abs,
    clickRelDb, clickAbsDbFS,
  });
}

const firstAbove = results.find(r => r.noiseSustainRel > -50);
const out = { label, file: label + ".wav", samples: x.length, firstFretSustainNoiseAboveM50: firstAbove ? firstAbove.fret : null, notes: results };
writeFileSync(dir + "/analysis_" + label + ".json", JSON.stringify(out, null, 2));
console.log("fret pitch  fundHz  cents |  fundSusAbs | h2  h3  h4  h5 | medFl  >12k  >16k | inh | atkN  susN  tailN | susNabs tailNabs | click rel  abs");
for (const r of results) {
  const h = r.harm2to8Rel.slice(0, 4).map(v => String(v).padStart(5)).join("");
  console.log(String(r.fret).padStart(3), String(r.pitch).padStart(4), r.fundHz.toFixed(1).padStart(7), String(r.centsOff).padStart(6), String(r.fundSustainAbsFix ?? r.fundAbsSus).padStart(10), "|", h, "|", String(r.floorMedianRel).padStart(6), String(r.above12kRel).padStart(6), String(r.above16kRel).padStart(6), "|", String(r.inharmonicAboveM50).padStart(3), "|", String(r.noiseAttackRel).padStart(6), String(r.noiseSustainRel).padStart(6), String(r.noiseTailRel).padStart(6), "|", String(r.noiseSustainAbs).padStart(7), String(r.noiseTailAbs).padStart(8), "|", String(r.clickRelDb).padStart(6), String(r.clickAbsDbFS).padStart(6));
}
console.log("first fret with sustain mean noise > -50 dB rel fundamental:", out.firstFretSustainNoiseAboveM50);
