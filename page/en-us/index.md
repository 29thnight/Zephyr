---
layout: page
sidebar: false
---

<AntigravityBg
  :count="300"
  :magnetRadius="14"
  :ringRadius="12"
  :waveSpeed="0.4"
  :waveAmplitude="1"
  :particleSize="0.4"
  :lerpSpeed="0.05"
  :autoAnimate="true"
  :particleVariance="1"
  :rotationSpeed="0"
  :depthFactor="1"
  :pulseSpeed="3"
  particleShape="capsule"
  :fieldStrength="10"
/>

<CustomHero
  locale="en"
  tagline="Ultra-fast Script Runtime for Game Engines"
  primaryText="Read the Docs"
  primaryLink="/en-us/introduction/overview"
  secondaryText="View on GitHub"
  secondaryLink="https://github.com/29thnight/Zephyr"
/>

<ScrollFeatures
  :features="[
    { title: '⚡ Cache-Friendly VM', description: 'Superinstruction fusion and virtual register assignments explicitly engineered for optimized execution.' },
    { title: '♻️ Generational GC', description: 'Meticulously crafted 4-Space heap with write card barrier tracking ensures consistent frame pacing tailored for heavy real-time gameplay loops.' },
    { title: '🎮 First-class Coroutines', description: 'Yield & resume without hesitation via purely heap-preserved internal state machine suspensions entirely independent of the native C++ host stack constraints.' },
    { title: '🛡️ Strict Host Dependencies', description: 'Formidable 4-phase handle lifecycle architecture built firmly from the ground up to protect fragile C++ native memory zones from severe layout leaks.' }
  ]"
/>
