<script setup>
defineProps({ widget: { type: Object, required: true }, formatted: { type: String, default: '---' }, percent: { type: Number, default: 0 } });
</script>
<template>
  <div class="lvgl-gauge-preview" :style="{ '--gauge': widget.props.color }">
    <div class="lvgl-caption">{{ widget.props.label }}</div>
    <div class="lvgl-bar-track"><span :style="{ width: percent + '%' }"></span></div>
    <b>{{ formatted }}</b>
  </div>
</template>
