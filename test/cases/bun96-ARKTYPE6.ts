// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type show<t> = { [k in keyof t]: t[k] } & unknown
type R<d> = d extends object ? show<{ [k: string]: R<d> | 1 } & { b: 1 }> : never
declare const s: R<{ a: 1 }>
const x = s["k"]
if (x !== 1) {
	x["z"]
	const y: number = x
}
const t: number = s
// without the mapped type: the intersection itself
type I<d> = d extends object ? { [k: string]: I<d> | 1 } & { b: 1 } : never
declare const i: I<{ a: 1 }>
const ti: number = i
const xi = i["k"]
if (xi !== 1) { xi["z"] }
const sentinel: number = "";
