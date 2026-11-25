const fs = require("node:fs");
const f = "C:/Users/jeffr/Fretwork/render/upscale_20261115/LoopProbe.cpp";
let s = fs.readFileSync(f, "utf8");
const a1 = 'partial2_Hz\\n");';
if (!s.includes(a1)) { console.log("HDR MISSING"); process.exit(1); }
s = s.replace(a1, 'partial2_Hz   totalDelay_minus_period\\n");');
s = s.replace('            std::printf ("%5.0f %4d %5d %9.2f %10.3e %9.4f %8.4f %9.4f %14.2f %12.2f %s\\n",', '            const double tdErr = totalLoopDelay (p.f0, fs, r.fracPart, r.lpC, r.dispA) - fs / p.f0;\n            std::printf ("%5.0f %4d %5d %9.2f %10.3e %9.4f %8.4f %9.4f %14.2f %12.2f %12.4f %s\\n",');
s = s.replace('1200.0 * std::log2 (h2 / (2.0 * p.f0)), h2, ok ? "" : "[notfound]");', '1200.0 * std::log2 (h2 / (2.0 * p.f0)), h2, tdErr, ok ? "" : "[notfound]");');
fs.writeFileSync(f, s);
console.log("patched");
