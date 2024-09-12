const annotationLineRegex = /([^\r\n]*)(\r\n|\n|\r)?/y;
const newlineRegex = /\r\n|\n|\r/;

// Returns two lists of the same length, one with the line contents and the other with newlines ('\n', '\n\r', '')
function splitLinesWithNewlines(text) {
  text = text ?? '';
  annotationLineRegex.lastIndex = 0;
  const lines = [];
  const newlines = [];
  let match;
  while (match = annotationLineRegex.exec(text)) {
    lines.push(match[1]);
    if (match[2] !== undefined) {
      newlines.push(match[2]);
    } else {
      newlines.push('');
      break;
    }
  }
  return [lines, newlines];
}

// Takes a list of reference lines and a single annotation string. Diffs them (ignoring newlines), and returns
// (string[] | undefined)[]. Each non-undefined element is an array of lines, which contain the newline characters at
// the end. Each undefined represents one line of reference.
function processAnnotaiton(referenceLines, annotationString) {
  const [annotationLines, annotationNewlines] = splitLinesWithNewlines(annotationString);
  const lineDiff = Diff.diffArrays(referenceLines, annotationLines);

  let ret = [];
  let annotationIndex = 0;
  for (const elem of lineDiff) {
    if (elem.added) {
      // elem.value is a list of line strings
      const lines = [];
      for (const line of elem.value) {
        lines.push(line + annotationNewlines[annotationIndex]);
        annotationIndex++;
      }
      ret.push(lines);
    } else {
      if (!elem.removed) {
        annotationIndex += elem.value.length;
      }
      for (const _line of elem.value) {
        ret.push(undefined);
      }
    }
  }
  return ret;
}

class AnnotatedEditor extends Editor {
  constructor(inputElem, viewElem) {
    super(inputElem, viewElem);
    this.indent = null;
    // An array of tokens representing the diff of the results. It also contains nulls, each representing one
    // line (not token!) of input.
    this.annotationWithNulls = [];
    this.childConstructorDone = true;
    this.render();
  }

  // referenceValue may be out of date from the current editor value, it should be what was used to generate the
  // annotations. The annotations are two strings that should contain all the lines from the reference value, plus
  // extra annotaiton lines added. This function will pull the annotation lines out and then diff them between A and B.
  // Indent (if not null) is applied to the value before comparing to the annotations.
  setAnnotations(annotationA, annotationB, referenceValue, indent) {
    this.indent = indent;

    referenceValue = referenceValue ?? '';
    let referenceLines = referenceValue.split(newlineRegex);
    if (this.indent) {
      referenceLines = referenceLines.map(line => this.indent + line);
    }

    const processedA = processAnnotaiton(referenceLines, annotationA);
    const processedB = processAnnotaiton(referenceLines, annotationB);

    let indexA = 0;
    let indexB = 0;
    this.annotationWithNulls = [];
    while (indexA < processedA.length || indexB < processedB.length) {
      if (processedA[indexA] === undefined && processedB[indexB] === undefined) {
        this.annotationWithNulls.push(null);
        indexA++;
        indexB++;
      } else {
        let sectionLinesA = [];
        if (processedA[indexA] !== undefined) {
          sectionLinesA = processedA[indexA];
          indexA++;
        }

        let sectionLinesB = [];
        if (processedB[indexB] !== undefined) {
          sectionLinesB = processedB[indexB];
          indexB++;
        }

        for (const i of Diff.diffArrays(sectionLinesA, sectionLinesB)) {
          let className = i.added ?
            'token-diff-b' :
            (i.removed ? 'token-diff-a' : 'token-diff-both');
          for (const line of i.value) {
            tokenizeStringInto(line, this.annotationWithNulls, {
              classes: [className],
            });
          }
        }
      }
    }

    this.render();
  }

  integratedTokens() {
    let inputIndex = 0;
    const retTokens = [];

    const inputTokens = this.tokenizeValue(this.indent);

    for (const result of this.annotationWithNulls) {
      if (result === null) {
        // Push a line of input (if available)
        while (inputIndex < inputTokens.length) {
          const token = inputTokens[inputIndex];
          retTokens.push(token);
          inputIndex++;
          if (token.type === Token.NEWLINE) {
            break;
          }
        }
      } else {
        retTokens.push(result);
      }
    }

    // Push any left over input
    while (inputIndex < inputTokens.length) {
      retTokens.push(inputTokens[inputIndex]);
      inputIndex++;
    }

    return retTokens;
  }

  render() {
    if (!this.childConstructorDone) {
      return;
    }
    const integratedTokens = this.integratedTokens();
    renderTokensTo(this.viewElem, integratedTokens);
  }
}
