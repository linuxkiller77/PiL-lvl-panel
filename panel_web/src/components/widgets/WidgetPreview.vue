<script setup>
import LabelWidget from './LabelWidget.vue';
import ReadoutWidget from './ReadoutWidget.vue';
import LedWidget from './LedWidget.vue';
import ButtonWidget from './ButtonWidget.vue';
import ToggleWidget from './ToggleWidget.vue';
import GaugeWidget from './GaugeWidget.vue';
import TankWidget from './TankWidget.vue';
import SetpointWidget from './SetpointWidget.vue';

const componentMap = {
  label: LabelWidget,
  readout: ReadoutWidget,
  numeric: ReadoutWidget,
  led: LedWidget,
  indicator: LedWidget,
  button: ButtonWidget,
  toggle: ToggleWidget,
  gauge: GaugeWidget,
  bar: GaugeWidget,
  tank: TankWidget,
  thermometer: TankWidget,
  setpoint: SetpointWidget,
};

const props = defineProps({
  widget: { type: Object, required: true },
  value: { default: undefined },
  formatted: { type: String, default: '---' },
  percent: { type: Number, default: 0 },
});
</script>
<template>
  <component
    :is="componentMap[widget.type] || LabelWidget"
    :widget="widget"
    :value="value"
    :formatted="formatted"
    :percent="percent"
  />
</template>
