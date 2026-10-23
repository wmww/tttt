// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "esnext", "module": "esnext", "lib": ["esnext"], "types": []}, "files": ["globals.ts", "script.ts", "main.ts", "later.ts"]}
// @filename: globals.ts
export {}
declare global {
  let counter: string | number
  var legacy: string | number
  const fixed: string | number
  let assignedHere: string | number
  let assignedThere: string | number
  namespace Space { let inside: string | number }
}
assignedHere = 1
// @filename: later.ts
export {}
if (typeof counter === 'string') { const f = () => counter.toUpperCase() }
if (typeof assignedHere === 'string') { const f = () => assignedHere.toUpperCase() }
counter = 3
// @filename: main.ts
export {}
if (typeof counter === 'string') { const f = () => counter.toUpperCase() }
if (typeof legacy === 'string') { const f = () => legacy.toUpperCase() }
if (typeof fixed === 'string') { const f = () => fixed.toUpperCase() }
if (typeof assignedHere === 'string') { const f = () => assignedHere.toUpperCase() }
if (typeof assignedThere === 'string') { const f = () => assignedThere.toUpperCase() }
if (typeof Space.inside === 'string') { const f = () => Space.inside.toUpperCase() }
if (typeof scriptLet === 'string') { const f = () => scriptLet.toUpperCase() }
if (typeof counter === 'string') { function g() { return counter.toUpperCase() } }
if (typeof counter === 'string') { const o = { m() { return counter.toUpperCase() } } }
if (typeof counter === 'string') { const probe: never = counter; const f = () => { const inner: never = counter } }
assignedThere = 2
export const sentinel: number = ''
// @filename: script.ts
let scriptLet: string | number = 1
