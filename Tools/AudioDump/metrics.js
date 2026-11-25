// Read-only audio metrics for AudioDump output. Not part of the judge rubric.
// Usage: bun metrics.js <dir-with-wavs>
// Reports: energy >8 kHz and >10 kHz relative to 1-4 kHz, inharmonic peak energy,
// and sustain ripple (residual std of the dB envelope over 0.3-2.5 s).
const dir = process.argv[2];
const CASES = ["standard_soft_low_E", "standard_hard_low_E", "pure_nickel_hard_low_E", "twelve_string_low_E"];
const SR = 48000;

function readWavMono(buf) {
  const dv = new DataView(buf);
  const str = (o, n) => String.fromCharCode(...new Uint8Array(buf, o, n));
  if (str(0, 4) !== "RIFF" || str(8, 4) !== "WAVE") throw new Error("not RIFF/WAVE");
  let off = 12, fmt = null, data = null;
  while (off + 8 <= buf.byteLength) {
    const id = str(off, 4), size = dv.getUint32(off + 4, true);
    if (id === "fmt ") fmt = { format: dv.getUint16(off + 8, true), ch: dv.getUint16(off + 10, true), rate: dv.getUint32(off + 12, true), bits: dv.getUint16(off + 22, true) };
    if (id === "data") data = { off: off + 8, size };
    off += 8 + size + (size & 1);
  }
  if (!fmt || !data) throw new Error("missing fmt/data");
  const bps = fmt.bits / 8, n = Math.floor(data.size / (bps * fmt.ch));
  const out = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    const p = data.off + i * bps * fmt.ch; // channel 0
    if (fmt.format === 3 && fmt.bits === 32) out[i] = dv.getFloat32(p, true);
    else if (fmt.bits === 24) { let v = (dv.getUint8(p) | (dv.getUint8(p + 1) << 8) | (dv.getUint8(p + 2) << 16)); if (v & 0x800000) v -= 0x1000000; out[i] = v / 8388608; }
    else if (fmt.bits === 16) out[i] = dv.getInt16(p, true) / 32768;
    else throw new Error("unsupported wav format " + fmt.format + "/" + fmt.bits);
  }
  return { x: out, rate: fmt.rate };
}

function fft(re, im) { // in-place radix-2
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
        const a = i + k, b = a + len / 2;
        const tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
        re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
        const nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
      }
    }
  }
}

function powerSpectrum(x, start, N) {
  const re = new Float64Array(N), im = new Float64Array(N);
  for (let i = 0; i < N; i++) re[i] = (x[start + i] || 0) * (0.5 - 0.5 * Math.cos(2 * Math.PI * i / N));
  fft(re, im);
  const P = new Float64Array(N / 2 + 1);
  for (let k = 0; k <= N / 2; k++) P[k] = re[k] * re[k] + im[k] * im[k];
  return P;
}

function bandPower(P, N, f1, f2) {
  let s = 0;
  const k1 = Math.max(0, Math.ceil(f1 * N / SR)), k2 = Math.min(P.length - 1, Math.floor(f2 * N / SR));
  for (let k = k1; k <= k2; k++) s += P[k];
  return s;
}

// Peaks with parabolic interpolation: returns [{f, p}] above threshold (dB below max).
function peaks(P, N, fmax, thrDb) {
  let mx = 0; const kmax = Math.floor(fmax * N / SR);
  for (let k = 1; k < kmax; k++) mx = Math.max(mx, P[k]);
  const thr = mx * Math.pow(10, thrDb / 10), out = [];
  for (let k = 2; k < kmax - 1; k++) {
    if (P[k] > thr && P[k] > P[k - 1] && P[k] >= P[k + 1]) {
      const a = Math.log(P[k - 1] + 1e-30), b = Math.log(P[k] + 1e-30), c = Math.log(P[k + 1] + 1e-30);
      const d = 0.5 * (a - c) / (a - 2 * b + c);
      out.push({ f: (k + d) * SR / N, p: P[k] });
    }
  }
  return out;
}

function metricsFor(x) {
  // Sustained window for spectral metrics: 0.3-2.0 s, N=8192 (about 5.9 Hz bins).
  const N = 8192, P = powerSpectrum(x, Math.round(0.3 * SR), N);
  const e14 = bandPower(P, N, 1000, 4000);
  const gt8 = 10 * Math.log10(bandPower(P, N, 8000, 23900) / e14);
  const gt10 = 10 * Math.log10(bandPower(P, N, 10000, 23900) / e14);

  // Inharmonicity: f0 = strongest peak in 60-110 Hz; each peak up to 5 kHz is
  // inharmonic if it sits more than 25 cents from every partial k*f0.
  const low = peaks(P, N, 110, -60).filter(p => p.f >= 60);
  const f0 = low.length ? low.reduce((a, b) => (b.p > a.p ? b : a)).f : 82.41;
  const mid = peaks(P, N, 5000, -40);
  let totP = 0, inhP = 0, nInh = 0;
  for (const pk of mid) {
    totP += pk.p;
    const k = Math.max(1, Math.round(pk.f / f0));
    const cents = Math.abs(1200 * Math.log2(pk.f / (k * f0)));
    if (cents > 25) { inhP += pk.p; nInh++; }
  }
  const inhFrac = totP > 0 ? inhP / totP : 0;

  // Sustain ripple: 10 ms RMS envelope over 0.3-2.5 s, residual std of a linear
  // fit to the dB envelope (removes the decay slope, leaves the wobble).
  const hop = Math.round(0.01 * SR), s0 = Math.round(0.3 * SR), s1 = Math.round(2.5 * SR);
  const env = [], tt = [];
  for (let s = s0; s + hop <= s1; s += hop) {
    let e = 0; for (let i = s; i < s + hop; i++) e += x[i] * x[i];
    env.push(10 * Math.log10(e / hop + 1e-20)); tt.push((s - s0) / SR);
  }
  const n = env.length, mt = tt.reduce((a, b) => a + b, 0) / n, my = env.reduce((a, b) => a + b, 0) / n;
  let sxy = 0, sxx = 0; for (let i = 0; i < n; i++) { sxy += (tt[i] - mt) * (env[i] - my); sxx += (tt[i] - mt) ** 2; }
  const slope = sxy / sxx, icpt = my - slope * mt;
  let rss = 0; for (let i = 0; i < n; i++) { const r = env[i] - (icpt + slope * tt[i]); rss += r * r; }
  const ripple = Math.sqrt(rss / n);

  return { f0: +f0.toFixed(2), gt8k_dB: +gt8.toFixed(1), gt10k_dB: +gt10.toFixed(1), inharmonic_frac: +inhFrac.toFixed(3), inharmonic_peaks: nInh, peaks_total: mid.length, ripple_dB: +ripple.toFixed(2), decay_fit_dB_per_s: +(slope).toFixed(2) };
}

const rows = [];
for (const c of CASES) {
  const f = `${dir}/${c}.wav`;
  const file = Bun.file(f);
  if (!(await file.exists())) { rows.push({ case: c, error: "missing " + f }); continue; }
  const { x } = readWavMono(await file.arrayBuffer());
  rows.push({ case: c, ...metricsFor(x) });
}
console.table(rows);
