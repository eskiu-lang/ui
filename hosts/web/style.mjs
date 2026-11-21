// Text, stacking, clipping and motion props for the web renderer (renderer.mjs): 85 line
// height, 86 italic, 87 z index, 88 clip, 89 a modal's presentation, 90 layout animation,
// 91 a modal's appearing, and a switch's or slider's accent (prop 9 on kinds 9 and 15).

const reduced = () => globalThis.matchMedia?.('(prefers-reduced-motion: reduce)')?.matches ?? false;

// Props 85 to 91 on `node`; false for any other code.
export function styleProp(node, code, value) {
  const s = node.style;
  switch (code) {
    case 85: s.lineHeight = value ? `${value}px` : ''; return true;
    case 86: s.fontStyle = value ? 'italic' : ''; return true;
    case 87: s.zIndex = value ? String(value) : ''; return true;
    case 88: s.overflow = value ? 'hidden' : ''; return true;
    case 89: node.uiPresentation = value; return true;
    case 90: node.uiLayoutMs = value; return true;
    case 91: node.uiAppear = value; return true;
  }
  return false;
}

// A switch's or a slider's text color is its accent: the checked track, the filled part and
// the thumb.
export function accent(node, css) {
  node.style.accentColor = css;
  if (node.type === 'checkbox' && node.getAttribute?.('role') === 'switch') {
    node.style.setProperty?.('--ui-accent', css || '');
  }
}

// Layout animation (prop 90): before a commit, the frames of the elements that animate theirs;
// after it, each one the layout moved or resized starts where it was and moves to its new
// frame over its time, with its easing (prop 40), as a transform (FLIP).
export function layoutMotion(nodes, easings) {
  let before = null;
  return {
    snapshot() {
      before = null;
      if (reduced()) return;
      for (const node of nodes.values()) {
        if (!node.uiLayoutMs || !node.isConnected || typeof node.getBoundingClientRect !== 'function') continue;
        (before ??= new Map()).set(node, node.getBoundingClientRect());
      }
    },
    play() {
      if (!before) return;
      for (const [node, was] of before) {
        if (!node.isConnected || typeof node.animate !== 'function') continue;
        const now = node.getBoundingClientRect();
        const dx = was.left - now.left, dy = was.top - now.top;
        const sx = now.width ? was.width / now.width : 1, sy = now.height ? was.height / now.height : 1;
        if (Math.abs(dx) < 0.5 && Math.abs(dy) < 0.5 && Math.abs(sx - 1) < 0.01 && Math.abs(sy - 1) < 0.01) continue;
        const ease = easings[node.uiMotion?.[40] ?? 0] ?? easings[0];
        node.animate([
          {transformOrigin: 'top left', transform: `translate(${dx}px, ${dy}px) scale(${sx}, ${sy})`},
          {transformOrigin: 'top left', transform: 'none'},
        ], {duration: node.uiLayoutMs, easing: ease, composite: 'add'});
      }
      before = null;
    },
  };
}

// A modal as it opens (props 89 and 91): a sheet's panel slides up from the bottom edge, a
// dialog or a whole-screen modal fades in, the backdrop with it; nothing when it appears at
// once (91 = 2) or the system asks for reduced motion.
export function appear(modal) {
  if (modal.uiAppeared) return;
  modal.uiAppeared = true;
  if (modal.uiAppear === 2 || reduced() || typeof modal.animate !== 'function') return;
  const timing = {duration: 220, easing: 'cubic-bezier(0.2, 0.8, 0.2, 1)'};
  modal.animate([{opacity: 0}, {opacity: 1}], timing);
  const panel = modal.firstElementChild;
  if (modal.uiPresentation === 1 && panel?.animate) {
    panel.animate([{transform: 'translateY(100%)'}, {transform: 'none'}], timing);
  } else if (modal.uiPresentation === 2 && panel?.animate) {
    panel.animate([{transform: 'scale(0.96)'}, {transform: 'none'}], timing);
  }
}
