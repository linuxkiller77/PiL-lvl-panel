<script setup>
const props = defineProps({ widget: { type: Object, required: true }, value: { default: undefined } });
const isOn = () => !!props.value;
</script>
<template>
  <div class="lvgl-toggle-preview" :style="{ '--toggle': widget.props.color }">
    <div class="lvgl-caption">{{ widget.props.label }}</div>
    <div class="lvgl-switch-track" :class="{ on: isOn() }">
      <span></span>
    </div>
  </div>
</template>
