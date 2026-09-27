// @strict: true
interface CD { kind: 1; d: string }
interface CE { kind: 2; e: string }
type CL = CD | CE;
declare function inject<T extends CL>(node: T): Extract<CL, Pick<T, "kind">>;
declare const ce: CE;
const r: CE = inject(ce);
type G<T extends CL> = Extract<CL, Pick<T, "kind">>;
