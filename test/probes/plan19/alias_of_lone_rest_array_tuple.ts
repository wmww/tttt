type A1 = number[] extends readonly unknown[] ? 1 : 0;
const a1: A1 = 1;
type TA = readonly [...unknown[]];
type A2 = number[] extends TA ? 1 : 0;
const a2: A2 = 1;
type A3 = number[] extends readonly [...unknown[]] ? 1 : 0;
const a3: A3 = 1;
declare const n: number[];
const t: TA = n;
