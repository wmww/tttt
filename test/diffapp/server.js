const http = require('http');
const fs = require('fs');
const path = require('path');
const child_process = require('child_process');

const port = 3030;
// Earlier in the list takes precedence
const ttttRoot = path.dirname(path.dirname(__dirname));
const ttttBuildDir = path.join(ttttRoot, 'debug');
const tscRoot = path.join(ttttRoot, 'test', 'TypeScript');
const publicDirectories = [
  path.join(__dirname, 'public'),
  path.join(ttttRoot, 'thirdparty', 'javascript'),
];
const tmpBaseDir = path.join('/tmp', 'diffapp');
const codePath = path.join(tmpBaseDir, 'tests', 'cases', 'conformance', 'customTest.ts');
const tscOutputDir = path.join(tmpBaseDir, 'tests', 'baselines', 'local');
const tscTestName = 'customTest';
let initialTestPath = path.join(__dirname, 'defaultCustomTest.ts');
if (process.argv.length >= 3 && process.argv[2]) {
  initialTestPath = process.argv[2];
  console.log('Loading test from ' + initialTestPath);
}
const tscPathForChannel = new Map([
  ['err', path.join(tscOutputDir, tscTestName + '.errors.txt')],
  ['sym', path.join(tscOutputDir, tscTestName + '.symbols')],
  ['typ', path.join(tscOutputDir, tscTestName + '.types')],
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
// A list of runners (generally one for tttt and one for tsc)
const runners = [];

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
      try {
        sendEvents();
      } catch (e) {
        console.error('Sending events: ' + e);
      }
    });
  }
}

function sendEvents() {
  sendEventsQueued = false;
  for (const [token, response] of responses) {
    const session = sessions.get(token);
    if (!session) {
      try {
        console.error('Invalid token ' + token);
        response.writeHead(500);
        response.end('Invalid token');
      } catch (e) {
        console.error('Sending invalid token response: ' + e);
      }
      responses.delete(token);
    } else if (session.size !== 0) {
      try {
        let message = {};
        for (const key of session) {
          message[key] = properties.get(key);
        }
        session.clear();
        response.writeHead(200, { 'Content-Type': 'application/json' });
        response.end(JSON.stringify(message), 'utf-8');
      } catch (e) {
        console.error('Sending events response: ' + e);
      }
      responses.delete(token);
    }
  }
}

function addPendingResponse(token, response) {
  const prev = responses.get(token);
  if (prev) {
    try {
      prev.writeHead(400);
      prev.end('Got newer events request');
    } catch (e) {
      console.error('Failed to cancel overridden request: ' + e);
    }
  }
  responses.set(token, response);
  if (!sessions.has(token)) {
    sessions.set(token, new Set(properties.keys()));
  }
  sendEvents();
}

function resetToken(token) {
  const prev = responses.get(token);
  if (prev) {
    responses.delete(token);
    prev.writeHead(400);
    prev.end('Request cancelled');
  }
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
      buffer = '[error reading ' + filePath + ': ' + error.code + ']';
    }
  }
  setProperty(key, buffer);
}

function clearAndInvokeAll(funcs) {
  const local = funcs.slice();
  funcs.length = 0;
  for (const func of local) {
    try {
      if (func) {
        func();
      }
    } catch (e) {
      surfaceError(e);
    }
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
        Buffer.concat(stdoutChunks).toString('utf-8'),
        Buffer.concat(stderrChunks).toString('utf-8'),
      );
    } catch (e) {
      surfaceError('Error handling ' + command + ' output: ' + e);
    }
  });
}

class TscRunner {
  constructor(name) {
    this.name = name;
    this.inProgressSerial = null;
    this.process = null;
    this.onDone = [];
  }

  startSubprocess(initialSerial) {
    if (this.process) {
      surfaceError('Tried to start ' + this.name + ' runner when it was already running');
      return;
    }

    this.inProgressSerial = initialSerial;

    this.process = child_process.spawn(
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
      }
    );

    this.process.stdout.on('data', chunk => {
      // Yes, it says [tttt runner] because while the script that generates this is our tsc runner, it's in the tttt
      // project and here it's distinguishing itself from the rest of the tsc output.
      if (chunk.toString('utf-8').includes('[tttt runner] Done running tests\n')) {
        if (!this.inProgressSerial) {
          surfaceError('tsc claims to be done with a test run when no run was known to be in-flight');
        }
        const updateResult = channel => {
          const filePath = getOrThrow(tscPathForChannel, channel);
          const ignoreMissing = (channel == 'err');
          setPropertyToContentsOf('result.' + this.name + '.' + channel, filePath, ignoreMissing);
        }
        updateResult('err');
        updateResult('sym');
        updateResult('typ');
        setProperty('result.tsc.serial', this.inProgressSerial);
        this.inProgressSerial = null;
        clearAndInvokeAll(this.onDone);
      }
    });

