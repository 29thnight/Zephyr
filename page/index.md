<script setup>
import { onMounted } from 'vue'
import { withBase } from 'vitepress'

onMounted(() => {
  const lang = navigator.language?.toLowerCase() ?? ''
  if (lang.startsWith('ko')) {
    window.location.replace(withBase('/ko-kr/'))
  } else {
    window.location.replace(withBase('/en-us/'))
  }
})
</script>

# Zephyr Docs

Redirecting...
