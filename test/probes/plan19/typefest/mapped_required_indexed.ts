declare const sym: unique symbol;
type X = typeof sym | undefined | { [sym]?: never };
type CK<Base, Condition> = { [Key in keyof Base]-?: Base[Key] extends Condition ? Key : never }[keyof Base];
type K = CK<{ s: string; o: { a: string } }, X>;
type CK3<Base, Condition> = { [Key in keyof Base]: Base[Key] extends Condition ? Key : never }[keyof Base];
type K3 = CK3<{ s: string; o: { a: string } }, X>;
type K4 = CK<{ s: string; o: { a: string } }, number>;
