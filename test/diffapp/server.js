const http = require('http');
const fs = require('fs');
const path = require('path');
const child_process = require('child_process');

const port = 3030;
// Earlier in the list takes precedence
const publicDirectories = [
  path.join(__dirname, 'public'),
  path.join(__dirname, '..', '..', 'thirdparty', 'javascript'),
];
const tscBaseDir = '/tmp/tsTests/'
const codePath = tscBaseDir + 'tests/cases/conformance/customTest.ts';
const tscOutputDir = tscBaseDir + 'tests/baselines/local/';
const tscTestName = 'customTest';
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

const tscProcess = child_process.spawn(
  'node',
  [path.join(__dirname, 'tsc_custom_baseline.js')],
  {
    env: {
      NODE_PATH: path.join(path.dirname(__dirname), 'TypeScript', 'node_modules')
    },
    stdio: [
      'pipe', // stdin
      'pipe', // stdout
      'inherit', // stderr
    ]
  });
tscProcess.stdout.on('data', chunk => {
  if (chunk.toString("utf-8").includes('[tttt runner] Done running tests\n')) {
    tscUpdateRunning = false;
    function updateResult(channel) {
      const filePath = getOrThrow(tscPathForChannel, channel);
      const ignoreMissing = (channel == 'err');
      setPropertyToContentsOf('result.tsc.' + channel, filePath, ignoreMissing);
    }
    updateResult('err');
    updateResult('sym');
    updateResult('typ');
    if (tscUpdateQueued) {
      tscUpdateQueued = false;
      updateTscResults();
    }
  }
});
tscProcess.on('close', (code) => {
  surfaceError(`tsc baseline runner exited with code ${code}`);
});

let tscUpdateRunning = true;
let tscUpdateQueued = false;
function updateTscResults() {
  if (tscUpdateRunning) {
    tscUpdateQueued = true;
  } else {
    tscProcess.stdin.write('\n');
    tscUpdateRunning = true;
  }
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
        Buffer.concat(stdoutChunks).toString("utf-8"),
        Buffer.concat(stderrChunks).toString("utf-8"),
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

let ttttStderr = '';
function updateTtttResults() {
  const resultStderr = [];
  let results = {
    err: '[tttt failed]',
    sym: '[tttt failed]',
    typ: '[tttt failed]',
  };

  function sendResults() {
    newStderr = resultStderr.join('\n\n');
    if (newStderr != ttttStderr) {
      ttttStderr = newStderr;
      if (newStderr) {
        surfaceError(newStderr);
      }
    }
    setProperty('result.tttt.err', results.err);
    setProperty('result.tttt.sym', results.sym);
    setProperty('result.tttt.typ', results.typ);
  }

  subprocessThen(
    ['ninja', '-C', path.join(__dirname, '..', '..', 'build'), 'diffapp_runner'],
    (code, stdout, stderr) => {
      if (stderr) {
        resultStderr.push('C compiler:\n' + stderr);
      }
      if (code != 0) {
        resultStderr.push('Compiler exited with code ' + code);
        sendResults();
        return;
      }
      subprocessThen(
        [path.join(__dirname, '..', '..', 'build', 'diffapp_runner'), codePath],
        (code, stdout, stderr) => {
          if (stderr) {
            resultStderr.push('tttt:\n' + stderr);
          }
          if (code != 0) {
            resultStderr.push('Runner exited with code ' + code);
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

setTimeout(() => {
  setPropertyToContentsOf('code', codePath);
}, 1000);

function filePathFor(url) {
  for (const dir of publicDirectories) {
    let filePath = path.join(dir, url);
    if (fs.existsSync(filePath)) {
      return filePath;
    }
  }
  return null;
}

const server = http.createServer((request, response) => {
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

  function serveWriteTextFile(fullPath, callback) {
    if (request.method != 'POST') {
      response.writeHead(405);
      response.end(url.pathname + ' must be a POST request');
      callback(null);
    } else {
      let body = [];
      request.on('data', chunk => {
        body.push(chunk);
      }).on('end', () => {
        body = Buffer.concat(body).toString();
        fs.writeFile(fullPath, body, error => {
          if (error) {
            response.writeHead(500);
            response.end(`Server Error: ${error.code}`);
            callback(null);
          } else {
            response.writeHead(200);
            response.end();
            callback(body);
          }
        });
      });
    }
  }

  function getToken() {
    const token = url.searchParams.get('token');
    if (!token) { throw Error('No token'); }
    return token;
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
        serveWriteTextFile(codePath, contents => {
          if (contents !== null) {
            setProperty('code', contents, token);
            updateTscResults();
            updateTtttResults();
          }
        });
        break;

      default:
        serveStatic(url.pathname);
        break;
    }
  } catch (e) {
    console.error(e);
    response.writeHead(500);
    response.end('Server Error: ' + e);
  }
});

server.listen(port, () => {
  console.log(`Server running at http://localhost:${port}/`);
});
