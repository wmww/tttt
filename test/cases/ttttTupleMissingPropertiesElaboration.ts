// A tuple source lacking several properties of a non-array target says nothing more than the head (tsc's
// tryElaborateArrayLikeErrors), though the tuple is related as the array it extends (mutation of
// recursiveTypeReferences1)
interface H { a: string; b: number; c: boolean; d: string; e: string }
declare const t2: [H, number];
const h: H = t2;
declare const t3: [H, number][];
const h3: H[] = t3;
declare function f(x: H[]): void;
f(t3);
declare const o: { x: number }[];
const h4: H[] = o;
const h5: { a: string; length: string } = t2;
