let runJsContent;

{
    const fs = require('fs');

    const tmpDir = '/tmp/tsTests';
    const tsDir = `${__dirname}/../TypeScript`;

    // TypeScript/src/harness/findUpDir.ts needs __dirname set to something in the TypeScript dir
    __dirname = tsDir;

    console.log(`Setting up ${tmpDir}...`);
    fs.rmSync(tmpDir, { recursive: true, force: true });
    fs.mkdirSync(tmpDir);
    fs.mkdirSync(`${tmpDir}/tests/cases/conformance`, { recursive: true });

    fs.writeFileSync(`${tmpDir}/mytest.config`, JSON.stringify({
        runUnitTests: false,
        runners: ['conformance'],
    }));

    fs.writeFileSync(`${tmpDir}/tests/cases/conformance/customTest.ts`, 'let foo = 22;\n');

    function assertOneReplacement(path, text, pattern, replacement) {
        let count = 0;
        const ret = text.replace(pattern, () => {
            count += 1;
            return replacement;
        });
        if (count != 1) {
            throw Error(`'${pattern}' should have appeared in ${path} exactly once, but it appeared ${count} times`);
        }
        return ret;
    }

    console.log('Loading test runner script...');
    const runJsPath = `${tsDir}/built/local/run.js`;
    runJsContent = fs.readFileSync(runJsPath) + '';

    runJsContent = assertOneReplacement(
        runJsPath,
        runJsContent,
        'userSpecifiedRoot = "";',
        `userSpecifiedRoot = "${tmpDir}/";`
    );

    // This is required for TS v5.5.4
    // runJsContent = assertOneReplacement(runJsPath, runJsContent, 'importTests();', '');

    // This can be gotten rid of when we bump to TS v5.5.4
    __filename = "Harness.js";

    process.chdir(tmpDir);
}

// Mocha mocks
function before(f) {
    f();
}
function after(f) {}
function describe(name, f) {
    console.log(name);
    try {
        f();
    } catch (e) {
        console.error(e);
    }
}
const beforeEach = before;
const afterEach = after;
const it = describe;

console.log('Evaling test runner script...');
eval(runJsContent);
console.log('Done');
