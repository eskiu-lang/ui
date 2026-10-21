// The "slider" view on the web: an <input type=range>.
export function setup(ui) {
  ui.registerView('slider', {
    create(emit) {
      const input = document.createElement('input');
      input.type = 'range';
      input.min = '0';
      input.max = '100';
      input.addEventListener('input', () => emit('change', input.value));
      return {
        element: input,
        set(name, value) { if (name === 'value' && input.value !== value) input.value = value; },
      };
    },
  });
}
