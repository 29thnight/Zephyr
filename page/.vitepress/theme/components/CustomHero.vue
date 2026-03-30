<template>
  <div class="zs-hero-container" :style="heroStyle">
    <div class="zs-hero-inner">

      <!-- Phase 2+: Brand label morphs in -->
      <div class="zs-brand" :class="phase >= 2 ? 'zs-morph-in' : 'zs-hidden'">
        <BrandLogo :isHero="true" />
      </div>

      <!-- Phase 1: Typewriter headline -->
      <p class="zs-tagline">
        <span>{{ displayedText }}</span>
        <span class="zs-cursor" :class="cursorClass">|</span>
      </p>

      <!-- Phase 2+: Buttons morph in -->
      <div class="zs-actions" :class="phase >= 2 ? 'zs-morph-in-delayed' : 'zs-hidden'">
        <a :href="resolvedPrimaryLink" class="zs-btn zs-btn-primary">{{ primaryText }}</a>
        <a :href="resolvedSecondaryLink" class="zs-btn zs-btn-ghost">{{ secondaryText }}</a>
      </div>

    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onBeforeUnmount } from 'vue';
import { withBase } from 'vitepress';
import BrandLogo from './BrandLogo.vue';

const props = defineProps({
  tagline:      { type: String, default: '' },
  locale:       { type: String, default: 'en' }, // 'ko' | 'en'
  primaryText:  { type: String, default: '시작하기' },
  primaryLink:  { type: String, default: '/' },
  secondaryText:{ type: String, default: 'GitHub' },
  secondaryLink:{ type: String, default: '/' }
});

// Apply base path to internal links (external links pass through unchanged)
const resolvedPrimaryLink = computed(() =>
  props.primaryLink.startsWith('http') ? props.primaryLink : withBase(props.primaryLink)
);
const resolvedSecondaryLink = computed(() =>
  props.secondaryLink.startsWith('http') ? props.secondaryLink : withBase(props.secondaryLink)
);

// ── Korean IME step generator ──────────────────────────────────
const CHOSEONG = ['ㄱ','ㄲ','ㄴ','ㄷ','ㄸ','ㄹ','ㅁ','ㅂ','ㅃ','ㅅ','ㅆ','ㅇ','ㅈ','ㅉ','ㅊ','ㅋ','ㅌ','ㅍ','ㅎ'];
const CHAR_DELAY = 62; // ms per step

function buildSteps(text, locale) {
  const steps = [];
  let built = '';
  for (const ch of text) {
    const code = ch.charCodeAt(0);
    if (locale === 'ko' && code >= 0xAC00 && code <= 0xD7A3) {
      const off  = code - 0xAC00;
      const jong = off % 28;
      const jung = Math.floor(off / 28) % 21;
      const cho  = Math.floor(off / 28 / 21);
      // step: initial consonant alone
      steps.push(built + CHOSEONG[cho]);
      // step: consonant + vowel (no final)
      if (jong > 0) steps.push(built + String.fromCharCode(0xAC00 + (cho * 21 + jung) * 28));
      // step: complete syllable
      built += ch;
      steps.push(built);
    } else {
      built += ch;
      steps.push(built);
    }
  }
  return steps;
}

// ── State ─────────────────────────────────────────────────────
const phase        = ref(0);  // 0=idle 1=typing 2=morphing 3=done
const displayedText = ref('');
let timerId = null;

onMounted(() => {
  const steps = buildSteps(props.tagline, props.locale);
  let idx = 0;

  // Small initial hold before typing begins
  timerId = setTimeout(() => {
    phase.value = 1;
    timerId = setInterval(() => {
      if (idx < steps.length) {
        displayedText.value = steps[idx++];
      } else {
        clearInterval(timerId);
        // 350ms hold after last char, then morph-in
        timerId = setTimeout(() => {
          phase.value = 2;
          timerId = setTimeout(() => { phase.value = 3; }, 900);
        }, 350);
      }
    }, CHAR_DELAY);
  }, 250);
});

