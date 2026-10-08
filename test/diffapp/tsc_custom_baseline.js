let testRunnerJsContent; // The code that is going to be evaled
let triggerTestRerun; // Set by code we inject into the evaled code

{
  const fs = require('fs');
  const path = require('path');

  if (process.argv.length < 3) {
    throw Error('Not enough arguments');
  }

  const tmpDir = process.argv[2];
  const ttttRoot = path.dirname(path.dirname(__dirname));
  const tsDir = path.join(ttttRoot, 'thirdparty', 'tsc');

  fs.writeFileSync(path.join(tmpDir, 'mytest.config'), JSON.stringify({
    runUnitTests: false,
    runners: ['conformance'],
  }));

  // TypeScript/src/harness/findUpDir.ts needs __dirname set to something in the TypeScript dir
  __dirname = tsDir;

  function assertOneReplacement(filePath, text, pattern, replacement) {
    let count = 0;
    const ret = text.replace(pattern, () => {
      count += 1;
      return replacement;
    });
    if (count != 1) {
      throw Error(`'${pattern}' should have appeared in ${filePath} exactly once, but it appeared ${count} times`);
    }
    return ret;
  }

  console.log('[tttt runner] Loading test runner script...');
  // RUNJS: a patched copy of run.js (console.error in checker functions: what tsc does on a test, through ./tscbaseline.sh's steps)
  const runJsPath = process.env.RUNJS || path.join(tsDir, 'built', 'local', 'run.js');
  testRunnerJsContent = fs.readFileSync(runJsPath) + '';

  // This allows us to trigger a re-run of the tests
  testRunnerJsContent = assertOneReplacement(
    runJsPath,
    testRunnerJsContent,
    'function startTestEnvironment() {',
    // Don't put in newline, preserve line numbers on error messages
    'triggerTestRerun = () => { runTests(runners); }; function startTestEnvironment() {'
  );

  testRunnerJsContent = assertOneReplacement(runJsPath, testRunnerJsContent, 'importTests();', '');

  process.chdir(tmpDir);
}

// Mocha mocks
function before(f) {
    f();
}
function after(f) {}
function describe(name, f) {
  console.log('[tttt runner] Mocha stub: ' + name);
  try {
    f();
  } catch (e) {
    const errorLines = e.message.split('\n');
    console.log('[tttt runner] ' + errorLines[0] + ' (+' + (errorLines.length - 1) + ' additional lines)');
  }
}
const beforeEach = before;
const afterEach = after;
const it = describe;

console.log('[tttt runner] Evaling test runner script...');
eval(testRunnerJsContent);
console.log('[tttt runner] Done running tests');

process.stdin.on('data', () => {
  console.log('[tttt runner] Re-running tests...');
  triggerTestRerun();
  console.log('[tttt runner] Done running tests');
}).on('end', () => {
  console.log('[tttt runner] Stdin seems to have closed');
})
