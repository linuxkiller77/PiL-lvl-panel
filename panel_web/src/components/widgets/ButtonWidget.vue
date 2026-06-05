<script setup>
defineProps({ widget: { type: Object, required: true } });
</script>
<template>
  <div class="lvgl-button-preview" :style="{ '--btn': widget.props.color }">
    <div class="lvgl-button-face">{{ widget.props.label }}</div>
  </div>
</template>
