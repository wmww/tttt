// origin: npm @devexpress/dx-react-grid-material-ui@4.0.11 https://github.com/DevExpress/devextreme-reactive tsconfig.bench.json seed 20261033
// flip: no
// `params={...params}` inside an arrow inside JSX children: tsgo's parseList recovery (isInSomeParsingContext: the JSX children context takes any token) unwinds to the children, the rest is JSX text (TS1381/TS1382); tttt goes on in the arrow body (TS1161, TS1005)
// @jsx: preserve
declare const T: any, P: any;
const x = (
  <T>
    {(params: any) => {
      return <P params={...params} />;
    }}
  </T>
);
