<template>
  <div class="antigravity-wrapper" ref="containerRef" :class="{ 'bg-ready': bgReady }"></div>
</template>

<script setup>
import { ref, onMounted, onBeforeUnmount } from 'vue';
import * as THREE from 'three';

const bgReady = ref(false);

const props = defineProps({
  count: { type: Number, default: 300 },
  magnetRadius: { type: Number, default: 6 },
  ringRadius: { type: Number, default: 7 },
  waveSpeed: { type: Number, default: 0.4 },
  waveAmplitude: { type: Number, default: 1 },
  particleSize: { type: Number, default: 0.3 },
  lerpSpeed: { type: Number, default: 0.05 },
  color: { type: String, default: '#5227FF' },
  autoAnimate: { type: Boolean, default: true },
  particleVariance: { type: Number, default: 1 },
  rotationSpeed: { type: Number, default: 0 },
  depthFactor: { type: Number, default: 1 },
  pulseSpeed: { type: Number, default: 3 },
  particleShape: { type: String, default: 'capsule' },
  fieldStrength: { type: Number, default: 10 }
});

const containerRef = ref(null);
let renderer, scene, camera, clock, instancedMesh, animationId;
let particles = [];
let virtualMouse = { x: 0, y: 0 };
let lastMousePos = { x: 0, y: 0 };
let lastMouseMoveTime = 0;
let mouseNDC = { x: 0, y: 0 };
const _color = new THREE.Color();

// Brand-matched emerald (same as .zs-brand-bold color: #00c07a)
const EMERALD_HSL = { h: 0.438, s: 1.0, l: 0.376 };


