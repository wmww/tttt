// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "allowJs": true, "checkJs": true, "types": [], "lib": ["es2022"], "module": "commonjs" }, "files": ["a.js", "c.js"] }
// @filename: a.js
#!/usr/bin/env -S node --require ./hook
require("./B");
// @filename: b.js
module.exports = 1;
// @filename: c.js
require("./b");
