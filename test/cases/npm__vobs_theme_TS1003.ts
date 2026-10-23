// origin: npm @vobs/theme@1.8.18 https://github.com/vobsjs/vobs tsconfig.bench.json seed 20261029
// flip: no
// `?.` followed by whitespace before the call's `(` or the element access's `[`: one token, then the call (TS1003 Identifier expected in tttt, and the program's other errors went missing)
declare const f: ((a: number) => number) | undefined;
declare const o: { m?: (a: number) => number; a?: number[] };
f?. (1);
f ?.(1);
o.m?.  (2);
o.a?. [0];
f?. /* c */ (3);
f?.
  (4);
