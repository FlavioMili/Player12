// Lexical boundary audit, not a C++ parser. Complete-value construction/copy and
// aliases still require review. Run from anywhere; optional --out-dir writes CSV.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const repo = path.resolve(__dirname, '..');
const header = fs.readFileSync(path.join(repo, 'src/model/match_engine.h'), 'utf8');
const scrub = s => s.replace(/"(?:\\.|[^"\\])*"/g, m => m.replace(/[^\n]/g, ' '))
  .replace(/\/\*[\s\S]*?\*\//g, m => m.replace(/[^\n]/g, ' ')).replace(/\/\/[^\r\n]*/g, '');
const start = header.indexOf('struct MatchBall');
const declaration = scrub(header.slice(start, header.indexOf('\n};', start)));
const fields = [...declaration.matchAll(/^\s*(?!struct\b)(?:const\s+)?[\w:<>]+(?:\s*\*)?\s+(\w+)\s*(?:=|\{|;)/gm)].map(m => m[1]);
if (fields.length !== 87) throw Error(`Review MatchBall classification: expected 87 fields, found ${fields.length}`);
const physical = new Set(['position', 'z', 'velocity', 'velocityZ', 'spin', 'inGoalMouth']);
const planning = new Set(['dribbleGoal','ownTouchCooldown','meetFor','meetRecovering','knockedOn',
  'plannedTouchIn','pushDirection','meetPace','meetPoint','meetSeconds','meetOut','meetPush','meetLegStart']);
const diagnostics = new Set(['touchGap','touchPeakGap','sentFrom','sentHeightMetres','touchKind',
  'touchContactGap','touchContactHeight','touchAngleError','touchWeightError','touchAngleDifficulty',
  'touchWeightDifficulty','touchAngleFailed','touchWeightFailed','touchTurn','touchOutward','touchPaceChange',
  'touchLaunchSpeed','dribbleExposure','dribbleTouchLength','dribbleDirection','touchTimer']);
const eligibility = new Set(['kickerLockout','touchAttempts','commitMisses','touchWaiting']);
const policy = Object.fromEntries(fields.map(field => [field,
  physical.has(field) ? 'physics/rules placement or net geometry' :
  planning.has(field) ? 'brain/body plan, not control or receipt authority' :
  diagnostics.has(field) ? 'body/contact diagnostics and exposure' :
  eligibility.has(field) ? 'body/rules eligibility timer or mask' : 'selected possession implementation']));
const owned = new Set(fields.filter(f => policy[f] === 'selected possession implementation'));
const context = ['lastControlledTeamHome','possessionStartSeconds','lastCompletedPasser',
  'lastCompletedReceiver','lastShooter','shotAssistCandidate','pendingTackle','headerRun','headerRunLastHome'];
const files = [];
function walk(dir) {
  for (const e of fs.readdirSync(dir, {withFileTypes:true})) {
    const p = path.join(dir,e.name);
    if(e.isDirectory()) walk(p); else if(/\.(cpp|h)$/.test(p)) files.push(p);
  }
}
walk(path.join(repo, 'src'));
const rows = [], aggregates = [], aliases = [], violations = [];
const ownerFile = f => /^src\/model\/match_possession_(aardvark|binturong)\.cpp$/.test(f);
const csv = (heading, data) => [heading,...data].map(r=>r.map(v=>'"'+String(v).replaceAll('"','""')+'"').join(',')).join('\n')+'\n';
const assignment = '(?:=(?!=)|[+*/&|^%-]=|\\+\\+|--)';
for(const full of files.sort()) {
  const file = path.relative(repo,full).replaceAll('\\','/');
  const raw = fs.readFileSync(full,'utf8').split(/\r?\n/);
  const lines = scrub(raw.join('\n')).split('\n');
  let method = '';
  lines.forEach((line,i) => {
    const fn = line.match(/\bMatchEngine::(\w+)\s*\(/);
    if(fn) method = fn[1];
    const member = new RegExp('(?<![\\w.>])(?:this->)?ball\\.(\\w+)(?:\\.[xyz])?\\s*'+assignment,'g');
    const prefix = /(?:\+\+|--)\s*(?:this->)?ball\.(\w+)/g;
    for(const m of [...line.matchAll(member),...line.matchAll(prefix)]) {
      const field = m[1];
      if(!policy[field]) continue;
      const row = [file,i+1,method,field,policy[field],raw[i].trim()];
      rows.push(row);
      if(owned.has(field) && !ownerFile(file)) violations.push(row);
    }
    for(const field of context) {
      const pattern = new RegExp('(?<![\\w.>])'+field+'\\s*(?:'+assignment+'|\\.reset\\s*\\()|(?:\\+\\+|--)\\s*'+field+'\\b');
      // Header member initialisers are construction, not live mutation.
      if(file.endsWith('.cpp') && pattern.test(line)) {
        const row=[file,i+1,method,field,'selected possession context',raw[i].trim()];
        rows.push(row);if(!ownerFile(file))violations.push(row);
      }
    }
    const localDeclaration = /\b(?!(?:else|return|if)\b)(?:const\s+)?[\w:<>]+\s*[&*]?\s+ball\s*=/.test(line);
    if(!localDeclaration && /(?<![\w.>])(?:this->)?ball\s*=(?!=)/.test(line)) {
      const row=[file,i+1,method,raw[i].trim()];aggregates.push(row);
      if(!ownerFile(file))violations.push([...row,'whole MatchBall assignment outside selected owner']);
    }
    if(/\bMatchBall\s*[&*]|\bauto\s*&\s*\w+\s*=.*\bball\b|\bconst_cast\b.*\bMatchBall\b|\b\w+(?:->|\.)ball\./.test(line))
      aliases.push([file,i+1,method,raw[i].trim()]);
  });
}
const result = {schema:1,fields:fields.length,guardedBallFields:[...owned],guardedEngineContext:context,
  fieldClassification:policy,sourceFilesScanned:files.length,namedWriterRows:rows.length,
  residualPhysicalPlanningDiagnosticRows:rows.filter(r=>!ownerFile(r[0])),wholeBallAssignments:aggregates,
  aliasReview:aliases,violations,sourceHeaderSha256:crypto.createHash('sha256').update(header).digest('hex'),
  limitations:['Lexical matches are not proof against aliases, macros, raw strings or complete engine restores.',
    'Default construction and complete engine copy/assignment are sanctioned value restore paths and need copy/replay checks.',
    'Physical placement/motion, brain/body plans, diagnostics and eligibility masks retain their existing writers; this does not declare Stage 3 encapsulation complete.']};
const arg=process.argv.indexOf('--out-dir');
if(arg>=0) {
  const out=path.resolve(process.argv[arg+1]);fs.mkdirSync(out,{recursive:true});
  fs.writeFileSync(path.join(out,'writer-audit.json'),JSON.stringify(result,null,2)+'\n');
  fs.writeFileSync(path.join(out,'writers.csv'),csv(['file','line','method_hint','field','classification','source'],rows));
  fs.writeFileSync(path.join(out,'field-classification.csv'),csv(['field','classification'],Object.entries(policy)));
  fs.writeFileSync(path.join(out,'aggregate-restore-review.csv'),csv(['file','line','method_hint','source'],aggregates));
  fs.writeFileSync(path.join(out,'alias-review.csv'),csv(['file','line','method_hint','source'],aliases));
}
console.log(JSON.stringify({fields:fields.length,guardedBallFields:owned.size,guardedEngineContext:context.length,
  writerRows:rows.length,wholeBallAssignments:aggregates.length,violations:violations.length}));
for(const row of violations)console.error(row.join(': '));
process.exitCode=violations.length?1:0;
