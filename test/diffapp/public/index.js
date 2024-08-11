const inputWaitMs = 50; // How long to wait after an input for more events to batch with it

const mainText = document.getElementById('main-text');
const leftText = document.getElementById('left-text');
const rightText = document.getElementById('right-text');
const errorList = document.getElementById('error-list');
const errorTemplate = document.getElementById('error-template');
const token = Array(6).fill().map(() => String.fromCharCode(Math.floor(Math.random() * 26) + 97)).join('');

let currentChannel;
const latestResults = {
  tttt: {},
  tsc: {},
}

function errorsUpdated(errors) {
  if (errors) {
    console.error('Errors from server: ' + errors.join('\n'));
  }
  while (errorList.lastElementChild) {
    errorList.removeChild(errorList.lastElementChild);
  }
  for (let message of errors) {
    const template = errorTemplate.content.cloneNode(true);
    template.querySelector('#error-message').textContent = message;
    errorList.append(template);
  }
}

function codeChangedByServer(code) {
  mainText.value = code;
}

const channelButtons = Array.from(document.getElementsByClassName('channel-button'));
function onChannelSelected(channel) {
  if (channel == currentChannel) {
    return;
  }
  currentChannel = channel;
  channelButtons.forEach(button => {
    button.classList.toggle('active', button.id.endsWith(channel));
  });
  renderDiff();
}
channelButtons.forEach(button => {
  button.onclick = () => {
    onChannelSelected(button.id.split('-').pop());
  }
});

let mainTextTimeout;
mainText.oninput = () => {
  clearTimeout(mainTextTimeout);
  mainTextTimeout = setTimeout(() => {
    fetch('/setCode?token=' + token, {
      method: 'POST',
      body: mainText.value,
    });
  }, inputWaitMs);
}

function renderDiff() {
  leftText.textContent = latestResults.tsc[currentChannel];
  rightText.textContent = latestResults.tsc[currentChannel];
}

function handleEvents() {
  fetch('/events?token=' + token)
    .then(resp => resp.json())
    .then(json => {
      // console.log('Got message: ', json);
      let render = false;
      for (const key in json) {
        const keys = key.split('.');
        switch (keys[0]) {
          case 'errors':
            errorsUpdated(json[key]);
            break;

          case 'code':
            codeChangedByServer(json[key]);
            break;

          case 'result':
            latestResults[keys[1]][keys[2]] = json[key];
            if (currentChannel == keys[2]) {
              render = true;
            }
            break;
        }
      }
      if (render) {
        renderDiff();
      }
      handleEvents();
    })
    .catch(err => {
      errorsUpdated([err.toString(), 'Server connection closed, please refresh']);
    })
}

onChannelSelected('typ');
handleEvents();
