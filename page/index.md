---
layout: page
sidebar: false
---

<script setup>
import { onMounted } from 'vue'
import { useRouter, useData } from 'vitepress'

const { lang } = useData()
const router = useRouter()

onMounted(() => {
  // If we are at the exact root base path, redirect to en-us
  if (window.location.pathname === '/Zephyr/' || window.location.pathname === '/Zephyr') {
    router.go('/Zephyr/en-us/')
  }
})
</script>

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
    { title: '⚡ Cache-Friendly VM', description: 'Superinstruction fusion and massive virtual register assignments explicitly engineered for ultra-low latency execution downtimes.' },
    { title: '♻️ Generational GC', description: 'Meticulously crafted 4-Space heap with write card barrier tracking ensures completely hitch-free frame pacing tailored entirely for heavy real-time gameplay loops.' },
    { title: '🎮 First-class Coroutines', description: 'Yield & resume without hesitation via purely heap-preserved internal state machine suspensions entirely independent of the native C++ host stack constraints.' },
    { title: '🛡️ Strict Host Dependencies', description: 'Formidable 4-phase handle lifecycle architecture built firmly from the ground up to protect fragile C++ native memory zones from severe layout leaks.' }
  ]"
/>
