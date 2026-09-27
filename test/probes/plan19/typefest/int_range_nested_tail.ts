// @strict: true
type R<End extends number, Gap extends number = 0, List extends unknown[] = []> =
  Gap extends 0
    ? List['length'] extends End ? Exclude<List[number], never> : R<End, Gap, [...List, List['length']]>
    : never;
type A = R<500>;
declare const a: A;
const x: number = a;
