<script setup>
defineProps({ widget: { type: Object, required: true }, percent: { type: Number, default: 0 } });
</script>
<template>
  <div class="lvgl-tank-preview" :style="{ '--tank': widget.props.color }">
    <div class="lvgl-tank-vessel"><span :style="{ height: percent + '%' }"></span></div>
    <small>{{ widget.props.label }}</small>
  </div>
</template>
