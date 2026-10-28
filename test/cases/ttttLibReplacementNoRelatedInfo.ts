// A libReplacement's file is a default library to tsgo (IsSourceFileDefaultLibrary): no TS6500 pointing into it
// @target: es2015
// @traceResolution: true
// @libReplacement: true

// @Filename: /somepath/node_modules/@typescript/lib-dom/index.d.ts
// NOOP
// @Filename: /somepath/node_modules/@typescript/lib-dom/iterable.d.ts
interface DOMIterable { abc: number }
// @Filename: /somepath/tsconfig.json
{ }
// @Filename: /somepath/index.ts
/// <reference lib="dom.iterable" />
const a: DOMIterable = { abc: "Hello" }

// This should fail because libdom has been replaced
// by the module above ^
window.localStorage
