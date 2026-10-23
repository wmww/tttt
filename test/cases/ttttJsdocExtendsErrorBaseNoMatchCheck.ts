// @allowJs: true
// @checkJs: true
// @noEmit: true

// A class whose base is the error type (`extends Object.PureComponent`, TS2339) has no base types in tsc, so its
// `@extends` tag isn't compared with the clause (no TS8023); one with a base still is
// @filename: react.d.ts
declare namespace React {
    class Component { component: string }
    class PureComponent { pure: string }
}

// @filename: main.js
/**
 * @extends {React.Component}
 */
class C extends Object.PureComponent {}

/**
 * @extends {React.Component}
 */
class D extends React.Component {}

/**
 * @extends {React.Component}
 */
class E extends React.PureComponent {}