    this.process.on('close', (code) => {
      if (this.process) {
        this.process = null;
        surfaceError('tsc baseline runner exited with code ' + code);
      }
    });
  }

  stop() {
    if (this.process) {
      const local = this.process;
      this.process = null;
      local.kill()
    }
  }

  kick(serial, onDone) {
    if (this.inProgressSerial) {
      surfaceError('Tried to start a ' + this.name + ' run while it was already running');
    }
    this.onDone.push(onDone);
    if (this.process) {
      this.inProgressSerial = serial;
      // This should kick off a re-run of the tests
      this.process.stdin.write('\n');
    } else {
      this.startSubprocess(serial);
    }
  }
}

class TtttRunner {
  constructor(name) {
    this.name = name;
    this.inProgressSerial = null;
    this.onDone = [];
  }

  parseResults(stdout) {
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

  sendResults(results, stderrArray) {
    const stderr = stderrArray.join('\n');
    setProperty('result.' + this.name + '.err', results.err);
    setProperty('result.' + this.name + '.sym', results.sym);
    setProperty('result.' + this.name + '.typ', results.typ);
    setProperty('result.' + this.name + '.serial', this.inProgressSerial);
    setProperty('result.' + this.name + '.stderr', stderr);
    this.inProgressSerial = null;
    clearAndInvokeAll(this.onDone);
  }

  startSubprocess() {
    const stderrArray = [];
    let results = {
      err: '[tttt failed]',
      sym: '[tttt failed]',
      typ: '[tttt failed]',
    };

    subprocessThen(
      ['ninja', '-C', ttttBuildDir, 'diffapp_runner'],
      (code, stdout, stderr) => {
        if (code !== 0) {
          if (stdout) {
            stderrArray.push('C compiler:\n' + stdout);
          }
          stderrArray.push('Compiler exited with code ' + code);
          this.sendResults(results, stderrArray);
          return;
        }
        const runnerArgs = [
          'valgrind',
          '--leak-check=full',
          '--show-leak-kinds=all',
          '--exit-on-first-error=yes',
          '--error-exitcode=69',
          '--num-callers=30',
          '--quiet',
          path.join(ttttBuildDir, 'diffapp_runner'),
          codePath
        ];
        subprocessThen(
          runnerArgs,
          (code, stdout, stderr) => {
            if (stderr) {
              stderrArray.push(stderr);
            }
            if (code === 69) {
              stderrArray.push('Valgrind error');
              this.sendResults(results, stderrArray);
              return;
            } else if (code !== 0) {
              stderrArray.push('tttt runner exited with code ' + code);
              this.sendResults(results, stderrArray);
              return;
            }
            results = this.parseResults(stdout, results);
            this.sendResults(results, stderrArray);
          }
        )
      }
    )
  }

  stop() {}

  kick(serial, onDone) {
    if (this.inProgressSerial) {
      surfaceError('Tried to start a ' + this.name + ' run while it was already running');
    }
    this.onDone.push(onDone);
    this.inProgressSerial = serial;
    this.startSubprocess();
  }
}

class RunnerManager {
  constructor() {
    this.isRunning = false;
    this.runners = new Map();
    this.onDone = [];
    this.queuedCommand = null;
    this.currentSerial = 1;
  }

  addRunner(runner) {
    this.runners.set(runner.name, runner);
  }

  allRunners() {
    return [...this.runners.keys()];
  }

  maybeRun() {
    if (this.queuedCommand === null || this.isRunning) {
      return;
    }
    const forEachRunner = (names, cb) => {
      for (const name of names) {
        try {
          cb(this.runners.get(name));
        } catch (e) {
          surfaceError(e);
        }
      }
    }
    const command = this.queuedCommand;
    this.queuedCommand = null;
    this.isRunning = true;
    setProperty('running', true);
    forEachRunner(command.stop, runner => runner.stop());
    if (command.code !== null) {
      fs.writeFile(codePath, command.code, error => {
        if (error) {
          surfaceError('Writing code: ' + error.code);
          this.maybeDone();
        } else {
          forEachRunner(command.kick, runner => runner.kick(command.serial, () => this.maybeDone()));
        }
      });
    } else {
      forEachRunner(command.kick, runner => runner.kick(command.serial, () => this.maybeDone()));
    }
  }

  maybeDone() {
    if (!this.isRunning) {
      surfaceError('RunnerManager.maybeDone() called when not running');
      return;
    }
    let isDone = true;
    for (const runner of this.runners.values()) {
      if (runner.inProgressSerial !== null) {
        isDone = false;
      }
    }
    if (isDone) {
      this.isRunning = false;
      setProperty('running', false);
      clearAndInvokeAll(this.onDone);
      this.maybeRun();
    }
  }

