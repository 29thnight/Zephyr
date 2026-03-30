<template>
  <div class="brand-logo" :class="{ 'is-hero': isHero }">
    <svg width="512" height="512" viewBox="0 0 512 512" fill="none" xmlns="http://www.w3.org/2000/svg" role="img" aria-labelledby="title desc" class="logo-svg">
      <title id="title">Zephyr logo mark</title>
      <desc id="desc">A minimalist wind-inspired vector mark for Zephyr, using smooth layered ribbons in a single green color.</desc>
      <g fill="#00c07a">
        <!-- Bottom flow (rearmost) -->
        <path d="
          M164 404
          C186 372, 226 350, 278 340
          C334 330, 386 308, 430 268
          C424 312, 404 352, 370 386
          C334 420, 290 444, 240 462
          C208 474, 182 492, 166 510
          C158 474, 156 434, 164 404
          Z"/>
        <path d="
          M204 406
          C226 376, 258 356, 300 346
          C340 338, 376 322, 408 294
          C390 326, 358 354, 316 372
          C280 386, 242 396, 204 406
          Z" opacity="0.12"/>
        <!-- Diagonal connector flow (shifted right) -->
        <path d="
          M336 222
          C306 214, 270 226, 234 258
          C198 290, 170 336, 146 392
          C168 402, 206 394, 248 360
          C292 324, 320 278, 336 222
          Z"/>
        <path d="
          M310 228
          C284 224, 256 236, 230 264
          C204 292, 184 326, 168 366
          C186 372, 216 364, 244 338
          C272 312, 294 276, 310 228
          Z" opacity="0.12"/>
        <!-- Middle flow -->
        <path d="
          M96 266
          C120 216, 174 184, 244 176
          C316 168, 384 142, 438 96
          C434 138, 416 178, 384 212
          C348 248, 298 272, 238 288
          C184 302, 136 324, 100 356
          C90 322, 88 290, 96 266
          Z"/>
        <path d="
          M136 268
          C160 238, 202 216, 256 208
          C308 200, 356 184, 398 154
          C374 194, 332 224, 278 238
          C228 250, 180 262, 136 268
          Z" opacity="0.12"/>
        <!-- Top flow (frontmost) -->
        <path d="
          M92 194
          C116 138, 174 100, 254 90
          C334 80, 406 54, 458 8
          C454 56, 436 100, 404 136
          C368 176, 314 200, 248 210
          C190 220, 140 236, 100 262
          C88 236, 84 212, 92 194
          Z"/>
        <path d="
          M130 198
          C154 164, 198 138, 252 128
          C306 120, 356 102, 400 70
          C378 112, 336 144, 284 160
          C232 174, 180 188, 130 198
          Z" opacity="0.12"/>
      </g>
    </svg>
    <div class="brand-text">
      <span class="brand-bold">Zephyr</span>
      <span class="brand-normal">Script</span>
    </div>
  </div>
</template>

<script setup>
defineProps({
  isHero: { type: Boolean, default: false }
});
</script>

<style scoped>
.brand-logo {
  display: flex;
  align-items: center;
  gap: 0.6rem;
  transition: all 0.3s ease;
  height: 100%;
}

.logo-svg {
  width: 24px;
  height: 24px;
  flex-shrink: 0;
}

.brand-text {
  display: flex;
  align-items: baseline;
  white-space: nowrap;
  line-height: 1;
}

.brand-bold {
  font-weight: 700;
  color: #1a1a1a;
}

.brand-normal {
  font-weight: 400;
  color: #1a1a1a;
  margin-left: 0.35rem; /* Explicit space between Zephyr and Script */
}

/* Hero mode styles */
.is-hero {
  gap: 1rem;
}

.is-hero .logo-svg {
  width: clamp(2.2rem, 5.5vw, 3.8rem);
  height: clamp(2.2rem, 5.5vw, 3.8rem);
}

.is-hero .brand-text {
  font-size: clamp(1.4rem, 2.8vw, 2.4rem);
  letter-spacing: -0.02em;
}

/* Nav mode default styles */
.brand-text {
  font-size: 1.2rem;
  font-weight: 600;
  letter-spacing: -0.012em;
}

@media (max-width: 768px) {
  .brand-text {
    font-size: 1rem;
  }
}
</style>
