const http = require('http');
const fs = require('fs');
const path = require('path');
const child_process = require('child_process');

const port = 3030;
// Earlier in the list takes precedence
const ttttRoot = path.dirname(path.dirname(__dirname));
const tscRoot = path.join(ttttRoot, 'test', 'TypeScript');
const publicDirectories = [
  path.join(__dirname, 'public'),
  path.join(ttttRoot, 'thirdparty', 'javascript'),
];
const tmpBaseDir = '/tmp/diffapp/';
const codePath = tmpBaseDir + 'tests/cases/conformance/customTest.ts';
const tscOutputDir = tmpBaseDir + 'tests/baselines/local/';
const tscTestName = 'customTest';
let initialTestPath = path.join(__dirname, 'defaultCustomTest.ts');
if (process.argv.length >= 3 && process.argv[2]) {
  initialTestPath = process.argv[2];
  console.log('Loading test from ' + initialTestPath);
}
const tscPathForChannel = new Map([
  ['err', tscOutputDir + tscTestName + '.errors.txt'],
  ['sym', tscOutputDir + tscTestName + '.symbols'],
  ['typ', tscOutputDir + tscTestName + '.types'],
]);
let sendEventsQueued = false;
// A map of keys to values, which make up the state that is synced to clients. Some keys may have dots in their name,
// which means they're logically in a namespace
const properties = new Map();
// A map of session tokens to sets of property names that are stale for the given session. All sessions we've ever seen
// should be here
const sessions = new Map();
// A map of session tokens to response objects, only sessions with currently open events requests should be here
const responses = new Map();

function getOrThrow(map, key) {
  const ret = map.get(key);
  if (ret === undefined) {
    throw Error(`Invalid key ${key}`);
  }
  return ret;
}

function queueSendEvents() {
  if (!sendEventsQueued) {
    sendEventsQueued = true;
    setImmediate(() => {
      sendEvents();
    });
  }
}

function sendEvents() {
  sendEventsQueued = false;
  for (const [token, response] of responses) {
    const session = sessions.get(token);
    if (!session) {
      console.error('Invalid token ' + token);
      response.writeHead(500);
      response.end('Invalid token');
      responses.delete(token);
    } else if (session.size !== 0) {
      message = {}
      for (const key of session) {
        message[key] = properties.get(key);
      }
      session.clear();
      response.writeHead(200, { 'Content-Type': 'application/json' });
      response.end(JSON.stringify(message), 'utf-8');
      responses.delete(token);
    }
  }
}

function addPendingResponse(token, response) {
  const prev = responses.get(token);
  if (prev) {
    response.writeHead(400);
    response.end('Got newer events request');
  }
  responses.set(token, response);
  if (!sessions.has(token)) {
    sessions.set(token, new Set(properties.keys()));
  }
  sendEvents();
}

// If sourceToken is set, it will not be notified (it's the soure of the change, so it already knows about it)
function setProperty(key, value, sourceToken) {
  if (properties.get(key) != value) {
    properties.set(key, value);
    for (const [token, session] of sessions) {
      if (token != sourceToken) {
        session.add(key);
        queueSendEvents();
      }
    }
  }
}

const currentErrors = [];
function surfaceError(message) {
  console.error(message);
  currentErrors.push(message);
  setProperty('errors', currentErrors.slice());
  setTimeout(() => {
    currentErrors.shift(); // *should* always be the error we added
    setProperty('errors', currentErrors.slice());
  }, 10000);
}
setProperty('errors', []);

function setPropertyToContentsOf(key, filePath, ignoreMissing) {
  let buffer;
  try {
    buffer = fs.readFileSync(filePath).toString();
  } catch (error) {
    if (ignoreMissing && error.code == 'ENOENT') {
      buffer = '';
    } else {
      buffer = '[error reading file: ' + error.code + ']';
    }
  }
  setProperty(key, buffer);
}

setPropertyToContentsOf('code', initialTestPath);

let tscRunningTest = true;
const tscProcess = child_process.spawn(
  'node',
  [path.join(__dirname, 'tsc_custom_baseline.js'), tmpBaseDir, initialTestPath],
  {
    env: {
      NODE_PATH: path.join(tscRoot, 'node_modules')
    },
    stdio: [
      'pipe', // stdin
      'pipe', // stdout
      'inherit', // stderr
    ]
  });
