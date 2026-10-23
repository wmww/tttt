// @noEmit: true

// A union whose members disagree on a property's visibility has no such property (tsc's
// createUnionOrIntersectionProperty), so it is no spelling suggestion either: TS2339, not TS2551
class Default { member: string; }
class Public { public member: string; }
class Protected { protected member: string; }
class Private { private member: number; }
declare var v5: Default | Public;
declare var v11: Default | Public | Protected;
declare var v12: Default | Private;
v5.memberX;
v11.memberX;
v12.memberX;
