// @strict: true
// A context-sensitive function under `&&` (or a comma) is left out of a call's first inference pass like any other:
// tsc's check mode reaches the operand though `a && f` isn't context sensitive itself. Typing it early fixed T at its
// constraint (atproto's oauth-provider-ui sign-up wizard, `hcaptchaSiteKey != null && { contentRender: ... }`)
type DisabledStep = null | undefined | false;
type Step<D = any> = { render: (p: { next: (data: D) => void }) => string };
type Props<T extends readonly any[]> = {
  onDone: (data: T) => void;
  steps: { [K in keyof T]: null extends T[K] ? Step<T[K]> | DisabledStep : Step<T[K]> };
};
declare function Wizard<const T extends readonly any[]>(p: Props<T>): T;
declare const key: string | undefined;
const r = Wizard({
  onDone: ([a, b]: [string, number | null]) => {},
  steps: [
    { render: (p) => "" },
    key != null && { render: (p) => "" },
  ],
});
const r2 = Wizard({
  onDone: ([a, b]: [string, number | null]) => {},
  steps: [
    { render: (p) => "" },
    { render: (p) => "" },
  ],
});
