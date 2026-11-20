// Accessibility and text input options for the web renderer (renderer.mjs): Style's role and
// states (props 57 to 64) as ARIA, disabled (prop 61) down a subtree, a text input's editing
// options (props 65 to 71), and an open modal taking the keyboard.

const ROLES = {1: 'button', 2: 'link', 3: 'heading', 4: 'img', 5: 'checkbox', 6: 'radio', 7: 'radiogroup', 8: 'switch',
  9: 'tab', 10: 'tablist', 11: 'slider', 12: 'progressbar', 13: 'menu', 14: 'menuitem', 15: 'list', 16: 'listitem',
  17: 'alert', 18: 'search'};
export const INPUT_MODES = {1: 'email', 2: 'numeric', 3: 'decimal', 4: 'tel', 5: 'url', 6: 'search'};
export const ENTER_HINTS = {1: 'done', 2: 'go', 3: 'next', 4: 'search', 5: 'send'};
export const CAPITALIZE = {1: 'none', 2: 'words', 3: 'sentences', 4: 'characters'};
export const AUTOFILL = {1: 'username', 2: 'current-password', 3: 'new-password', 4: 'email', 5: 'one-time-code', 6: 'name', 7: 'tel'};
export const TEXT_ALIGN = {1: 'start', 2: 'center', 3: 'end', 4: 'justify'};
const CONTROLS = 'button[data-ui-id], input[data-ui-id], textarea[data-ui-id], div[data-kind="8"]';

// An attribute, removed when value is null.
export function aria(node, name, value) {
  if (value == null) node.removeAttribute(name); else node.setAttribute(name, value);
}

// Style.role (prop 57): the role's name, or the one the element's kind gave it (uiRole).
export function setRole(node, value) {
  const role = ROLES[value] ?? node.uiRole;
  if (role) node.setAttribute('role', role); else node.removeAttribute('role');
  aria(node, 'aria-level', value === 3 ? '2' : null);
}

// True when node or an element above it (up to root) is disabled (uiDisabled).
export function disabledAbove(node, root) {
  for (let at = node; at && at !== root; at = at.parentElement) if (at.uiDisabled) return true;
  return false;
}

// Style.disabled holds for what is inside too: node and the controls under it are disabled
// while anything above them is, and enabled again when nothing is.
export function applyDisabled(node, root) {
  const set = control => {
    const off = disabledAbove(control, root);
    if (control.dataset.kind === '8') control.tabIndex = off ? -1 : 0;
    else if ('disabled' in control) control.disabled = off;
    aria(control, 'aria-disabled', off ? 'true' : null);
  };
  set(node);
  for (const control of node.querySelectorAll?.(CONTROLS) ?? []) set(control);
}

// Input.multiline (prop 68): a one-line field becomes a textarea, and back, the same node to
// the app (`nodes` maps its id), with its attributes, its value and what the renderer keeps on
// it. Returns the element now in its place.
export function setMultiline(node, multiline, nodes) {
  const want = multiline ? 'TEXTAREA' : 'INPUT';
  if (node.tagName === want) return node;
  const next = document.createElement(want.toLowerCase());
  for (const {name, value} of [...node.attributes]) if (name !== 'type') next.setAttribute(name, value);
  for (const key of Object.keys(node)) if (key.startsWith('ui')) next[key] = node[key];
  next.value = node.value;
  if (multiline) { next.style.resize = 'none'; next.style.fieldSizing = 'content'; }
  else next.type = node.uiSecure ? 'password' : 'text';
  node.replaceWith(next);
  nodes.set(Number(node.dataset.uiId), next);
  return next;
}

// An open modal takes the keyboard: what is outside the newest one is inert, focus moves into
// it when it opens and goes back where it was when the last one closes. update(modals) after
// each commit.
export function focusTrap() {
  let inerted = [];
  let before = null;
  return {
    get active() { return inerted.length > 0; },
    update(modals) {
      const top = modals.filter(m => m.isConnected).at(-1);
      for (const element of inerted) element.inert = false;
      inerted = [];
      if (!top) {
        if (before?.isConnected) before.focus?.();
        before = null;
        return;
      }
      for (let at = top; at && at !== document.body; at = at.parentElement) {
        for (const sibling of at.parentElement?.children ?? []) {
          if (sibling !== at && !sibling.inert && sibling.tagName !== 'SCRIPT' && sibling.tagName !== 'STYLE') {
            sibling.inert = true;
            inerted.push(sibling);
          }
        }
      }
      if (!top.contains(document.activeElement)) {
        before ??= document.activeElement;
        const first = top.querySelector('button:not([disabled]), input:not([disabled]), textarea:not([disabled]), [tabindex="0"]');
        if (first) first.focus(); else { top.tabIndex = -1; top.focus(); }
      }
    },
  };
}

// The padding (prop 2) and a side's own (props 73 to 76, top, right, bottom, left; 0: the
// padding's), kept on the node so each change applies all four.
export function applyPadding(node, code, value) {
  const pad = node.uiPadding ??= {2: 0, 73: 0, 74: 0, 75: 0, 76: 0};
  pad[code] = value;
  const side = c => `${pad[c] === 0 ? pad[2] : pad[c]}px`;
  const sides = [73, 74, 75, 76].some(c => pad[c] !== 0);
  node.style.padding = sides ? `${side(73)} ${side(74)} ${side(75)} ${side(76)}` : `${pad[2]}px`;
}

// Style.max_lines (prop 82): a text cut after that many lines with an ellipsis; 0 no limit.
export function maxLines(node, lines) {
  const s = node.style;
  if (lines > 0) {
    s.display = '-webkit-box'; s.webkitBoxOrient = 'vertical'; s.webkitLineClamp = String(lines); s.overflow = 'hidden';
  } else if (s.webkitLineClamp) {
    s.display = ''; s.webkitBoxOrient = ''; s.webkitLineClamp = ''; s.overflow = '';
  }
}

// An image: hidden while it cannot be loaded (no broken icon, no alt text spilling out of its
// box), shown again when a new source loads; `report(id, generation, loaded)` when the app
// listens (prop 84, node.uiListens).
export function watchImage(node, report) {
  const tell = loaded => {
    node.style.visibility = loaded ? '' : 'hidden';
    if (node.uiListens) report(Number(node.dataset.uiId), Number(node.dataset.generation), loaded);
  };
  node.addEventListener?.('load', () => tell(true));
  node.addEventListener?.('error', () => tell(false));
}
