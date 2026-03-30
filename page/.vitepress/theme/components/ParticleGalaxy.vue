<template>
  <canvas ref="canvasRef" class="particle-galaxy"></canvas>
</template>

<script setup>
import { onMounted, onBeforeUnmount, ref } from 'vue';

const canvasRef = ref(null);
let animationFrameId;

onMounted(() => {
  const canvas = canvasRef.value;
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  
  let w = window.innerWidth;
  let h = window.innerHeight;
  canvas.width = w;
  canvas.height = h;

  const particles = [];
  const maxParticles = 600; // Dense starfield

  // Initialize particles with random 3D-like positions
  for(let i=0; i<maxParticles; i++) {
    particles.push({
      x: Math.random() * w,
      y: Math.random() * h,
      z: Math.random() * w,
      o: Math.random() * 0.5 + 0.1,    // Opacity
      size: Math.random() * 1.5 + 0.5  // Size
    });
  }

  const cx = w / 2;
  const cy = h / 2;
  let angle = 0;

  const render = () => {
    // Fill deep dark background
    ctx.clearRect(0, 0, w, h);
    ctx.fillStyle = '#05070a'; 
    ctx.fillRect(0, 0, w, h);

    angle += 0.0015; // Slow rotation speed
    const cosAngle = Math.cos(angle);
    const sinAngle = Math.sin(angle);

    for(let i=0; i<maxParticles; i++) {
      let p = particles[i];

      // Translate point to origin
      let vx = p.x - cx;
      let vy = p.y - cy;

      // 2D Rotation matrix
      let x = vx * cosAngle - vy * sinAngle;
      let y = vx * sinAngle + vy * cosAngle;

      let screenX = x + cx;
      let screenY = y + cy;

      // Calculate distance for radial effects
      let dist = Math.sqrt(x*x + y*y);
      let t = 1 - Math.min(dist / (w/1.5), 1);

      ctx.beginPath();
      // Particles further out are smaller or less visible
      ctx.arc(screenX, screenY, p.size * (t + 0.3), 0, Math.PI * 2);

      // Mixed colors: 1 out of 3 is deeply blue, others are cyan-white (like Antigravity screenshot)
      let isBlue = i % 3 === 0;
      let color = isBlue ? `rgba(100, 160, 255, ${p.o * t * 1.5})` : `rgba(210, 230, 255, ${p.o * t * 1.2})`;

      ctx.fillStyle = color;
      ctx.fill();
      
      // Motion blur trail
      let trailX = screenX + (y * 0.03); 
      let trailY = screenY - (x * 0.03);
      
      ctx.beginPath();
      ctx.moveTo(screenX, screenY);
      ctx.lineTo(trailX, trailY);
      ctx.strokeStyle = color;
      ctx.lineWidth = p.size * 0.7;
      ctx.stroke();

      // Zoom effect
      p.z -= 0.5;
      if (p.z <= 0) p.z = w;
    }
    
    // Radial shadow overlay in the center to create "black hole" depth
    const gr = ctx.createRadialGradient(cx, cy, 0, cx, cy, Math.min(w, h)*0.8);
    gr.addColorStop(0, "rgba(5, 7, 10, 0.95)");
    gr.addColorStop(0.3, "rgba(5, 7, 10, 0.6)");
    gr.addColorStop(1, "rgba(5, 7, 10, 0)");
    
    ctx.fillStyle = gr;
    ctx.fillRect(0, 0, w, h);

    animationFrameId = requestAnimationFrame(render);
  };
  
  const resize = () => {
    w = window.innerWidth;
    h = window.innerHeight;
    canvas.width = w;
    canvas.height = h;
  };
  window.addEventListener('resize', resize);
  
  render();
  
  onBeforeUnmount(() => {
    cancelAnimationFrame(animationFrameId);
    window.removeEventListener('resize', resize);
  });
});
</script>

<style scoped>
.particle-galaxy {
  position: fixed;
  top: 0;
  left: 0;
  width: 100vw;
  height: 100vh;
  z-index: -10;
  pointer-events: none;
  background-color: #05070a;
}
</style>
