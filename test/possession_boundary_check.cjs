// Isolated source fixtures for the owner boundary; no live engine mutation hooks.
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process');
const repo=path.resolve(__dirname,'..'),root=path.join(repo,'build-possession/control-boundary-fixture');
fs.mkdirSync(path.join(root,'src/model'),{recursive:true});
fs.mkdirSync(path.join(root,'scripts'),{recursive:true});
fs.copyFileSync(path.join(repo,'src/model/match_engine.h'),path.join(root,'src/model/match_engine.h'));
for(const name of ['check_possession_boundary.cjs','check_possession_writers.cjs'])
  fs.copyFileSync(path.join(repo,'scripts',name),path.join(root,'scripts',name));
const fixtures=[
 ['private control write','ball.possession.isPass = true;',1,false],
 ['private timer write','ball.possession.footControlSeparationSeconds = 0.1f;',1,false],
 ['private timer read','auto t = ball.possession.footControlSeparationSeconds;',1,false],
 ['private current control','auto c = ball.possession.footControlled;',1,false],
 ['private claim age','ball.possession.claimUncontrolledSeconds = 1.0f;',1,false],
 ['const claim clock write','getPossessionState().claimUnrecoverableSeconds = 0.0f;',1,false],
 ['selected claim clock','ball.possession.claimUncontrolledSeconds = 0.0f;',0,true],
 ['const current control','auto c = getPossessionState().footControlled;',0,false],
 ['private history write','ball.possession.history.lastShooter = nullptr;',1,false],
 ['qualified backing','this->ball.possession.inTouch = true;',1,false],
 ['const view write','getPossessionState().footControlSeparationSeconds = 0.1f;',1,false],
 ['bare legacy history','pendingTackle.reset();',1,false],
 ['whole ball restore','ball = MatchBall{};',1,false],
 ['removed legacy owner','auto actor = ball.possessedBy;',1,false],
 ['selected owner timer','ball.possession.footControlSeparationSeconds = 0.0f;',0,true],
 ['selected owner restore','ball = MatchBall{};',0,true],
 ['const consumer view','const auto& view = getPossessionState();',0,false],
 ['physical motion','ball.velocity.x = 0.0f;',0,false],
 ['body plan','ball.meetFor = nullptr;',0,false],
 ['render value projection','snapshot.ball.isPass = engine.getPossessionState().isPass;',0,false],
 ['cast bypass','const_cast<MatchPossessionState&>(getPossessionState()).isPass = true;',1,false]
];
for(const [name,source,expected,owner] of fixtures){
  fs.writeFileSync(path.join(root,'src/model/unowned.cpp'),owner?'':`void MatchEngine::fixture() { ${source} }\n`);
  fs.writeFileSync(path.join(root,'src/model/match_possession_binturong.cpp'),owner?`void MatchEngine::fixture() { ${source} }\n`:'');
  const result=cp.spawnSync(process.execPath,[path.join(root,'scripts/check_possession_writers.cjs')],{encoding:'utf8'});
  if(result.status!==expected)throw Error(name+': expected '+expected+', got '+result.status+'\n'+result.stdout+result.stderr);
}
console.log(`PASS: ${fixtures.length} owner/const-view/restore boundary fixtures`);
