// @strict: true
type R<End extends number, List extends unknown[] = []> =
  List['length'] extends End ? List[number] : R<End, [...List, List['length']]>;
type A = R<999>;
declare const a: A;
const x: number = a;
