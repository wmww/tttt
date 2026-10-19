// `bigint / any` is bigint: tsc's bothAreBigIntLike is the loose isTypeAssignableToKind, where `any` (and an unresolved
// import's error type) is bigint-like (ethereumjs's header.ts, its references unbuilt: TS6305 leaves `Common` unresolved)
// @target: es2020
// @strict: true
import { Common } from "./missing";
declare const a: bigint;
declare const anyv: any;
declare const common: Common;
const s = a / anyv;
const t = anyv * 2n;
const u = a - common.param("x");
const v = a - s - t - u;
const w: bigint = (a * a) / common.param("y");
const x = a ** anyv;
const y = 1 - anyv; // number