onBeforeUnmount(() => { clearInterval(timerId); clearTimeout(timerId); });

// ── Scroll parallax (reused from before) ──────────────────────
const scrollY = ref(0);
const _onScroll = () => { scrollY.value = window.scrollY ?? 0; };
onMounted(() => window.addEventListener('scroll', _onScroll, { passive: true }));
onBeforeUnmount(() => window.removeEventListener('scroll', _onScroll));

const heroStyle = computed(() => {
  const y = scrollY.value;
  return {
    opacity: Math.max(0, 1 - y / 480),
    transform: `translateY(${y * 0.35}px)`,
    filter: `blur(${Math.min(y / 35, 14)}px)`,
    pointerEvents: y > 160 ? 'none' : 'auto'
  };
});

const cursorClass = computed(() => ({
  'zs-cursor-blink': phase.value === 1,
  'zs-cursor-gone':  phase.value >= 2
}));
</script>

<style>
.zs-hero-container {
  position: relative;
  z-index: 10;
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 100vh;
  padding-top: 0;
  padding-bottom: 18vh;
  padding-left: 24px;
  padding-right: 24px;
  text-align: center;
  will-change: transform, opacity;
}

.zs-hero-inner {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 0.6rem;
  max-width: 900px;
}

/* ── Brand ── */
.zs-brand {
  font-size: clamp(1.3rem, 2.5vw, 1.8rem);
  line-height: 1.2;
  letter-spacing: -0.01em;
  margin-bottom: 2rem;
  white-space: nowrap;
}
.zs-brand-bold   { font-weight: 600; color: #00c07a; }
.zs-brand-normal { font-weight: 400; color: #1a1a1a; }

/* ── Tagline (typed) ── */
.zs-tagline {
  font-size: clamp(2rem, 5vw, 4rem) !important;
  font-weight: 700;
  color: #111111 !important;
  letter-spacing: -0.04em;
  margin: 0 0 3.5rem !important;
  line-height: 1.12;
  word-break: keep-all;
}

/* ── Cursor ── */
.zs-cursor {
  color: #00c07a;
  font-weight: 300;
  display: inline-block;
}
@keyframes zs-blink {
  0%, 100% { opacity: 1; }
  50%       { opacity: 0; }
}
.zs-cursor-blink { animation: zs-blink 0.75s step-end infinite; }
.zs-cursor-gone  { opacity: 0; transition: opacity 0.25s ease; }

/* ── Morph-in animations ── */
.zs-hidden { opacity: 0; transform: translateY(10px); pointer-events: none; }

.zs-morph-in {
  opacity: 1;
  transform: translateY(0);
  transition: opacity 0.7s cubic-bezier(0.22, 1, 0.36, 1),
              transform 0.7s cubic-bezier(0.22, 1, 0.36, 1);
}
.zs-morph-in-delayed {
  opacity: 1;
  transform: translateY(0);
  transition: opacity 0.7s cubic-bezier(0.22, 1, 0.36, 1) 0.15s,
              transform 0.7s cubic-bezier(0.22, 1, 0.36, 1) 0.15s;
}

/* ── Buttons ── */
.zs-actions {
  display: flex;
  gap: 0.85rem;
  justify-content: center;
  flex-wrap: wrap;
}
.zs-btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  height: 48px;
  padding: 0 1.75rem;
  border-radius: 24px;
  font-size: 0.95rem;
  font-weight: 500;
  letter-spacing: -0.005em;
  text-decoration: none !important;
  transition: transform 0.22s ease, box-shadow 0.22s ease, background 0.22s ease;
}
.zs-btn-primary { background: #1a1a1a; color: #ffffff !important; box-shadow: 0 2px 8px rgba(0,0,0,0.18); }
.zs-btn-primary:hover { background: #2d2d2d; transform: translateY(-2px) scale(1.03); }
.zs-btn-ghost   { background: #ececec; color: #1a1a1a !important; border: none; }
.zs-btn-ghost:hover { background: #dcdcdc; transform: translateY(-2px) scale(1.03); }
</style>
