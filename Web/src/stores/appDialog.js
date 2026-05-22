import { reactive } from 'vue';

let nextDialogId = 1;

export const dialogState = reactive({
  current: null,
  queue: []
});

function pumpDialogQueue() {
  if (dialogState.current || dialogState.queue.length === 0) return;
  dialogState.current = dialogState.queue.shift();
}

function openAppDialog(options) {
  return new Promise((resolve) => {
    const request = {
      id: nextDialogId++,
      type: options.type || 'message',
      tone: options.tone || 'default',
      kicker: options.kicker || 'PiLab',
      title: options.title || 'Message',
      message: options.message || '',
      detail: options.detail || '',
      help: options.help || '',
      label: options.label || 'Value',
      placeholder: options.placeholder || '',
      initialValue: options.initialValue ?? '',
      confirmText: options.confirmText || (options.type === 'confirm' ? 'OK' : 'Close'),
      cancelText: options.cancelText || 'Cancel',
      closeOnBackdrop: options.closeOnBackdrop !== false,
      validator: typeof options.validator === 'function' ? options.validator : null,
      resolve
    };
    dialogState.queue.push(request);
    pumpDialogQueue();
  });
}

function settleCurrentDialog(value) {
  const current = dialogState.current;
  if (!current) return;
  dialogState.current = null;
  current.resolve(value);
  queueMicrotask(pumpDialogQueue);
}

export function confirmDialog(options = {}) {
  return openAppDialog({
    type: 'confirm',
    confirmText: 'OK',
    ...options
  });
}

export function promptDialog(options = {}) {
  return openAppDialog({
    type: 'prompt',
    confirmText: 'OK',
    ...options
  });
}

export function messageDialog(options = {}) {
  return openAppDialog({
    type: 'message',
    confirmText: 'OK',
    ...options
  });
}

export const appDialogActions = {
  confirm(value = true) { settleCurrentDialog(value); },
  cancel() {
    const current = dialogState.current;
    if (!current) return;
    settleCurrentDialog(current.type === 'confirm' ? false : null);
  }
};
