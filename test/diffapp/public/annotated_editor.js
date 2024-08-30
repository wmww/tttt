class AnnotatedEditor extends Editor {
  constructor(inputElem, viewElem) {
    super(inputElem, viewElem);
    this.indent = null;
    this.newlineRegex = /\r?\n/g;
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

    annotationA = annotationA ?? '';
    annotationB = annotationB ?? '';
    referenceValue = referenceValue ?? '';

    let linesValue = referenceValue.split(this.newlineRegex);
    if (this.indent) {
      for (let i = 0; i < linesValue.length; i++) {
        linesValue[i] = this.indent + linesValue[i]
      }
    }
    const linesA = annotationA.split(this.newlineRegex);
    const linesB = annotationB.split(this.newlineRegex);
    const lineDiffA = Diff.diffArrays(linesValue, linesA);
    const lineDiffB = Diff.diffArrays(linesValue, linesB);

    // Returns added sets of lines joined and a single undefined for each non-added line
    const processLineDiff = lineDiff => {
      let ret = [];
      for (const elem of lineDiff) {
        if (elem.added) {
          ret.push(elem.value.join('\n'));
        } else {
          for (let i = 0; i < elem.value.length; i++) {
            ret.push(undefined);
          }
        }
      }
      return ret;
    }

    const processedA = processLineDiff(lineDiffA);
    const processedB = processLineDiff(lineDiffB);

    let indexA = 0;
    let indexB = 0;
    this.annotationWithNulls = [];
    while (indexA < processedA.length || indexB < processedB.length) {
      if (processedA[indexA] === undefined && processedB[indexB] === undefined) {
        this.annotationWithNulls.push(null);
        indexA++;
        indexB++;
      } else {
        let resultA = '';
        if (processedA[indexA] !== undefined) {
          resultA = processedA[indexA];
          indexA++;
        }

        let resultB = '';
        if (processedB[indexB] !== undefined) {
          resultB = processedB[indexB];
          indexB++;
        }

        const sectionLinesA = resultA.split(this.newlineRegex);
        const sectionLinesB = resultB.split(this.newlineRegex);
        for (const i of Diff.diffArrays(sectionLinesA, sectionLinesB)) {
          if (i.added) {
            tokenizeStringInto(i.value.join('\n'), null, 'token-diff-b', false, null, this.annotationWithNulls);
          } else if (i.removed) {
            tokenizeStringInto(i.value.join('\n'), null, 'token-diff-a', false, null, this.annotationWithNulls);
          } else {
            tokenizeStringInto(i.value.join('\n'), null, 'token-diff-both', false, null, this.annotationWithNulls);
          }
          if (this.annotationWithNulls &&
            this.annotationWithNulls[this.annotationWithNulls.length - 1] &&
            this.annotationWithNulls[this.annotationWithNulls.length - 1].type !== Token.NEWLINE
          ) {
            this.annotationWithNulls.push({
              type: Token.NEWLINE,
              extraClass: 'token-diff-both',
              offset: null,
            });
          }
        }
      }
    }

    this.annotationWithNulls.push({
      type: Token.ENDLINE,
      extraClass: 'token-diff-both',
      offset: null,
    });

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
