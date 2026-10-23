// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type show<t> = { [k in keyof t]: t[k] } & unknown
type R<d> = d extends object ? show<{ [k in keyof d as string]: R<d> | 1 } & { b: 1 }> : never
declare const s: R<{ a: 1 }>
const t: number = s
// the two types side by side
declare let u: typeof s | Exclude<(typeof s)[string], 1>
const v: number = u
// no `as` clause
type R3<d> = d extends object ? show<{ [k in string]: R3<d> | 1 } & { b: 1 }> : never
declare const s3: R3<{ a: 1 }>
const t3: number = s3
// not an intersection, or a property in place of the index signature: one type in both tools
type R6<d> = d extends object ? show<{ [k in string]: R6<d> | 1 }> : never
declare const s6: R6<{ a: 1 }>
const t6: number = s6
type R7<d> = d extends object ? show<{ [k in "p"]: R7<d> | 1 } & { b: 1 }> : never
declare const s7: R7<{ a: 1 }>
const t7: number = s7
const sentinel: number = "";
