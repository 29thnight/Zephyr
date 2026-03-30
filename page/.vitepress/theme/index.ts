import { h } from 'vue';
import DefaultTheme from 'vitepress/theme';
import './style.css';

import ParticleGalaxy from './components/ParticleGalaxy.vue';
import GradientBlinds from './components/GradientBlinds.vue';
import CustomHero from './components/CustomHero.vue';
import ScrollFeatures from './components/ScrollFeatures.vue';
import AntigravityBg from './components/AntigravityBg.vue';
import BrandLogo from './components/BrandLogo.vue';

export default {
  ...DefaultTheme,
  Layout: () => {
    return h(DefaultTheme.Layout, null, {
      'nav-bar-title-before': () => h(BrandLogo, { isHero: false })
    });
  },
  enhanceApp({ app }: { app: any }) {
    app.component('ParticleGalaxy', ParticleGalaxy);
    app.component('GradientBlinds', GradientBlinds);
    app.component('CustomHero', CustomHero);
    app.component('ScrollFeatures', ScrollFeatures);
    app.component('AntigravityBg', AntigravityBg);
    app.component('BrandLogo', BrandLogo);
  }
}
