// @noTypesAndSymbols: true
// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "ES2022", "module": "ESNext", "moduleResolution": "bundler", "types": []}, "files": ["index.ts", "while_plain-chained.ts", "while_array-chained.ts", "while_array-direct.ts", "while_array-two.ts", "while_object-chained.ts", "while_object-renamed.ts", "while_object-index.ts", "while_nested.ts", "while_array-default.ts", "while_array-rest.ts", "while_object-rest.ts", "while_longer-chain.ts", "while_chain-of-calls.ts", "while_element-access.ts", "while_array-of-element.ts", "while_parenthesized.ts", "while_non-null.ts", "while_as.ts", "while_let.ts", "while_optional-call.ts", "for_plain-chained.ts", "for_array-chained.ts", "for_object-chained.ts", "for_nested.ts", "for_element-access.ts", "do_plain-chained.ts", "do_array-chained.ts", "do_object-chained.ts", "do_nested.ts", "do_element-access.ts"]}
// @filename: do_array-chained.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
do {
	if (cursor === null) break;
	const [row] = where(cursor).limit();
	cursor = (row)?.parent ?? null;
} while (true);
const sentinel: number = "";
export {};
// @filename: do_element-access.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
do {
	if (cursor === null) break;
	const row = where(cursor).limit()[0];
	cursor = (row)?.parent ?? null;
} while (true);
const sentinel: number = "";
export {};
// @filename: do_nested.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
do {
	if (cursor === null) break;
	const { row: { parent } } = where(cursor).obj();
	cursor = ({ parent })?.parent ?? null;
} while (true);
const sentinel: number = "";
export {};
// @filename: do_object-chained.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
do {
	if (cursor === null) break;
	const { row } = where(cursor).obj();
	cursor = (row)?.parent ?? null;
} while (true);
const sentinel: number = "";
export {};
// @filename: do_plain-chained.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
do {
	if (cursor === null) break;
	const r = where(cursor).limit();
	cursor = (r[0])?.parent ?? null;
} while (true);
const sentinel: number = "";
export {};
// @filename: for_array-chained.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
for (; cursor !== null; ) {
	const [row] = where(cursor).limit();
	cursor = (row)?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: for_element-access.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
for (; cursor !== null; ) {
	const row = where(cursor).limit()[0];
	cursor = (row)?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: for_nested.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
for (; cursor !== null; ) {
	const { row: { parent } } = where(cursor).obj();
	cursor = ({ parent })?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: for_object-chained.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
for (; cursor !== null; ) {
	const { row } = where(cursor).obj();
	cursor = (row)?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: for_plain-chained.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
for (; cursor !== null; ) {
	const r = where(cursor).limit();
	cursor = (r[0])?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: index.ts
type Row = { parent: string | null };
interface Query {
	limit(): Row[];
}
declare function where(id: string): Query;

let cursor: string | null = "root";
while (cursor !== null) {
	const [row] = where(cursor).limit();
	cursor = row?.parent ?? null;
}
export {};
// @filename: while_array-chained.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
while (cursor !== null) {
	const [row] = where(cursor).limit();
	cursor = (row)?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: while_array-default.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
while (cursor !== null) {
	const [row = null! as Row] = where(cursor).limit();
	cursor = (row)?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: while_array-direct.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
while (cursor !== null) {
	const [row] = rows(cursor);
	cursor = (row)?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: while_array-of-element.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
while (cursor !== null) {
	const [row] = [where(cursor).one()];
	cursor = (row)?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: while_array-rest.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
while (cursor !== null) {
	const [...all] = where(cursor).limit();
	cursor = (all[0])?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: while_array-two.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
while (cursor !== null) {
	const [row, n] = where(cursor).pair();
	cursor = (row)?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: while_as.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
while (cursor !== null) {
	const [row] = where(cursor).limit() as Row[];
	cursor = (row)?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: while_chain-of-calls.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
while (cursor !== null) {
	const [row] = api.where(cursor).q.limit();
	cursor = (row)?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: while_element-access.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
while (cursor !== null) {
	const row = where(cursor).limit()[0];
	cursor = (row)?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: while_let.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
while (cursor !== null) {
	let [row] = where(cursor).limit();
	cursor = (row)?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: while_longer-chain.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
while (cursor !== null) {
	const [row] = where(cursor).self.self.limit();
	cursor = (row)?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: while_nested.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
while (cursor !== null) {
	const { row: { parent } } = where(cursor).obj();
	cursor = ({ parent })?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: while_non-null.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
while (cursor !== null) {
	const [row] = where(cursor).limit()!;
	cursor = (row)?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: while_object-chained.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
while (cursor !== null) {
	const { row } = where(cursor).obj();
	cursor = (row)?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: while_object-index.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
while (cursor !== null) {
	const { 0: row } = where(cursor).limit();
	cursor = (row)?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: while_object-renamed.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
while (cursor !== null) {
	const { row: x } = where(cursor).obj();
	cursor = (x)?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: while_object-rest.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
while (cursor !== null) {
	const { n, ...others } = where(cursor).obj();
	cursor = (others.row)?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: while_optional-call.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
while (cursor !== null) {
	const [row] = where(cursor)?.limit();
	cursor = (row)?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: while_parenthesized.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
while (cursor !== null) {
	const [row] = (where(cursor).limit());
	cursor = (row)?.parent ?? null;
}
const sentinel: number = "";
export {};
// @filename: while_plain-chained.ts
type Row = { parent: string | null };
interface Query { limit(): Row[]; one(): Row; pair(): [Row, number]; obj(): { row: Row; n: number }; self: Query; q: { limit(): Row[] } }
declare function where(id: string): Query;
declare function rows(id: string): Row[];
declare const api: { where(id: string): Query };
let cursor: string | null = "root";
while (cursor !== null) {
	const r = where(cursor).limit();
	cursor = (r[0])?.parent ?? null;
}
const sentinel: number = "";
export {};
