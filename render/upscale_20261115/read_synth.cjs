const fs = require("node:fs");
for (const l of ["synth_clean", "synth_noiseA", "synth_noiseB"]) {
  const j = JSON.parse(fs.readFileSync("C:/Users/jeffr/Fretwork/render/upscale_20261115/analysis_" + l + ".json", "utf8"));
  const n = j.notes[0];
  console.log(l, "susRel", n.noiseSustainRel, "susAbs", n.noiseSustainAbs, "fundAbs", n.fundSustainAbs, "h2rel", n.harm2to8Rel[0]);
}
