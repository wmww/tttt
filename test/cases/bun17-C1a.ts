// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Matrix<T> = Row<T>[]; type Row<T> = T[];
type Grid<T> = Line<T>[]; type Line<T> = T[];
declare function flat<T>(m: Matrix<T>): T;
declare let grid: Grid<number>;
flat(grid).toFixed();
const sentinel: number = "";
