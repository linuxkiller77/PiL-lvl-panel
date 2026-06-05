<script setup>
defineProps({ widget: { type: Object, required: true }, formatted: { type: String, default: '---' } });
</script>
<template>
  <div class="lvgl-readout-preview">
    <div class="lvgl-caption">{{ widget.props.label }}</div>
    <div class="lvgl-value" :style="{ color: widget.props.color }">{{ formatted }}</div>
  </div>
</template>
