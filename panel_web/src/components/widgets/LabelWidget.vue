<script setup>
const props = defineProps({ widget: { type: Object, required: true }, value: { default: undefined } });
</script>
<template>
  <div class="lvgl-label-preview" :style="{ color: widget.props.color }">
    {{ widget.props.pin ? (value ?? widget.props.label) : widget.props.label }}
  </div>
</template>
