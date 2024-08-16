let testRunnerJsContent; // The code that is going to be evaled
let triggerTestRerun; // Set by code we inject into the evaled code

{
  const fs = require('fs');
  const path = require('path');

  if (process.argv.length < 4) {
    throw Error('Not enough arguments');
  }

  const tmpDir = process.argv[2];
  const tsDir = path.join(path.dirname(__dirname), 'TypeScript');
  const initialTestSource = process.argv[3];

  if (!tmpDir.startsWith('/tmp/')) {
    throw Error('tmpDir does not start with /tmp/');
  }

  // TypeScript/src/harness/findUpDir.ts needs __dirname set to something in the TypeScript dir
  __dirname = tsDir;

  console.log('[tttt runner] Setting up ' + tmpDir + '...');
  fs.rmSync(tmpDir, { recursive: true, force: true });
  fs.mkdirSync(tmpDir);
  fs.mkdirSync(path.join(tmpDir, 'tests', 'cases', 'conformance'), { recursive: true });

  fs.writeFileSync(path.join(tmpDir, 'mytest.config'), JSON.stringify({
    runUnitTests: false,
    runners: ['conformance'],
  }));

  const testPath = path.join('tests', 'cases', 'conformance', 'customTest.ts');
  fs.copyFileSync(initialTestSource, path.join(tmpDir, testPath));

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
  const runJsPath = path.join(tsDir, 'built', 'local', 'run.js');
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
