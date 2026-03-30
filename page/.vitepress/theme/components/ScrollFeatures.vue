<template>
  <div class="scroll-features-wrapper">
    <div 
      v-for="(feature, index) in features" 
      :key="index"
      class="scroll-feature-item"
      ref="featureRefs"
    >
      <div class="border-glow-card">
        <div class="card-content">
          <h2>{{ feature.title }}</h2>
          <p>{{ feature.description }}</p>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { onMounted, ref, defineProps } from 'vue';

const props = defineProps({
  features: { type: Array, required: true }
});

const featureRefs = ref([]);

onMounted(() => {
  const observer = new IntersectionObserver((entries) => {
    entries.forEach(entry => {
      if (entry.isIntersecting) {
        entry.target.classList.add('is-visible');
      }
    });
  }, { threshold: 0.2 });

  if (featureRefs.value) {
    featureRefs.value.forEach(el => observer.observe(el));
  }
});
</script>

<style scoped>
.scroll-features-wrapper {
  display: flex;
  flex-direction: column;
  gap: 12vh;
  padding: 15vh 5% 25vh;
  max-width: 1100px;
  margin: 0 auto;
}

.scroll-feature-item {
  opacity: 0;
  transform: translateY(80px);
  transition: opacity 0.8s ease, transform 1s cubic-bezier(0.175, 0.885, 0.32, 1);
  display: flex;
  justify-content: center;
}

.scroll-feature-item.is-visible {
  opacity: 1;
  transform: translateY(0);
}

/* Border Glow */
.border-glow-card {
  position: relative;
  width: 100%;
  max-width: 880px;
  border-radius: 22px;
  padding: 3px;
  overflow: hidden;
  z-index: 1;
  box-shadow: 0 20px 60px -12px rgba(0, 0, 0, 0.12);
}

.border-glow-card::before {
  content: '';
  position: absolute;
  top: 50%;
  left: 50%;
  width: 140%;
  height: 250%;
  background: conic-gradient(from 0deg, transparent 0%, transparent 40%, #00E59B 75%, #5227FF 90%, #FF9FFC 100%);
  animation: border-glow-spin 5s linear infinite;
  transform-origin: center center;
  transform: translate(-50%, -50%);
  z-index: -1;
}

@keyframes border-glow-spin {
  0%   { transform: translate(-50%, -50%) rotate(0deg); }
  100% { transform: translate(-50%, -50%) rotate(360deg); }
}

/* Card interior — light */
.card-content {
  background: #ffffff;
  border-radius: 19px;
  padding: 4.5rem 4rem;
  min-height: 340px;
  display: flex;
  flex-direction: column;
  justify-content: center;
  position: relative;
  z-index: 2;
}

.card-content h2 {
  font-size: clamp(2.2rem, 4.5vw, 3.6rem) !important;
  font-weight: 700 !important;
  color: #1a1a1a !important;
  margin: 0 0 1.2rem 0 !important;
  letter-spacing: -0.03em;
  border: none !important;
}

.card-content p {
  font-size: clamp(1.1rem, 2.2vw, 1.6rem) !important;
  line-height: 1.65 !important;
  color: #555 !important;
  margin: 0 !important;
}

@media (max-width: 768px) {
  .card-content {
    padding: 3rem 2rem;
    min-height: auto;
  }
}
</style>
