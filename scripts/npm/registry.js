// A throwaway npm registry for scripts/npm/npmtest.sh: serves the .tgz files of one directory (packuments built
// from each tarball's package.json) and 404s everything else. Test-only, never packed.
//   node registry.js DIR PORT
'use strict';
const fs = require('fs');
const path = require('path');
const http = require('http');
const zlib = require('zlib');
const crypto = require('crypto');

const [dir, port] = process.argv.slice(2);

function packageJson(tgz) {
  const tar = zlib.gunzipSync(tgz);
  for (let off = 0; off + 512 <= tar.length;) {
    const name = tar.subarray(off, off + 100).toString().replace(/\0.*$/s, '');
    const size = parseInt(tar.subarray(off + 124, off + 136).toString().replace(/\0.*$/s, ''), 8);
    off += 512;
    if (isNaN(size)) break;
    if (name === 'package/package.json') return JSON.parse(tar.subarray(off, off + size).toString());
    off += (size + 511) & ~511;
  }
  throw new Error('no package.json');
}

const packuments = {};
const files = {};
for (const file of fs.readdirSync(dir).filter((f) => f.endsWith('.tgz'))) {
  const data = fs.readFileSync(path.join(dir, file));
  const pkg = packageJson(data);
  files[file] = data;
  const doc = (packuments[pkg.name] ||= { name: pkg.name, 'dist-tags': {}, versions: {} });
  doc.versions[pkg.version] = {
    ...pkg,
    _id: `${pkg.name}@${pkg.version}`,
    dist: {
      tarball: `http://localhost:${port}/-/${file}`,
      shasum: crypto.createHash('sha1').update(data).digest('hex'),
      integrity: 'sha512-' + crypto.createHash('sha512').update(data).digest('base64'),
    },
  };
  doc['dist-tags'].latest = pkg.version;
  // Published long ago: Yarn 4 quarantines versions younger than a day (npmMinimalAgeGate), or without a time.
  doc.time = { ...doc.time, created: '2020-01-01T00:00:00.000Z', modified: '2020-01-01T00:00:00.000Z', [pkg.version]: '2020-01-01T00:00:00.000Z' };
}

http.createServer((req, res) => {
  const url = decodeURIComponent(req.url.split('?')[0]);
  const file = url.startsWith('/-/') && files[url.slice(3)];
  const doc = packuments[url.slice(1)];
  if (file) {
    res.writeHead(200, { 'content-type': 'application/octet-stream' });
    res.end(file);
  } else if (doc) {
    res.writeHead(200, { 'content-type': 'application/json' });
    res.end(JSON.stringify(doc));
  } else {
    res.writeHead(404, { 'content-type': 'application/json' });
    res.end('{"error":"not found"}');
  }
}).listen(port, '127.0.0.1', () => console.log(`registry on ${port}: ${Object.keys(packuments).join(' ')}`));
