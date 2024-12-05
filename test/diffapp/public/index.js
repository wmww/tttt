const codeInput = document.getElementById('code-input');
const codeView = document.getElementById('code-view');
const stderrView = document.getElementById('stderr-view');
const errorList = document.getElementById('error-list');
const errorTemplate = document.getElementById('error-template');
const token = Array(6).fill().map(() => String.fromCharCode(Math.floor(Math.random() * 26) + 97)).join('');

const backButton = document.getElementById('back-button');
const forwardButton = document.getElementById('forward-button');
const editor = new AnnotatedEditor(codeInput, codeView);
editor.onValueChanged = onCodeValueChanged;

backButton.onclick = () => {
  editor.changeBuffer(-1);
  focusCode();
};

forwardButton.onclick = () => {
  editor.changeBuffer(1);
  focusCode();
};

backButton.disabled = true;
editor.onBuffersChanged = (prev, next) => {
  backButton.disabled = prev === 0;
};

let clientErrors = [];
let serverErrors = [];
let connectedToServer = true;
function updateErrors() {
  while (errorList.lastElementChild) {
    errorList.removeChild(errorList.lastElementChild);
  }
  const fullErrors = clientErrors.concat(serverErrors);
  if (!connectedToServer) {
    fullErrors.push('Disconnected from server');
  }
  for (let message of fullErrors) {
    const template = errorTemplate.content.cloneNode(true);
    template.querySelector('#error-message').textContent = message;
    errorList.append(template);
  }
}
function surfaceError(message) {
  console.error(message);
  clientErrors.push(message);
  updateErrors();
  setTimeout(() => {
    clientErrors.splice(clientErrors.indexOf(message), 1);
    updateErrors();
  }, 10000);
}

function focusCode() {
  codeInput.focus({preventScroll: true});
}

let currentChannel;
const channelButtons = Array.from(document.getElementsByClassName('channel-button'));
function onChannelSelected(channel) {
  if (channel == currentChannel) {
    return;
  }
  currentChannel = channel;
  for (const button of channelButtons) {
    button.classList.toggle('active', button.id.endsWith(channel));
  }
  focusCode();
  updateEditor();
}
channelButtons.forEach(button => {
  button.onclick = () => {
    onChannelSelected(button.id.split('-').pop());
  }
});

const errorCountRegex = /=== .* \((?<count>\d+) errors\) ===/g;
function updateErrorCounts(errsA, errsB) {
  function getErrorCount(result) {
    if (!result) {
      return 0;
    }
    const matches = result.matchAll(errorCountRegex);
    if (matches) {
      return matches
        .map(x => x.groups.count)
        // DO NOT CHANGE THIS TO .map(parseInt)!!!!!
        // https://twitter.com/xsphi/status/1864568484497375335
        .map(x => parseInt(x))
        .reduce((a, b) => a + b, 0);
    } else {
      return null;
    }
  }
  const errorCountA = getErrorCount(errsA) ?? '?';
  const errorCountB = getErrorCount(errsB) ?? '?';
  const errorsButton = document.getElementById('channel-button-err');
  errorsButton.textContent = 'Errors';
  if (errorCountA !== errorCountB) {
    errorsButton.textContent += ' [' + errorCountA + '/' + errorCountB + ']';
  } else if (errorCountA) {
    errorsButton.textContent += ' [' + errorCountA + ']';
  }
}

function setStderrText(stderr) {
  const tokens = [];
  tokenizeStderrInto(stderr, tokens);
  if (stderr) {
    stderrView.parentElement.style.display = '';
    renderTokensTo(stderrView, tokens);
  } else {
    stderrView.parentElement.style.display = 'none';
  }
}

onChannelSelected('typ');
setStderrText('');
fetchEvents();
