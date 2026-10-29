// An intersection's property has its members' most permissive visibility (createUnionOrIntersectionProperty): a
// protected `content` in Base and a public one in Mock make a public `content` of `Base & Mock`, which the spelling
// suggestion offers (TS2551). Seed 20261119, syntheticProtectedProperties member-typo
declare class Dummy { a: number; }
declare class Base { protected get content(): string; protected set content(value: string); }
declare class Mock { get content(): string; set content(value: string); }
declare const w: (Base & Mock) | (Dummy & Mock);
w.contentX;
declare const w2: Base & Mock;
w2.contentX;
declare const w3: Base & Dummy;
w3.contentX;