  addCommand(command, onDone) {
    if (!this.queuedCommand) {
      this.queuedCommand = {
        code: null,
        serial: 1,
        stop: new Set(),
        kick: new Set(),
      }
    }
    if (typeof command.code === 'string') {
      this.queuedCommand.code = command.code;
      this.currentSerial++;
    }
    const serial = this.currentSerial;
    this.queuedCommand.serial = serial;
    if (command.stop) {
      command.stop.forEach(item => this.queuedCommand.stop.add(item));
    }
    if (command.kick) {
      command.kick.forEach(item => this.queuedCommand.kick.add(item));
    }
    this.onDone.push(onDone);
    this.maybeRun();
    return serial;
  }
}

const manager = new RunnerManager();

function filePathFor(url) {
  for (const dir of publicDirectories) {
    let filePath = path.join(dir, url);
    if (fs.existsSync(filePath)) {
      return filePath;
    }
  }
  return null;
}

function serveStatic(response, url) {
  let filePath = filePathFor(url);
  if (!filePath) {
    response.writeHead(404);
    response.end('404 Not Found');
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
      response.end('Server Error: ' + error.code);
    } else {
      response.writeHead(200, { 'Content-Type': contentType });
      response.end(content, 'utf-8');
    }
  });
}

function receivePostBody(request, response, callback) {
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

function findFileIn(dir, name) {
  let full = path.join(dir, name);
  if (fs.existsSync(full)) {
    return full;
  }
  for (const item of fs.readdirSync(dir)) {
    const subdir = path.join(dir, item);
    if (fs.lstatSync(subdir).isDirectory()) {
      const found = findFileIn(subdir, name);
      if (found) {
        return found;
      }
    }
  }
  return null;
}

function editorJumpTo(file, line, char) {
  const editor = 'kate';
  let tmp;
  if (fs.existsSync(file)) {
  } else if (fs.existsSync(tmp = path.join(ttttRoot, file))) {
    file = tmp;
  } else if (fs.existsSync(tmp = path.join(ttttRoot, 'src', file))) {
    file = tmp;
  } else if (tmp = findFileIn(ttttRoot, file)) {
    file = tmp
  } else {
    throw Error('Could not find file ' + file);
  }
  if (line ?? null !== null) {
    file += ':' + line;
    if (char ?? null !== null) {
      file += ':' + char;
    }
  }
  //console.log('Jumping ' + editor + ' to ' + file);
  child_process.spawn(editor, [file]);
}

function getTokenFrom(url) {
  const token = url.searchParams.get('token');
  if (!token) { throw Error('No token'); }
  return token;
}

function handleRequest(request, response) {
  try {
    const url = new URL('http://' + (process.env.HOST ?? 'localhost') + request.url);

    switch (url.pathname) {
      case '/':
        serveStatic(response, 'index.html');
        break;

      case '/events':
        addPendingResponse(getTokenFrom(url), response);
        break;

      case '/reset':
        resetToken(getTokenFrom(url));
        response.writeHead(200);
        response.end();
        break;

      case '/setCode':
        const token = getTokenFrom(url);
        receivePostBody(request, response, contents => {
          const serial = manager.addCommand({
            code: contents,
            kick: manager.allRunners(),
          });
          setProperty('code.text', contents, token);
          setProperty('code.serial', serial, token);
          response.writeHead(200);
          response.end(String(serial));
        });
        break;

      case '/jumpTo':
        const file = url.searchParams.get('file');
        const line = url.searchParams.get('line');
        const char = url.searchParams.get('char');
        if (!file) {
          response.writeHead(400);
          response.end('No filename');
        } else {
          editorJumpTo(file, line, char);
          response.writeHead(200);
          response.end();
        }
        break;

      default:
        serveStatic(response, url.pathname);
        break;
    }
  } catch (e) {
    console.error(e);
    surfaceError('Server failed to handle ' + request.url + ' request: ' + e.message);
    response.writeHead(500);
    response.end('Server Error: ' + e);
  }
}

function startServer() {
  setProperty('errors', []);
  setProperty('running', false);
  setPropertyToContentsOf('code.text', initialTestPath);
  setProperty('code.serial', manager.currentSerial);

  // Protect from wiping anything important
  if (!tmpBaseDir.startsWith('/tmp/') || tmpBaseDir == '/tmp/') {
    throw Error('tmpBaseDir does not start with /tmp/');
  }
  fs.rmSync(tmpBaseDir, { recursive: true, force: true });
  fs.mkdirSync(tmpBaseDir);
  fs.mkdirSync(path.join(tmpBaseDir, 'tests', 'cases', 'conformance'), { recursive: true });
  fs.copyFileSync(initialTestPath, codePath);

  manager.addRunner(new TscRunner('tsc'));
  manager.addRunner(new TtttRunner('tttt'));
  manager.addCommand({
    kick: manager.allRunners(),
  });

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
      manager.addCommand({
        stop: ['tttt'],
        kick: ['tttt'],
      });
    }, 50);
  });

  const server = http.createServer(handleRequest);
  server.listen(port, () => {
    console.log('Server running at http://localhost:' + port + '/');
  });
}

startServer();
