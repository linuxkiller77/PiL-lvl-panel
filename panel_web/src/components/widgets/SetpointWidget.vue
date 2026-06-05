<script setup>
defineProps({ widget: { type: Object, required: true }, formatted: { type: String, default: '---' } });
</script>
<template>
  <div class="lvgl-setpoint-preview" :style="{ '--sp': widget.props.color }">
    <div class="lvgl-caption">{{ widget.props.label }}</div>
    <div class="lvgl-spinbox">{{ formatted }}</div>
  </div>
</template>