tscProcess.stdout.on('data', chunk => {
  if (chunk.toString('utf-8').includes('[tttt runner] Done running tests\n')) {
    if (!tscRunningTest || !runningSerial) {
      surfaceError('tsc claims to be done with a test run when no run was known to be in-flight');
    }
    if (!firstRunCompleted) {
      // This will get un an initial tttt result. We do it here to be sure the code file is written
      startTtttRun();
    }
    function updateResult(channel) {
      const filePath = getOrThrow(tscPathForChannel, channel);
      const ignoreMissing = (channel == 'err');
      setPropertyToContentsOf('result.tsc.' + channel, filePath, ignoreMissing);
    }
    updateResult('err');
    updateResult('sym');
    updateResult('typ');
    setProperty('result.tsc.serial', runningSerial);
    tscRunningTest = false;
    testRunMaybeDone();
  }
});
tscProcess.on('close', (code) => {
  surfaceError(`tsc baseline runner exited with code ${code}`);
  process.exit(1);
});

function startTscRun() {
  if (tscRunningTest) {
    surfaceError('Tried to start a tsc run while it was already running');
    return
  }
  tscRunningTest = true;
  // This should kick off a re-run of the tests
  tscProcess.stdin.write('\n');
}

function subprocessThen(args, callback) {
  //console.log('Running ' + args.join(' '));
  const command = args.shift();
  const child = child_process.spawn(
    command, args,
    {
      timeout: 30000,
      stdio: [
        'ignore', // stdin
        'pipe',   // stdout
        'pipe',   // stderr
      ]
    });
  const stdoutChunks = [];
  const stderrChunks = [];
  child.stdout.on('data', chunk => stdoutChunks.push(Buffer.from(chunk)));
  child.stderr.on('data', chunk => stderrChunks.push(Buffer.from(chunk)));
  child.on("close", () => {
    try {
      callback(
        child.exitCode,
        Buffer.concat(stdoutChunks).toString('utf-8'),
        Buffer.concat(stderrChunks).toString('utf-8'),
      );
    } catch (e) {
      surfaceError('Error handling ' + command + ' output: ' + e);
    }
  });
}

function parseTtttResults(stdout) {
  let start = 0;
  const results = {};
  while (start < stdout.length) {
    const channel = stdout.slice(start, start + 3);
    const length = parseInt(stdout.slice(start + 4, start + 16));
    const data = stdout.slice(start + 17, start + 17 + length);
    results[channel] = data;
    start += length + 18;
  }
  return results;
}

let ttttRunningTest = false;
function startTtttRun() {
  if (ttttRunningTest) {
    surfaceError('Tried to start a tttt run while it was already running');
    return
  }
  ttttRunningTest = true;

  const resultStderr = [];
  let results = {
    err: '[tttt failed]',
    sym: '[tttt failed]',
    typ: '[tttt failed]',
  };

  function sendResults() {
    if (!ttttRunningTest || !runningSerial) {
      surfaceError('tttt was not supposed to be running, but finished');
    }
    joinedStderr = resultStderr.join('\n');
    if (joinedStderr) {
      surfaceError(joinedStderr);
    }
    setProperty('result.tttt.err', results.err);
    setProperty('result.tttt.sym', results.sym);
    setProperty('result.tttt.typ', results.typ);
    setProperty('result.tttt.serial', runningSerial);
    ttttRunningTest = false;
    testRunMaybeDone();
  }

  subprocessThen(
    ['ninja', '-C', path.join(ttttRoot, 'build'), 'diffapp_runner'],
    (code, stdout, stderr) => {
      if (code !== 0) {
        if (stdout) {
          resultStderr.push('C compiler:\n' + stdout);
        }
        resultStderr.push('Compiler exited with code ' + code);
        sendResults();
        return;
      }
      const runnerArgs = [
        'valgrind',
        '--leak-check=full',
        '--show-leak-kinds=all',
        '--exit-on-first-error=yes',
        '--error-exitcode=69',
        '--quiet',
        path.join(ttttRoot, 'build', 'diffapp_runner'),
        codePath
      ];
      subprocessThen(
        runnerArgs,
        (code, stdout, stderr) => {
          if (stderr) {
            resultStderr.push(stderr);
          }
          if (code === 69) {d
            resultStderr.push('Valgrind error');
            sendResults();
            return;
          } else if (code !== 0) {
            resultStderr.push('tttt runner exited with code ' + code);
            sendResults();
            return;
          }
          results = parseTtttResults(stdout, results);
          sendResults();
        }
      )
    }
  )
}

