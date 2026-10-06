// @strict: true
type A = { type: "a"; x: number; id: string };
type B = { type: "b"; x: number; id: string };
type El = A | B;
declare const p1: Omit<Partial<El>, "id">;
const q1: Partial<El> = p1;
declare const p2: Pick<Partial<El>, "type" | "x">;
const q2: Partial<El> = p2;
declare const p3: Omit<Partial<El & { index: string }>, "id">;
const q3: Partial<El & { index: string }> = p3;
declare const p4: Partial<El & { index: string }>;
const q4: Partial<El & { index: string }> = p4;
