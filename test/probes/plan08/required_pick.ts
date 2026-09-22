// @strict: true
interface Tracker { report?(n: number): void; other?: string }
declare const t: Required<Pick<Tracker, "report">>;
t.report(1);
declare const r: Required<Tracker>;
const s: string = r.other;
