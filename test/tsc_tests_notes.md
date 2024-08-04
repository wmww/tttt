Everything here should be up to date as of TS v5.5.4

TypeScript is a clusterfuck.

Reading English from the TypeScript repo or elsewhere online is not an effective way to figure out what is going on. Reading code is. Most of this information was gained reading and poking code.

Hereby is some sort of generic configurable runner thing. Hereby is controller by `Herebyfile.mjs`. It's used to build everything and run tests. The TypeScript docs say to install it with `npm install -g hereby`, but this is bad and wrong. Instead install it locally with `npm install hereby`. The command to run all the tests with Hereby is `node_modules/.bin/hereby runtests`. To run a specific test it's `node_modules/.bin/hereby runtests --tests=TESTNAME`. `node_modules/.bin/hereby` without any arguments seems to build TypeScript, but not the tests. `node_modules/.bin/hereby tests` builds TypeScript and the tests.

Hereby calls Mocha, which actually runs the tests. Hereby helpfully prints the mocha command it invokes, it should look something like `node_modules/mocha/bin/_mocha -R scripts/failed-tests.cjs -O "reporter=mocha-fivemat-progress-reporter" -g "genericArrayAssignmentCompatErrors" --colors -t 40000 ./built/local/run.js`. Most of that isn't strictly required, all that's needed is `node_modules/mocha/bin/_mocha -g genericArrayAssignmentCompatErrors ./built/local/run.js` (or drop the `-g genericArrayAssignmentCompatErrors` to run all tests)

The `run.js` entry point is built from `src/testRunner/runner.ts`. Note that `run.js` is not re-built and so changes do not take effect unless you run `hereby tests`. `runner.ts` creates instances of `CompilerBaselineRunner` from `src/testRunner/compilerRunner.ts`. It calls into `src/harness/harnessIO.ts`.

Our `diffapp.sh` script clones TypeScript if needed, installs hereby, runs `hereby tests`, and runs `tsc_custom_baseline.js`. `tsc_custom_baseline.js` sets up a javascript environment to run tests in, loads `run.js` and evals it. Nothing is designed to work this way, but with enough monkey-patches it does. I think this is easier than maintaining source patches on top of TypeScript, and it doesn't appear there's any public APIs to do what I need.
