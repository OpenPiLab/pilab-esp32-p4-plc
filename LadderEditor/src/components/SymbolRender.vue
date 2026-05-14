<template>
  <g class="cursor-pointer">
    <template v-if="el.type==='NO'">
      <line :x1="x-30" :y1="y" :x2="x-15" :y2="y" stroke="#e2e8f0" stroke-width="2"/>
      <line :x1="x+15" :y1="y" :x2="x+30" :y2="y" stroke="#e2e8f0" stroke-width="2"/>
      <line :x1="x-15" :y1="y-20" :x2="x-15" :y2="y+20" stroke="#67e8f9" stroke-width="3"/>
      <line :x1="x+15" :y1="y-20" :x2="x+15" :y2="y+20" stroke="#67e8f9" stroke-width="3"/>
    </template>
    <template v-else-if="el.type==='NC'">
      <line :x1="x-30" :y1="y" :x2="x-15" :y2="y" stroke="#e2e8f0" stroke-width="2"/>
      <line :x1="x+15" :y1="y" :x2="x+30" :y2="y" stroke="#e2e8f0" stroke-width="2"/>
      <line :x1="x-15" :y1="y-20" :x2="x-15" :y2="y+20" stroke="#fca5a5" stroke-width="3"/>
      <line :x1="x+15" :y1="y-20" :x2="x+15" :y2="y+20" stroke="#fca5a5" stroke-width="3"/>
      <line :x1="x-22" :y1="y+20" :x2="x+22" :y2="y-20" stroke="#fca5a5" stroke-width="2"/>
    </template>
    <template v-else-if="el.type==='OUT'">
      <line :x1="x-34" :y1="y" :x2="x-25" :y2="y" stroke="#e2e8f0" stroke-width="2"/>
      <line :x1="x+25" :y1="y" :x2="x+34" :y2="y" stroke="#e2e8f0" stroke-width="2"/>
      <path :d="'M '+(x-24)+' '+(y-21)+' Q '+(x-8)+' '+y+' '+(x-24)+' '+(y+21)" fill="none" stroke="#86efac" stroke-width="3"/>
      <path :d="'M '+(x+24)+' '+(y-21)+' Q '+(x+8)+' '+y+' '+(x+24)+' '+(y+21)" fill="none" stroke="#86efac" stroke-width="3"/>
    </template>
    <template v-else>
      <rect :x="x-31" :y="y-24" width="62" height="48" rx="8" fill="#0f172a" :stroke="blockColor(el.type)" stroke-width="2"/>
      <text :x="x" :y="y-4" text-anchor="middle" :fill="blockColor(el.type)" font-size="13" font-weight="800">{{el.type}}</text>
      <text :x="x" :y="y+9" text-anchor="middle" fill="#94a3b8" font-size="9">{{presetLabel(el)}}</text>
      <text v-if="sim" :x="x" :y="y+21" text-anchor="middle" :fill="sim.output ? '#86efac' : '#cbd5e1'" font-size="9" class="mono">{{simLabel(sim)}}</text>
    </template>
    <text :x="x" :y="y+41" text-anchor="middle" fill="#dbeafe" font-size="11" class="mono">{{el.tag}}</text>
  </g>
</template>

<script>
export default {
  name: 'SymbolRender',
  props: ['el', 'x', 'y', 'sim'],
  methods: {
    blockColor(t){ return (t==='TON'||t==='TOF') ? '#fbbf24' : '#c084fc'; },
    presetLabel(el){ return (el.type==='TON'||el.type==='TOF') ? ((el.preset||1000)+'ms') : ('PV '+(el.preset||10)); },
    simLabel(sim){
      if(!sim) return '';
      if(sim.type==='TON') return 'Q='+ (sim.output?'1':'0') +' ET='+Math.round(sim.elapsed_ms||0);
      if(sim.type==='TOF') return 'Q='+ (sim.output?'1':'0') +' ET='+Math.round(sim.elapsed_ms||0)+' R='+Math.round(sim.remaining_ms||0);
      return 'Q='+ (sim.output?'1':'0') +' CV='+Math.round(sim.count||0);
    }
  }
};
</script>
