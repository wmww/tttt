// `import<T>` in a heritage clause is TS1326 from checkGrammarHeritageClause, clause by clause after the clause's own
// complaints (trailing comma, empty list), and never once the class's heritage checks stopped (TS1174 and co)
declare const v: any;
interface I {}
class X1 extends import<string> {}
class X2 extends import<string>, v {}
class X3 extends v implements import<string> {}
class X4 implements I, import<string> {}
interface I2 extends import<string> {}
interface I3 extends I, import<string> {}
class X5 extends import<string> implements I implements I {}
