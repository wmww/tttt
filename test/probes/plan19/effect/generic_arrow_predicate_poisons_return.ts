// @strict: true
declare function take<F>(f: F): F;
declare function take2(f: <S>(s: S) => [S, number]): void;
take<<S>(s: S) => [S, number]>(<S>(s: S) => [s, 1]);
take2(<S>(s: S) => [s, 1]);
declare function map<A, B>(a: A, f: (a: A) => B): B;
take2(<S>(s: S) => map(s, (x) => [x, 1]));
