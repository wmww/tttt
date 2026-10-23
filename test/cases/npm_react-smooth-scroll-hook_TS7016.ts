// origin: npm react-smooth-scroll-hook@1.3.4 https://github.com/ron0115/react-smooth-scroll-hook tsconfig.bench.json seed 20261027
// flip: no
// A `paths` pattern `*` mapped to ./node_modules/* resolves `react` to the untyped node_modules/react/index.js under
// bundler (tsgo tries TypeScript and JavaScript files together at each candidate location, so the paths candidate wins
// with JS): TS7016, and @types/react isn't used. tttt falls back to @types/react
// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "module": "esnext", "moduleResolution": "bundler", "types": [], "noEmit": true, "paths": { "*": ["./src/*", "./node_modules/*"] } }, "include": ["src"] }
// @filename: node_modules/react/package.json
{ "name": "react", "version": "16.13.1", "main": "index.js" }
// @filename: node_modules/react/index.js
module.exports = {};
// @filename: node_modules/@types/react/package.json
{ "name": "@types/react", "version": "16.9.0", "types": "index.d.ts" }
// @filename: node_modules/@types/react/index.d.ts
export declare const x: number;
// @filename: src/a.ts
import { x } from "react";
export const y: number = x;
