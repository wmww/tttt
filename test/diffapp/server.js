const http = require('http');
const fs = require('fs');
const path = require('path');

const port = 3030;
const publicDirectory = path.join(__dirname, 'public');
const tscBaseDir = '/tmp/tsTests/'
const inputPath = tscBaseDir + 'tests/cases/conformance/customTest.ts';
const tscOutputDir = tscBaseDir + 'tests/baselines/local/';
const tscTestName = 'customTest';

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
        res.writeHead(200, { 'Content-Type': 'text/plain' });
        res.end(content, 'utf-8');
      }
    });
  }

  function serveTtttResult(result) {
    res.writeHead(500);
    res.end('Not yet implemented');
  }

  switch (req.url) {
    case '/':
      serveStatic('index.html');
      break;

    case '/input':
      serveTextFile(inputPath, false);
      break;

    case '/tsc/err': serveTextFile(tscOutputDir + tscTestName + '.errors.txt', true); break;
    case '/tsc/sym': serveTextFile(tscOutputDir + tscTestName + '.symbols', false); break;
    case '/tsc/typ': serveTextFile(tscOutputDir + tscTestName + '.types', false); break;

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
