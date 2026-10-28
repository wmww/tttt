// @target: es2022
// tsc caches the awaited type of a union or a promise (getAwaitedTypeNoAlias' cachedTypes), so what it says while
// computing one (TS1320 for a thenable member, TS1062 for a cycle, TS1058 for an async function's return) it says
// the first time only; a bare thenable isn't cached and is reported each time
interface Bad { then(): void }
declare const p: Promise<string | Bad>; declare const u: string | Bad; declare const b: Bad;
async function f() { await p; await u; await b; await b; }
type S = string | Promise<S>;
declare const s: S; declare const ps: Promise<S>;
async function h() { await ps; await s; }
declare const v: number | Bad;
async function r1() { return v; }
async function r2() { return v; }
