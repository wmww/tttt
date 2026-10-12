// Without skipDefaultLibCheck tsc checks the default lib's files: a lib set that isn't closed under the libs' own
// references reports inside the lib (@types/node's `/// <reference lib="esnext.intl" />` without es2021.intl:
// TS2552 DateTimeRangeFormatPart in lib.esnext.intl.d.ts)
// @skipDefaultLibCheck: false
// @lib: es2020
/// <reference lib="esnext.intl" />
export const x: number = 1;
