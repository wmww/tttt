// A property every member of an intersection of more than 16 members has is the intersection of all of their property
// types, not of the first 16 (better-auth's client, `UnionToIntersection` of one `{ organization: ... }` per endpoint:
// `authClient.organization.update` was a spurious TS2339 in dokploy, fixed in 91d6a206)
declare const r:
    { o: { a0: 0 } } &
    { o: { a1: 1 } } &
    { o: { a2: 2 } } &
    { o: { a3: 3 } } &
    { o: { a4: 4 } } &
    { o: { a5: 5 } } &
    { o: { a6: 6 } } &
    { o: { a7: 7 } } &
    { o: { a8: 8 } } &
    { o: { a9: 9 } } &
    { o: { a10: 10 } } &
    { o: { a11: 11 } } &
    { o: { a12: 12 } } &
    { o: { a13: 13 } } &
    { o: { a14: 14 } } &
    { o: { a15: 15 } } &
    { o: { a16: 16 } } &
    { o: { a17: 17 } } &
    { o: { a18: 18 } };

r.o.a0;
r.o.a15;
r.o.a16;
r.o.a18;
const n: 18 = r.o.a18;
r.o.missing;
