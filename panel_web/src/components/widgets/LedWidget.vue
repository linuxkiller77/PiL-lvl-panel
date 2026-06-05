<script setup>
const props = defineProps({ widget: { type: Object, required: true }, value: { default: undefined } });
const isOn = () => !!props.value;
</script>
<template>
  <div class="lvgl-led-preview">
    <span class="lvgl-led-lamp" :class="{ on: isOn() }" :style="{ '--led': widget.props.color }"></span>
    <small>{{ widget.props.label }}</small>
  </div>
</template>
