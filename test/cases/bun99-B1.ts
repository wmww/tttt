// @noTypesAndSymbols: true
// @filename: tsconfig.json
{"compilerOptions":{"strict":true,"noEmit":true,"types":[],"lib":["es2022"],"noUnusedLocals":true,"exactOptionalPropertyTypes":true}}
// @filename: a.ts
type O = { a?: { b?: number; f?(): O | undefined; list?: string[] } | null; n: number | undefined; s?: string };
declare let o: O | undefined | null;
declare let u: number | undefined;
declare let arr: (number | undefined)[] | undefined;
declare function take(n: number): void;
declare function gen<T>(t: T): T;
declare const k: "a" | undefined;
class C { p?: number; m() { return this.p!; } static s?: C; }
{ const v1: never = o!; }
{ const v2: never = o!!; }
{ const v3: never = o!!!; }
{ const v4: never = o!.a; }
{ const v5: never = o!!.a; }
{ const v6: never = o!!!.a; }
{ const v7: never = o!.a!.b; }
{ const v8: never = o!!.a!!.b; }
{ const v9: never = o!!!.a!!!.b; }
{ const v10: never = o!.a!.b!; }
{ const v11: never = o!!.a!!.b!!; }
{ const v12: never = o!!!.a!!!.b!!!; }
{ const v13: never = o?.a!.b; }
{ const v14: never = o?.a!!.b; }
{ const v15: never = o?.a!!!.b; }
{ const v16: never = o?.a!?.b!; }
{ const v17: never = o?.a!!?.b!!; }
{ const v18: never = o?.a!!!?.b!!!; }
{ const v19: never = o?.a!.f!()!.n; }
{ const v20: never = o?.a!!.f!!()!!.n; }
{ const v21: never = o?.a!!!.f!!!()!!!.n; }
{ const v22: never = (o!).n!; }
{ const v23: never = (o!!).n!!; }
{ const v24: never = (o!!!).n!!!; }
{ const v25: never = o!["a"]!; }
{ const v26: never = o!!["a"]!!; }
{ const v27: never = o!!!["a"]!!!; }
{ const v28: never = o![k!]; }
{ const v29: never = o!![k!!]; }
{ const v30: never = o!!![k!!!]; }
take(u!);
take(u!!);
take(u!!!);
take(o!.n!);
take(o!!.n!!);
take(o!!!.n!!!);
{ const v37: never = gen(u!); }
{ const v38: never = gen(u!!); }
{ const v39: never = gen(u!!!); }
{ const v40: never = gen(o!.a!); }
{ const v41: never = gen(o!!.a!!); }
{ const v42: never = gen(o!!!.a!!!); }
{ const v43: never = [u!, o!.n!]; }
{ const v44: never = [u!!, o!!.n!!]; }
{ const v45: never = [u!!!, o!!!.n!!!]; }
{ const v46: never = { x: u!, ...o! }; }
{ const v47: never = { x: u!!, ...o!! }; }
{ const v48: never = { x: u!!!, ...o!!! }; }
{ const v49: never = typeof u!; }
{ const v50: never = typeof u!!; }
{ const v51: never = typeof u!!!; }
{ const v52: never = u! + 1; }
{ const v53: never = u!! + 1; }
{ const v54: never = u!!! + 1; }
{ const v55: never = -u!; }
{ const v56: never = -u!!; }
{ const v57: never = -u!!!; }
{ const v58: never = `${u!}`; }
{ const v59: never = `${u!!}`; }
{ const v60: never = `${u!!!}`; }
u! = 1;
u!! = 1;
u!!! = 1;
o!.n = 1;
o!!.n = 1;
o!!!.n = 1;
o!.a!.b = 1;
o!!.a!!.b = 1;
o!!!.a!!!.b = 1;
u!++;
u!!++;
u!!!++;
o!.n! += 1;
o!!.n!! += 1;
o!!!.n!!! += 1;
delete o!.a!.b;
delete o!!.a!!.b;
delete o!!!.a!!!.b;
delete o!.s!;
delete o!!.s!!;
delete o!!!.s!!!;
[u!] = [1];
[u!!] = [1];
[u!!!] = [1];
({ n: u! } = { n: 1 });
({ n: u!! } = { n: 1 });
({ n: u!!! } = { n: 1 });
for (u! of [1]) {}
for (u!! of [1]) {}
for (u!!! of [1]) {}
for (const x91 of arr!) { take(x91!); }
for (const x92 of arr!!) { take(x92!!); }
for (const x93 of arr!!!) { take(x93!!!); }
if (u!) { take(u); }
if (u!!) { take(u); }
if (u!!!) { take(u); }
if (o!.a) { take(o.a.b!); }
if (o!!.a) { take(o.a.b!!); }
if (o!!!.a) { take(o.a.b!!!); }
if (o!.a!.b) { take(o.a.b); }
if (o!!.a!!.b) { take(o.a.b); }
if (o!!!.a!!!.b) { take(o.a.b); }
if (o?.a!.b) { take(o.a.b); }
if (o?.a!!.b) { take(o.a.b); }
if (o?.a!!!.b) { take(o.a.b); }
if (u! !== undefined) { take(u); }
if (u!! !== undefined) { take(u); }
if (u!!! !== undefined) { take(u); }
if (typeof u! === "number") { take(u); }
if (typeof u!! === "number") { take(u); }
if (typeof u!!! === "number") { take(u); }
while (u!) { take(u); break; }
while (u!!) { take(u); break; }
while (u!!!) { take(u); break; }
{ const v115: never = u! ? u : 0; }
{ const v116: never = u!! ? u : 0; }
{ const v117: never = u!!! ? u : 0; }
{ const v118: never = u! ?? 0; }
{ const v119: never = u!! ?? 0; }
{ const v120: never = u!!! ?? 0; }
{ const v121: never = u! && u; }
{ const v122: never = u!! && u; }
{ const v123: never = u!!! && u; }
{ const v124: never = (u! as number); }
{ const v125: never = (u!! as number); }
{ const v126: never = (u!!! as number); }
{ const v127: never = <number>u!; }
{ const v128: never = <number>u!!; }
{ const v129: never = <number>u!!!; }
{ const v130: never = u! satisfies number; }
{ const v131: never = u!! satisfies number; }
{ const v132: never = u!!! satisfies number; }
{ const v133: never = C.s!.p!; }
{ const v134: never = C.s!!.p!!; }
{ const v135: never = C.s!!!.p!!!; }
{ const v136: never = new C().m()!; }
{ const v137: never = new C().m()!!; }
{ const v138: never = new C().m()!!!; }
{ const v139: never = arr![0]!; }
{ const v140: never = arr!![0]!!; }
{ const v141: never = arr!!![0]!!!; }
{ const v142: never = arr!.map(x => x!); }
{ const v143: never = arr!!.map(x => x!!); }
{ const v144: never = arr!!!.map(x => x!!!); }
{ function f145(x: number | undefined) { return x!; } }
{ function f146(x: number | undefined) { return x!!; } }
{ function f147(x: number | undefined) { return x!!!; } }
{ function g148(x?: O) { if (x!.a) return x.a; return x; } }
{ function g149(x?: O) { if (x!!.a) return x.a; return x; } }
{ function g150(x?: O) { if (x!!!.a) return x.a; return x; } }
{ const v151: never = null!; }
{ const v152: never = null!!; }
{ const v153: never = null!!!; }
{ const v154: never = undefined!; }
{ const v155: never = undefined!!; }
{ const v156: never = undefined!!!; }
{ const v157: never = (void 0)!; }
{ const v158: never = (void 0)!!; }
{ const v159: never = (void 0)!!!; }
{ const v160: never = 1!; }
{ const v161: never = 1!!; }
{ const v162: never = 1!!!; }
{ const v163: never = "s"!.length; }
{ const v164: never = "s"!!.length; }
{ const v165: never = "s"!!!.length; }
{ const v166: never = (() => u)()!; }
{ const v167: never = (() => u)()!!; }
{ const v168: never = (() => u)()!!!; }
{ const v169: never = gen!(1); }
{ const v170: never = gen!!(1); }
{ const v171: never = gen!!!(1); }
{ const v172: never = gen!<number>(1); }
{ const v173: never = gen!!<number>(1); }
{ const v174: never = gen!!!<number>(1); }
{ const v175: never = o!.a!.f!?.()!; }
{ const v176: never = o!!.a!!.f!!?.()!!; }
{ const v177: never = o!!!.a!!!.f!!!?.()!!!; }
{ const v178: never = o!.a!.list!.length; }
{ const v179: never = o!!.a!!.list!!.length; }
{ const v180: never = o!!!.a!!!.list!!!.length; }
const sentinel: number = "";
export {};
