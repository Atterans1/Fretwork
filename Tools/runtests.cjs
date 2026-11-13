const {spawnSync}=require('child_process');const fs=require('fs');
const r=spawnSync('C:/Users/jeffr/Fretwork/build/FretworkTests_artefacts/Release/FretworkTests.exe',[],{encoding:'utf8',timeout:0,maxBuffer:1<<27});
fs.writeFileSync('C:/Users/jeffr/Fretwork/test-baseline.log',(r.stdout||'')+(r.stderr||'')+'\nEXIT='+r.status+'\n');