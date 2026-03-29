<script setup>
import { onMounted } from 'vue'

onMounted(() => {
  // Default: en-us. Only redirect to ko-kr for Korean browsers.
  const lang = navigator.language?.toLowerCase() ?? ''
  if (lang.startsWith('ko')) {
    window.location.replace('/ko-kr/')
  } else {
    window.location.replace('/en-us/')
  }
})
</script>

# Zephyr Docs

Redirecting...