onMounted(() => {
  // Fade in particles after typing animation completes (~2.4s)
  setTimeout(() => { bgReady.value = true; }, 2400);

  const container = containerRef.value;
  if (!container || typeof window === 'undefined') return;

  renderer = new THREE.WebGLRenderer({ alpha: true, antialias: true });
  renderer.setPixelRatio(window.devicePixelRatio);
  renderer.setClearColor(0x000000, 0);
  // Fix: apply sRGB output so colors match their intended hex values
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  container.appendChild(renderer.domElement);

  scene = new THREE.Scene();
  camera = new THREE.PerspectiveCamera(35, 1, 0.1, 1000);
  camera.position.set(0, 0, 50);
  clock = new THREE.Clock();

  let geometry;
  if (props.particleShape === 'sphere') geometry = new THREE.SphereGeometry(0.2, 16, 16);
  else if (props.particleShape === 'box') geometry = new THREE.BoxGeometry(0.3, 0.3, 0.3);
  else if (props.particleShape === 'tetrahedron') geometry = new THREE.TetrahedronGeometry(0.3);
  else geometry = new THREE.CapsuleGeometry(0.1, 0.4, 4, 8);

  const emeraldColor = new THREE.Color('#00c07a');
  const material = new THREE.MeshBasicMaterial({ color: emeraldColor });
  instancedMesh = new THREE.InstancedMesh(geometry, material, props.count);
  scene.add(instancedMesh);

  const dummy = new THREE.Object3D();

  // Per-particle hue phase offset (0..1)
  const phaseOffsets = Array.from({ length: props.count }, () => Math.random());

  const resize = () => {
    const w = container.clientWidth;
    const h = container.clientHeight;
    renderer.setSize(w, h);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();

    if (particles.length === 0) {
      const dist = camera.position.z;
      const vFOV = THREE.MathUtils.degToRad(camera.fov);
      const vHeight = 2 * Math.tan(vFOV / 2) * dist;
      const vWidth = vHeight * camera.aspect;

      for (let i = 0; i < props.count; i++) {
        const x = (Math.random() - 0.5) * vWidth;
        const y = (Math.random() - 0.5) * vHeight;
        const z = (Math.random() - 0.5) * 20;
        particles.push({
          t: Math.random() * 100,
          speed: 0.01 + Math.random() / 200,
          mx: x, my: y, mz: z,
          cx: x, cy: y, cz: z,
          randomRadiusOffset: (Math.random() - 0.5) * 2
        });
      }
    }
  };

  const ro = new ResizeObserver(resize);
  ro.observe(container);
  resize();

  const onPointerMove = (e) => {
    const rect = container.getBoundingClientRect();
    mouseNDC.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
    mouseNDC.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;
    const dx = mouseNDC.x - lastMousePos.x;
    const dy = mouseNDC.y - lastMousePos.y;
    if (Math.sqrt(dx * dx + dy * dy) > 0.001) {
      lastMouseMoveTime = Date.now();
      lastMousePos = { x: mouseNDC.x, y: mouseNDC.y };
    }
  };
  window.addEventListener('pointermove', onPointerMove);

  const animate = () => {
    animationId = requestAnimationFrame(animate);
    const elapsed = clock.getElapsedTime();

    const dist = camera.position.z;
    const vFOV = THREE.MathUtils.degToRad(camera.fov);
    const vHeight = 2 * Math.tan(vFOV / 2) * dist;
    const vWidth = vHeight * camera.aspect;

    let destX, destY;
    if (props.autoAnimate && Date.now() - lastMouseMoveTime > 2000) {
      destX = Math.sin(elapsed * 0.5) * (vWidth / 4);
      destY = Math.cos(elapsed * 0.5 * 2) * (vHeight / 4);
    } else {
      destX = mouseNDC.x * (vWidth / 2);
      destY = mouseNDC.y * (vHeight / 2);
    }

    virtualMouse.x += (destX - virtualMouse.x) * 0.05;
    virtualMouse.y += (destY - virtualMouse.y) * 0.05;

    const targetX = virtualMouse.x;
    const targetY = virtualMouse.y;
    const globalRotation = elapsed * props.rotationSpeed;

    for (let i = 0; i < particles.length; i++) {
      const p = particles[i];
      p.t += p.speed / 2;

      const projectionFactor = 1 - p.cz / 50;
      const projX = targetX * projectionFactor;
      const projY = targetY * projectionFactor;

      const dx = p.mx - projX;
      const dy = p.my - projY;
      const dist2 = Math.sqrt(dx * dx + dy * dy);

      let tpx = p.mx, tpy = p.my, tpz = p.mz * props.depthFactor;

      if (dist2 < props.magnetRadius) {
        const angle = Math.atan2(dy, dx) + globalRotation;
        const wave = Math.sin(p.t * props.waveSpeed + angle) * (0.5 * props.waveAmplitude);
        const deviation = p.randomRadiusOffset * (5 / (props.fieldStrength + 0.1));
        const currentRing = props.ringRadius + wave + deviation;
        tpx = projX + currentRing * Math.cos(angle);
        tpy = projY + currentRing * Math.sin(angle);
        tpz = p.mz * props.depthFactor + Math.sin(p.t) * (1 * props.waveAmplitude * props.depthFactor);
      }

      p.cx += (tpx - p.cx) * props.lerpSpeed;
      p.cy += (tpy - p.cy) * props.lerpSpeed;
      p.cz += (tpz - p.cz) * props.lerpSpeed;

      dummy.position.set(p.cx, p.cy, p.cz);
      dummy.lookAt(projX, projY, p.cz);
      dummy.rotateX(Math.PI / 2);

      const currentDist = Math.sqrt(
        Math.pow(p.cx - projX, 2) + Math.pow(p.cy - projY, 2)
      );
      const distFromRing = Math.abs(currentDist - props.ringRadius);
      let scaleFactor = Math.max(0, Math.min(1, 1 - distFromRing / 10));
      const finalScale = scaleFactor
        * (0.8 + Math.sin(p.t * props.pulseSpeed) * 0.2 * props.particleVariance)
        * props.particleSize;
      dummy.scale.set(finalScale, finalScale, finalScale);
      dummy.updateMatrix();
      instancedMesh.setMatrixAt(i, dummy.matrix);
    }

    instancedMesh.instanceMatrix.needsUpdate = true;
    renderer.render(scene, camera);
  };

  animate();

  onBeforeUnmount(() => {
    cancelAnimationFrame(animationId);
    window.removeEventListener('pointermove', onPointerMove);
    ro.disconnect();
    geometry.dispose();
    material.dispose();
    renderer.dispose();
    if (renderer.domElement.parentElement) {
      renderer.domElement.parentElement.removeChild(renderer.domElement);
    }
  });
});
</script>

<style scoped>
.antigravity-wrapper {
  position: absolute;
  top: 0;
  left: 0;
  width: 100vw;
  height: 100vh;
  z-index: -10;
  pointer-events: auto;
  background: transparent;
  opacity: 0;
  transition: opacity 1.5s ease;
}
.antigravity-wrapper.bg-ready {
  opacity: 1;
}
</style>
