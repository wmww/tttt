// origin: npm @mui-treasury/component-number-input@0.0.1-alpha.3 https://github.com/siriwatknp/mui-treasury-next tsconfig.bench.json seed 20261031
// flip: no
// packages/style-textfield-*/src/*.stories.tsx: `<TextField {...args} InputProps={{ ..., disableUnderline: true }}/>`
// with MUI's TextFieldProps a union of variants (standard has disableUnderline, outlined doesn't). tsgo relates the
// whole attributes type and reports TS2322 at the tag name (in the project under a `{/* @ts-ignore */}` line, so
// nothing shows); tttt elaborates into the attribute and reports TS2353 at the property, past the ts-ignore.
// @jsx: preserve
// @strict: true
declare namespace JSX { interface Element {} interface IntrinsicElements {} interface ElementAttributesProperty { props: {} } }
interface StdInput { disableUnderline?: boolean; a?: number }
interface OutInput { notched?: boolean; a?: number }
interface StdProps { variant?: 'standard'; InputProps?: Partial<StdInput> }
interface OutProps { variant: 'outlined'; InputProps?: Partial<OutInput> }
type TFProps = StdProps | OutProps;
declare function TextField(p: TFProps): JSX.Element;
export const A = (args: TFProps) => (
  <>
    {/* @ts-ignore */}
    <TextField
      {...args}
      InputProps={{
        a: 1,
        disableUnderline: true,
      }}
    />
  </>
);
export const B = (args: TFProps) => (
  <TextField
    {...args}
    InputProps={{
      a: 1,
      disableUnderline: true,
    }}
  />
);
