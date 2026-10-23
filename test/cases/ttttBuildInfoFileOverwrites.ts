// The build info file is among the emitted paths tsc verifies (tsgo's GetBuildInfoFileName): written over a.ts's
// output it is TS5056 (over an input, TS5055).
// @filename: p/tsconfig.json
{ "compilerOptions": { "strict": true, "types": [], "incremental": true, "tsBuildInfoFile": "a.js" }, "files": ["a.ts"] }
// @filename: p/a.ts
const sentinel: number = "";
