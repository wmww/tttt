// origin: npm ChatbotXIO/ChatbotX @ a4ab684 apps/builder (tsconfig.bench.json), src/features/ads/components/conversion-events-view.tsx:255
// flip: no
// A name of an annotated pattern asked for before its declaration is checked (a return expression inferred while a
// generic call infers) checks the initializer with the written type as its context, not without one
// @strict: true
declare function useMemo<T>(factory: () => T): T;
type TC = { id: string }
declare const c: boolean;
const r1 = useMemo(() => { const { fk }: { fk: (t: TC) => boolean } = c ? { fk: (t) => !t.id } : { fk: (t) => !t.id }; return fk });
const r2 = useMemo(() => { const { fk }: { fk: (t: TC) => boolean } = { fk: (t) => !t.id }; return fk });
const r3 = useMemo(() => { const x: { fk: (t: TC) => boolean } = { fk: (t) => !t.id }; return x });
const r4 = useMemo(() => { const { fk }: { fk: (t: TC) => boolean } = { fk: (t) => !t.id }; return 1 });
