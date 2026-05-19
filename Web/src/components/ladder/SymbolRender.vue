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
    <template v-else-if="['OUT','SET','RST'].includes(el.type)">
      <!--
        Coil shape intentionally matches the toolbar icon:
          left side  = "("
          right side = ")"

        Important: SVG quadratic curves do not pass through their control point.
        For this curve the visible midpoint is halfway between the endpoint x
        and the control x, so the rung stubs terminate at +/-17, not +/-24.
        That makes the wire touch the parenthesis without crossing through it.
      -->
      <line :x1="x-30" :y1="y" :x2="x-17" :y2="y" stroke="#e2e8f0" stroke-width="2"/>
      <line :x1="x+17" :y1="y" :x2="x+30" :y2="y" stroke="#e2e8f0" stroke-width="2"/>
      <path :d="'M '+(x-10)+' '+(y-21)+' Q '+(x-24)+' '+y+' '+(x-10)+' '+(y+21)" fill="none" :stroke="coilColor(el.type)" stroke-width="3"/>
      <path :d="'M '+(x+10)+' '+(y-21)+' Q '+(x+24)+' '+y+' '+(x+10)+' '+(y+21)" fill="none" :stroke="coilColor(el.type)" stroke-width="3"/>
      <text v-if="el.type!=='OUT'" :x="x" :y="y+4" text-anchor="middle" :fill="coilColor(el.type)" font-size="13" font-weight="900">{{el.type==='SET' ? 'S' : 'R'}}</text>
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
    blockColor(t){ return (t==='TON'||t==='TOF') ? '#fbbf24' : (t==='ONS' ? '#38bdf8' : '#c084fc'); },
    coilColor(t){ return t==='SET' ? '#60a5fa' : (t==='RST' ? '#f87171' : '#86efac'); },
    presetLabel(el){ if(el.type==='TON'||el.type==='TOF') return ((el.preset||1000)+'ms'); if(el.type==='CTU'||el.type==='CTD') return 'PV '+(el.preset||10); if(el.type==='ONS') return 'one scan'; return ''; },
    simLabel(sim){
      if(!sim) return '';
      if(sim.type==='TON') return 'Q='+ (sim.output?'1':'0') +' ET='+Math.round(sim.elapsed_ms||0);
      if(sim.type==='TOF') return 'Q='+ (sim.output?'1':'0') +' ET='+Math.round(sim.elapsed_ms||0)+' R='+Math.round(sim.remaining_ms||0);
      if(sim.type==='ONS') return 'Q='+ (sim.output?'1':'0') +' L='+ (sim.last?'1':'0');
      return 'Q='+ (sim.output?'1':'0') +' CV='+Math.round(sim.count||0);
    }
  }
};
</script>