let srcChangeTimeout = null;
fs.watch(path.join(ttttRoot, 'src'), {
  persistent: true,
  recursive: true
}, (type, filename) => {
  switch (path.extname(filename)) {
    case '.c':
    case '.h':
    case '.build': break;
    default: return;
  }
  clearTimeout(srcChangeTimeout);
  srcChangeTimeout = setTimeout(() => {
    rerunTttt();
  }, 50);
});

let runningSerial = 1;
let queuedTestRun = null;
let firstRunCompleted = false;

function testRunMaybeDone() {
  if (!tscRunningTest && !ttttRunningTest) {
    runningSerial = null;
    if (!firstRunCompleted) {
      startServer();
      firstRunCompleted = true;
    }
    if (queuedTestRun) {
      const [code, serial] = queuedTestRun;
      queuedTestRun = null;
      runTests(code, serial);
    }
  }
}

function runTests(code, serial) {
  if (runningSerial) {
    queuedTestRun = [code, serial];
  } else {
    runningSerial = serial;
    fs.writeFile(codePath, code, error => {
      if (error) {
        surfaceError('Writing code: ' + error.code);
        testRunMaybeDone();
      } else {
        startTscRun();
        startTtttRun();
      }
    });
  }
}

function rerunTttt() {
  if (runningSerial) {
    surfaceError('TODO: allow queuing tttt runs');
  } else {
    runningSerial = properties.get('result.tttt.serial');
    startTtttRun();
  }
}

function filePathFor(url) {
  for (const dir of publicDirectories) {
    let filePath = path.join(dir, url);
    if (fs.existsSync(filePath)) {
      return filePath;
    }
  }
  return null;
}

function handleRequest(request, response) {
  const url = new URL(`http://${process.env.HOST ?? 'localhost'}${request.url}`);

  function serveStatic(url) {
    let filePath = filePathFor(url);
    if (!filePath) {
      response.writeHead(404);
      response.end(`404 Not Found`);
      return;
    }

    let extname = path.extname(url);
    let contentType = 'text/html';
    switch (extname) {
      case '.js':
        contentType = 'text/javascript';
        break;
      case '.css':
        contentType = 'text/css';
        break;
      case '.json':
        contentType = 'application/json';
        break;
      case '.png':
        contentType = 'image/png';
        break;
      case '.jpg':
        contentType = 'image/jpg';
        break;
    }

    fs.readFile(filePath, (error, content) => {
      if (error) {
        response.writeHead(500);
        response.end(`Server Error: ${error.code}`);
      } else {
        response.writeHead(200, { 'Content-Type': contentType });
        response.end(content, 'utf-8');
      }
    });
  }

  function receivePostBody(callback) {
    if (request.method != 'POST') {
      response.writeHead(405);
      response.end(url.pathname + ' must be a POST request');
    } else {
      let body = [];
      request.on('data', chunk => {
        body.push(chunk);
      }).on('end', () => {
        body = Buffer.concat(body).toString('utf-8');
        callback(body);
      });
    }
  }

  function getToken() {
    const token = url.searchParams.get('token');
    if (!token) { throw Error('No token'); }
    return token;
  }

  function getSerial() {
    const serial = url.searchParams.get('serial');
    if (!serial) { throw Error('No serial'); }
    return serial;
  }

  try {
    switch (url.pathname) {
      case '/':
        serveStatic('index.html');
        break;

      case '/events':
        addPendingResponse(getToken(), response);
        break;

      case '/setCode':
        const token = getToken();
        const serial = getSerial();
        receivePostBody(contents => {
          setProperty('code', contents, token);
          runTests(contents, serial);
          response.writeHead(200);
          response.end();
        });
        break;

      default:
        serveStatic(url.pathname);
        break;
    }
  } catch (e) {
    console.error(e);
    surfaceError('Server failed to handle ' + url.pathname + ' request: ' + e.message);
    response.writeHead(500);
    response.end('Server Error: ' + e);
  }
}

function startServer() {
  const server = http.createServer(handleRequest);
  server.listen(port, () => {
    console.log(`Server running at http://localhost:${port}/`);
  });
}

