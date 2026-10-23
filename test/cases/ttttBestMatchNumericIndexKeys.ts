// @strict: true
// findMostOverlappyType intersects keyof the source with keyof each member: a numeric index signature's key is
// number there, which a tuple's "0" isn't, so the array member overlaps less than an object with a string index
// signature, which is the best match; indexing it gives unknown and the array literal isn't elaborated into:
// TS2345 at the argument (overture-stack/sqon, npm seed 20261023)
type SNode = { [x: string]: unknown; op?: "all" } | { [x: string]: unknown; op: "and"; content: SNode[] };
declare function or(content: SNode | SNode[]): void;
const study = { op: 'in', v: 1 } as const;
const site = { op: 'in', v: 2 } as const;
or([study, site]);
