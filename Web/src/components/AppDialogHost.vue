<template>
  <Teleport to="body">
    <div v-if="dialog" class="app-dialog-backdrop" @pointerdown.self="backdropDismiss">
      <form class="app-dialog" :class="`tone-${dialog.tone}`" @submit.prevent="submitDialog" @keydown.esc.prevent="cancelDialog">
        <div class="app-dialog-header">
          <div>
            <div class="app-dialog-kicker">{{ dialog.kicker }}</div>
            <h2>{{ dialog.title }}</h2>
          </div>
          <button type="button" class="app-dialog-close" aria-label="Close dialog" @click="cancelDialog">×</button>
        </div>

        <div v-if="dialog.message" class="app-dialog-message">{{ dialog.message }}</div>
        <div v-if="dialog.detail" class="app-dialog-detail">{{ dialog.detail }}</div>

        <label v-if="dialog.type === 'prompt'" class="app-dialog-field">
          <span>{{ dialog.label }}</span>
          <input ref="inputRef"
                 v-model="inputValue"
                 type="text"
                 autocomplete="off"
                 spellcheck="false"
                 :placeholder="dialog.placeholder">
        </label>

        <div v-if="validationError" class="app-dialog-error">{{ validationError }}</div>
        <div v-else-if="dialog.help" class="app-dialog-help">{{ dialog.help }}</div>

        <div class="app-dialog-actions">
          <button v-if="dialog.type !== 'message'" type="button" class="app-dialog-button secondary" @click="cancelDialog">
            {{ dialog.cancelText }}
          </button>
          <button type="submit" :class="['app-dialog-button', dialog.tone === 'danger' ? 'danger' : 'primary']">
            {{ dialog.confirmText }}
          </button>
        </div>
      </form>
    </div>
  </Teleport>
</template>

<script setup>
import { computed, nextTick, ref, watch } from 'vue';
import { appDialogActions, dialogState } from '../stores/appDialog';

const dialog = computed(() => dialogState.current);
const inputValue = ref('');
const validationError = ref('');
const inputRef = ref(null);

watch(dialog, async (next) => {
  validationError.value = '';
  inputValue.value = next?.initialValue ?? '';
  if (next?.type === 'prompt') {
    await nextTick();
    inputRef.value?.focus();
    inputRef.value?.select();
  }
});

function submitDialog() {
  const d = dialog.value;
  if (!d) return;
  if (d.type === 'prompt') {
    const value = String(inputValue.value ?? '').trim();
    if (d.validator) {
      const result = d.validator(value);
      if (result) {
        validationError.value = String(result);
        nextTick(() => inputRef.value?.focus());
        return;
      }
    }
    appDialogActions.confirm(value);
    return;
  }
  appDialogActions.confirm(true);
}

function cancelDialog() {
  appDialogActions.cancel();
}

function backdropDismiss() {
  if (dialog.value?.closeOnBackdrop === false) return;
  cancelDialog();
}
</script>
