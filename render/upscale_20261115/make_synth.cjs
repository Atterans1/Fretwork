const fs = require("node:fs");
const FS = 48000, LEN = FS;
function writeWav(path, sig) {
  const ch = 2, dataSz = LEN * ch * 3;
  const b = Buffer.alloc(44 + dataSz);
  b.write("RIFF", 0); b.writeUInt32LE(36 + dataSz, 4); b.write("WAVE", 8);
  b.write("fmt ", 12); b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(ch, 22);
  b.writeUInt32LE(FS, 24); b.writeUInt32LE(FS * ch * 3, 28); b.writeUInt16LE(ch * 3, 32); b.writeUInt16LE(24, 34);
  b.write("data", 36); b.writeUInt32LE(dataSz, 40);
  let o = 44;
  for (let i = 0; i < LEN; i++) for (let c = 0; c < ch; c++) {
    let v = Math.round(Math.max(-1, Math.min(1, sig[i])) * 8388607);
    if (v < 0) v += 16777216;
    b[o++] = v & 255; b[o++] = (v >> 8) & 255; b[o++] = (v >> 16) & 255;
  }
  fs.writeFileSync(path, b);
}
let seed = 12345;
const rnd = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff * 2 - 1; };
const f0 = 440 * Math.pow(2, (85 - 69) / 12);
const variants = [["synth_clean", 0], ["synth_noiseA", 0.001], ["synth_noiseB", 0.01]];
for (const [name, nAmp] of variants) {
  const sig = new Float64Array(LEN);
  for (let i = 0; i < LEN; i++) sig[i] = 0.1 * Math.sin(2 * Math.PI * f0 * i / FS) + nAmp * rnd();
  writeWav("C:/Users/jeffr/Fretwork/render/upscale_20261115/" + name + ".wav", sig);
  fs.writeFileSync("C:/Users/jeffr/Fretwork/render/upscale_20261115/" + name + ".csv", "fret,pitch,onset_sample\n21,85,0\n");
}
console.log("synthetic written");
