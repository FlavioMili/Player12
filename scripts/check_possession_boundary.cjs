// Lexical owner boundary audit, supplemented by C++ private/const access checks.
// Run from anywhere; --out-dir exports the classified inventory for review.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const repo = path.resolve(__dirname, '..');
const header = fs.readFileSync(path.join(repo, 'src/model/match_engine.h'), 'utf8');
const scrub = s => s.replace(/"(?:\\.|[^"\\])*"/g, m => m.replace(/[^\n]/g, ' '))
  .replace(/\/\*[\s\S]*?\*\//g, m => m.replace(/[^\n]/g, ' ')).replace(/\/\/[^\r\n]*/g, '');
const cleanHeader = scrub(header);
function declaration(name) {
  const start = cleanHeader.indexOf('struct ' + name + '\n');
  if(start < 0) throw Error('Missing declaration: ' + name);
  return cleanHeader.slice(start, cleanHeader.indexOf('\n};', start) + 3);
}
const fields = text => [...text.matchAll(/^  (?!struct\b)(?:const\s+)?[\w:<>]+(?:\s*\*)?\s+(\w+)\s*(?:=|\{|;)/gm)].map(m => m[1]);
const owned = fields(declaration('MatchPossessionState')).filter(f => f !== 'history');
const residual = fields(declaration('MatchBall')).filter(f => f !== 'possession');
const context = fields(declaration('PossessionHistory'));
const policyFields = ['footControlled','footControlSeparationSeconds','claimUncontrolledSeconds','claimUnrecoverableSeconds'];
if(owned.length !== 47 || residual.length !== 44 || context.length !== 9 ||
   !policyFields.every(f => owned.includes(f)) || new Set([...owned,...residual]).size !== 91)
  throw Error('Review possession inventory: expected 43 legacy facts plus four policy fields, 44 residual fields and nine history fields');
if(!/private:\s*friend class MatchEngine;\s*MatchPossessionState possession;/.test(declaration('MatchBall')) ||
   !/const MatchPossessionState& possessionState\(\) const/.test(declaration('MatchBall')) ||
   !/const MatchPossessionState& getPossessionState\(\) const/.test(cleanHeader) ||
   !/const PossessionHistory& getPossessionHistory\(\) const/.test(cleanHeader))
  throw Error('Possession backing must be private, with const-only views');
const physical = new Set(['position','z','velocity','velocityZ','spin','inGoalMouth']);
const planning = new Set(['dribbleGoal','ownTouchCooldown','meetFor','meetRecovering','knockedOn',
  'plannedTouchIn','pushDirection','meetPace','meetPoint','meetSeconds','meetOut','meetPush','meetLegStart']);
const eligibility = new Set(['kickerLockout','touchAttempts','commitMisses','touchWaiting']);
const policy = Object.fromEntries([...owned,...residual].map(f => [f, owned.includes(f) ?
  'selected possession state' : physical.has(f) ? 'physics/rules placement or net geometry' :
  planning.has(f) ? 'brain/body plan, not control or receipt authority' :
  eligibility.has(f) ? 'body/rules eligibility timer or mask' : 'body/contact diagnostics and exposure']));
const files = [];
function walk(dir) { for(const entry of fs.readdirSync(dir,{withFileTypes:true})) {
  const full=path.join(dir,entry.name);if(entry.isDirectory())walk(full);
  else if(/\.(cpp|h)$/.test(entry.name))files.push(full);
} }
walk(path.join(repo,'src'));
const rows=[],aggregates=[],aliases=[],violations=[],readerViolations=[];
const ownerFile = f => /^src\/model\/match_possession_(aardvark|binturong)\.cpp$/.test(f);
// This adapter consumes MatchRenderBall, whose compatibility names are values.
const renderProjectionFile = f => f === 'src/gui/render/match_renderer_3d.cpp';
const assignment = '(?:=(?!=)|[+*/&|^%-]=|\\+\\+|--)';
const rawLegacy = new RegExp('(?<![\\w.>])(?:this->)?ball\\.('+owned.join('|')+')\\b','g');
for(const full of files.sort()) {
  const file=path.relative(repo,full).replaceAll('\\','/');
  const raw=fs.readFileSync(full,'utf8').split(/\r?\n/);
  const lines=scrub(raw.join('\n')).split('\n');let method='';
  lines.forEach((line,index) => {
    const fn=line.match(/\bMatchEngine::(\w+)\s*\(/);if(fn)method=fn[1];
    const row=(field,classification)=>[file,index+1,method,field,classification,raw[index].trim()];
    if(new RegExp('(?:getPossessionState|possessionState|getPossessionHistory)\\s*\\(\\s*\\)\\.(?:history\\.)?\\w+\\s*'+assignment).test(line))
      violations.push(row('view','mutation through const possession view'));
    const backing=/(?<![\w.>])(?:this->)?ball\.possession\b/.test(line);
    if(backing && !ownerFile(file)) violations.push(row('possession','private backing outside selected owner'));
    if(!renderProjectionFile(file))
      for(const match of line.matchAll(rawLegacy)) readerViolations.push(row(match[1],'removed legacy ball field'));
    const writes=new RegExp('(?<![\\w.>])(?:this->)?ball\\.(?:possession\\.(?:history\\.)?)?(\\w+)(?:\\.[xyz])?\\s*'+assignment,'g');
    for(const m of line.matchAll(writes)) {
      const field=m[1];rows.push(row(field,policy[field] || 'possession history/restore'));
      if((owned.includes(field)||context.includes(field)||field==='history'||field==='possession')&&!ownerFile(file))
        violations.push(row(field,'mutation outside selected owner'));
    }
    const prefix=/(?:\+\+|--)\s*(?:this->)?ball\.(?:possession\.(?:history\.)?)?(\w+)/g;
    for(const m of line.matchAll(prefix)) {
      rows.push(row(m[1],policy[m[1]] || 'possession history'));
      if((owned.includes(m[1])||context.includes(m[1]))&&!ownerFile(file))
        violations.push(row(m[1],'prefix mutation outside selected owner'));
    }
    // An old bare history field must not be reintroduced as a second authority.
    for(const field of context) if(file.endsWith('.cpp') &&
      new RegExp('(?<![\\w.>])'+field+'\\s*(?:'+assignment+'|\\.reset\\s*\\()').test(line))
      violations.push(row(field,'bare legacy history mutation'));
    if(/\bconst_cast\b/.test(line)&&/Possession|MatchBall|possession/.test(line))
      violations.push(row('cast','const possession access bypass'));
    const localDeclaration=/\b(?!(?:else|return|if)\b)(?:const\s+)?[\w:<>]+\s*[&*]?\s+ball\s*=/.test(line);
    if(!localDeclaration && /(?<![\w.>])(?:this->)?ball\s*=(?!=)/.test(line)) {
      const r=[file,index+1,method,raw[index].trim()];aggregates.push(r);
      if(!ownerFile(file))violations.push([...r,'whole ball restore outside selected owner']);
    }
    if(/\bMatchBall\s*[&*]|\bauto\s*&\s*\w+\s*=.*\bball\b|\b\w+(?:->|\.)ball\./.test(line))
      aliases.push([file,index+1,method,raw[index].trim()]);
  });
}
const result={schema:3,fields:91,legacyBallFields:87,policyFields,guardedBallFields:owned,guardedEngineContext:context,
  fieldClassification:policy,sourceFilesScanned:files.length,namedWriterRows:rows.length,
  wholeBallAssignments:aggregates,aliasReview:aliases,violations,readerViolations,
  sourceHeaderSha256:crypto.createHash('sha256').update(header).digest('hex'),
  limitations:['Lexical checks do not prove alias/macro safety; C++ private storage and const views also enforce access.',
    'Complete engine copies are sanctioned value restores; whole-ball lifecycle resets must preserve history.',
    'Physical motion, body/brain plans, diagnostics and eligibility remain in their existing domains.']};
const csv=(heading,data)=>[heading,...data].map(r=>r.map(v=>'"'+String(v).replaceAll('"','""')+'"').join(',')).join('\n')+'\n';
const arg=process.argv.indexOf('--out-dir');if(arg>=0) {
  const out=path.resolve(process.argv[arg+1]);fs.mkdirSync(out,{recursive:true});
  fs.writeFileSync(path.join(out,'writer-audit.json'),JSON.stringify(result,null,2)+'\n');
  fs.writeFileSync(path.join(out,'writers.csv'),csv(['file','line','method_hint','field','classification','source'],rows));
  fs.writeFileSync(path.join(out,'field-classification.csv'),csv(['field','classification'],Object.entries(policy)));
  fs.writeFileSync(path.join(out,'aggregate-restore-review.csv'),csv(['file','line','method_hint','source'],aggregates));
  fs.writeFileSync(path.join(out,'alias-review.csv'),csv(['file','line','method_hint','source'],aliases));
}
console.log(JSON.stringify({fields:91,guardedBallFields:owned.length,guardedEngineContext:context.length,
  writerRows:rows.length,wholeBallAssignments:aggregates.length,violations:violations.length,readerViolations:readerViolations.length}));
for(const r of [...violations,...readerViolations])console.error(r.join(': '));
process.exitCode=violations.length||readerViolations.length?1:0;
