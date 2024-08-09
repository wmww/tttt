const http = require('http');
const fs = require('fs');
const path = require('path');
const child_process = require('child_process');

const port = 3030;
const publicDirectory = path.join(__dirname, 'public');
const tscBaseDir = '/tmp/tsTests/'
const codePath = tscBaseDir + 'tests/cases/conformance/customTest.ts';
const tscOutputDir = tscBaseDir + 'tests/baselines/local/';
const tscTestName = 'customTest';

function surfaceError(message) {
  // TODO: send this to client
  console.error(message);
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
      // inherit good for debugging
      'ignore', // 'inherit', // stdout
      'ignore', // 'inherit', // stderr
    ]
  });
tscProcess.on('close', (code) => {
  surfaceError(`tsc baseline runner exited with code ${code}`);
});

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
    callback(
      child.exitCode,
      Buffer.concat(stdoutChunks).toString("utf-8"),
      Buffer.concat(stderrChunks).toString("utf-8"),
    )
  });
}

const ttttResults = {
  err: '',
  sym: '',
  typ: '',
  stderr: '',
};

function setTtttResults(results, callback) {
  let changed = {};
  for (let i in results) {
    if (i != 'err' && i != 'sym' && i != 'typ' && i != 'stderr') {
      throw Error('Invalid result channel ' + i);
    }
    changed[i] = results[i] != null && results[i] != ttttResults[i];
    if (changed[i]) {
      ttttResults[i] = results[i];
    }
  }
  callback(changed);
}

function parseTtttResults(stdout, results) {
  let start = 0;
  while (start < stdout.length) {
    const channel = stdout.slice(start, start + 3);
    const length = parseInt(stdout.slice(start + 4, start + 16));
    const data = stdout.slice(start + 17, start + 17 + length);
    results[channel] = data;
    start += length + 18;
  }
}

function updateTtttResults(callback) {
  let results = {
    err: null,
    sym: null,
    typ: null,
    stderr: null,
  }
  subprocessThen(
    ['ninja', '-C', path.join(__dirname, '..', '..', 'build'), 'diffapp_runner'],
    (code, stdout, stderr) => {
      if (stderr) {
        results.stderr = (results.stderr || '') + 'Compiler stderr:\n' + stderr;
      }
      if (code != 0) {
        results.stderr = (results.stderr || '') + 'Compiler exited with code ' + code;
        setTtttResults(results, callback);
        return;
      }
      subprocessThen(
        [path.join(__dirname, '..', '..', 'build', 'diffapp_runner'), codePath],
        (code, stdout, stderr) => {
          if (stderr) {
            results.stderr = (results.stderr || '') + 'Runner stderr:\n' + stderr;
          }
          if (code != 0) {
            results.stderr = (results.stderr || '') + 'Runner exited with code ' + code;
            setTtttResults(results, callback);
            return;
          }
          parseTtttResults(stdout, results);
          setTtttResults(results, callback);
        }
      )
    }
  )
}

const server = http.createServer((req, res) => {
  function serveStatic(url) {
    let filePath = path.join(publicDirectory, url);
    let extname = path.extname(filePath);

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
        if(error.code == 'ENOENT'){
          res.writeHead(404);
          res.end(`404 Not Found`);
        } else {
          res.writeHead(500);
          res.end(`Server Error: ${error.code}`);
        }
      } else {
        res.writeHead(200, { 'Content-Type': contentType });
        res.end(content, 'utf-8');
      }
    });
  }

  function serveString(content) {
    res.writeHead(200, { 'Content-Type': 'text/plain' });
    res.end(content, 'utf-8');
  }

  function serveTextFile(fullPath, ignoreMissing) {
    fs.readFile(fullPath, (error, content) => {
      if (ignoreMissing && error && error.code == 'ENOENT') {
        error = null;
        content = '';
      }
      if (error) {
        res.writeHead(500);
        res.end(`Server Error: ${error.code}`);
      } else {
        serveString(content);
      }
    });
  }

  function serveWriteTextFile(fullPath, callback) {
    if (req.method != 'POST') {
      res.writeHead(405);
      res.end(req.url + ' must be a POST request');
      callback(false);
    } else {
      let body = [];
      req.on('data', chunk => {
        body.push(chunk);
      }).on('end', () => {
        body = Buffer.concat(body).toString();
        fs.writeFile(fullPath, body, error => {
          if (error) {
            res.writeHead(500);
            res.end(`Server Error: ${error.code}`);
            callback(false);
          } else {
            res.writeHead(200);
            res.end();
            callback(true);
          }
        });
      });
    }
  }

  function serveTtttResult(result) {
    res.writeHead(500);
    res.end('Not yet implemented');
  }

  switch (req.url) {
    case '/':
      serveStatic('index.html');
      break;

    case '/getCode':
      serveTextFile(codePath, false);
      break;

    case '/setCode':
      serveWriteTextFile(codePath, success => {
        if (success) {
          tscProcess.stdin.write('\n');
          updateTtttResults(changed => {
            if (changed.stderr && ttttResults.stderr) {
              surfaceError(ttttResults.stderr);
            }
          })
        }
      });
      break;

    case '/tsc/err': serveTextFile(tscOutputDir + tscTestName + '.errors.txt', true); break;
    case '/tsc/sym': serveTextFile(tscOutputDir + tscTestName + '.symbols', false); break;
    case '/tsc/typ': serveTextFile(tscOutputDir + tscTestName + '.types', false); break;

    case '/tttt/err': serveString(ttttResults.err); break;
    case '/tttt/sym': serveString(ttttResults.sym); break;
    case '/tttt/typ': serveString(ttttResults.typ); break;
    case '/tttt/stderr': serveString(ttttResults.stderr); break;

    case '/tttt/err': serveTtttResult('err'); break;
    case '/tttt/sym': serveTtttResult('sym'); break;
    case '/tttt/typ': serveTtttResult('typ'); break;

    default:
      serveStatic(req.url);
      break;
  }
});

server.listen(port, () => {
  console.log(`Server running at http://localhost:${port}/`);
});
