<script setup>
import { computed, reactive, ref, onMounted, onBeforeUnmount } from 'vue'
import {
  Activity, AlertTriangle, Braces, CheckCircle2, CircleDot, Copy, Download,
  FileJson, GitBranch, GripHorizontal, Layers3, Move, PlayCircle, Plus,
  Route, Save, ShieldAlert, Square, Trash2, Upload, Workflow, Zap
} from 'lucide-vue-next'

const uid = (prefix) => `${prefix}_${Math.random().toString(36).slice(2, 7)}`.toUpperCase()
const selected = reactive({ kind: 'step', sequencerId: 'RUN', id: 'CLAMP_PART' })
const activeTab = ref('inspector')
const toast = ref('')
const canvasRef = ref(null)

const project = reactive({
  kind: 'PiLabSfcType',
  schemaVersion: 2,
  type: {
    name: 'FacePartCycleType',
    family: 'PiLabMachineSequence',
    version: '0.0002',
    reusable: true,
    comment: 'SFC type JSON IR with Siemens-inspired runtime semantics and future AngelScript/C++ runtime target.'
  },
  defaultInstance: {
    name: 'Station1_FacePart',
    scanMs: 5,
    mode: 'AUTO',
    stepControlMode: 'Transition',
    autoStart: false,
    cyclicOperation: false,
    timeMonitoring: true,
    retainStateOnRestart: true,
    commandOutput: true
  },
  standardInterface: {
    commands: ['START', 'HOLD', 'RESUME', 'STOP', 'ABORT', 'RESET'],
    states: ['IDLE', 'RUN', 'HELD', 'STOPPED', 'ABORTING', 'ERROR', 'COMPLETE'],
    diagnostics: ['ActiveSequencer', 'ActiveSteps', 'LastTransition', 'StepElapsedMs', 'FaultCode', 'FaultMessage', 'ExecErr', 'CpuRestart']
  },
  characteristics: {
    controlStrategies: [
      { id: 1, name: 'FACE', enabled: true, standard: true },
      { id: 2, name: 'DRILL', enabled: true, standard: false }
    ],
    setpoints: [
      { name: 'FaceDepthSP', ioName: 'FACE_DEPTH_SP', type: 'REAL', initial: 2.5, unit: 'mm', low: 0, high: 20, strategies: [1] },
      { name: 'FeedRateSP', ioName: 'FEED_RATE_SP', type: 'REAL', initial: 8.0, unit: 'mm/s', low: 0, high: 60, strategies: [1, 2] }
    ],
    processValues: [
      { name: 'FaceDepthPV', ioName: 'FACE_DEPTH_PV', type: 'REAL', unit: 'mm' },
      { name: 'SpindleSpeedPV', ioName: 'SPINDLE_RPM_PV', type: 'REAL', unit: 'rpm' }
    ],
    parameters: [
      { name: 'ClampTimeoutMs', ioName: 'CLAMP_TIMEOUT_MS', type: 'UINT', initial: 3000 },
      { name: 'FaceTimeoutMs', ioName: 'FACE_TIMEOUT_MS', type: 'UINT', initial: 8000 }
    ],
    timers: [
      { name: 'ClampStableTimer', mode: 'TON', presetMs: 250 },
      { name: 'SpindleStableTimer', mode: 'TON', presetMs: 500 }
    ],
    noteTexts: [
      { number: 1, name: 'Load part and press Start' },
      { number: 2, name: 'Check clamp pressure before resume' }
    ],
    positionTexts: [
      { number: 10, name: 'Clamping part' },
      { number: 20, name: 'Starting spindle' },
      { number: 30, name: 'Facing part' },
      { number: 40, name: 'Retracting tool' }
    ]
  },
  commands: {
    start: 'StartPB && EstopOk',
    hold: '!ClampPressureOk && STEP_FACE_PART.active',
    resume: 'ClampPressureOk && EstopOk',
    stop: 'StopPB',
    abort: '!EstopOk',
    reset: 'ResetPB && EstopOk'
  },
  sequencers: [
    {
      id: 'RUN',
      name: 'RUN',
      priority: 100,
      startCondition: 'RUN == true && READY_TC == false && EstopOk',
      description: 'Primary production sequence.',
      preprocess: ['Q_Fault = false;'],
      postprocess: ['ActiveSequencer = "RUN";'],
      steps: [
        { id: 'START', name: 'START', type: 'start', x: 390, y: 60, init: [], process: ['OPTIPNO = 1;'], terminate: [], minTimeMs: 0, maxTimeMs: 0, entryGuard: 'true', activeMonitors: [] },
        { id: 'CLAMP_PART', name: 'CLAMP_PART', type: 'normal', x: 390, y: 230, init: ['Q_Clamp = true;'], process: ['Q_Clamp = true;', 'POSINO = 10;'], terminate: ['POSINO = 0;'], minTimeMs: 250, maxTimeMs: 3000, entryGuard: 'PartPresent && EstopOk', activeMonitors: [{ condition: 'PartPresent', faultCode: 'PART_LOST_CLAMP', message: 'Part disappeared during clamp', targetSequencer: 'ERROR' }] },
        { id: 'SPINDLE_START', name: 'SPINDLE_START', type: 'normal', x: 390, y: 400, init: ['Q_Spindle = true;'], process: ['Q_Clamp = true;', 'Q_Spindle = true;', 'POSINO = 20;'], terminate: [], minTimeMs: 500, maxTimeMs: 5000, entryGuard: 'PartPresent && Clamp1Closed && Clamp2Closed && ClampPressureOk', activeMonitors: [{ condition: 'PartPresent && Clamp1Closed && Clamp2Closed && ClampPressureOk', faultCode: 'CLAMP_NOT_SECURE', message: 'Clamp or part lost before machining', targetSequencer: 'ERROR' }] },
        { id: 'FACE_PART', name: 'FACE_PART', type: 'normal', x: 390, y: 570, init: ['Q_Feed = true;'], process: ['Q_Clamp = true;', 'Q_Spindle = true;', 'Q_Feed = true;', 'POSINO = 30;'], terminate: ['Q_Feed = false;'], minTimeMs: 100, maxTimeMs: 8000, entryGuard: 'PartPresent && Clamp1Closed && Clamp2Closed && ClampPressureOk && SpindleAtSpeed', activeMonitors: [
          { condition: 'PartPresent', faultCode: 'PART_LOST_DURING_FACE', message: 'Part lost during facing', targetSequencer: 'ERROR' },
          { condition: 'Clamp1Closed && Clamp2Closed && ClampPressureOk', faultCode: 'CLAMP_LOST_DURING_FACE', message: 'Clamp lost during facing', targetSequencer: 'ERROR' },
          { condition: 'SpindleAtSpeed', faultCode: 'SPINDLE_SPEED_LOST', message: 'Spindle speed lost during facing', targetSequencer: 'ERROR' }
        ] },
        { id: 'RETRACT', name: 'RETRACT', type: 'normal', x: 390, y: 740, init: [], process: ['Q_Retract = true;', 'Q_Clamp = true;', 'Q_Spindle = true;', 'POSINO = 40;'], terminate: ['Q_Retract = false;', 'Q_Spindle = false;'], minTimeMs: 0, maxTimeMs: 4000, entryGuard: 'true', activeMonitors: [{ condition: 'Clamp1Closed && Clamp2Closed', faultCode: 'CLAMP_LOST_RETRACT', message: 'Clamp opened before retract completed', targetSequencer: 'ERROR' }] },
        { id: 'END', name: 'END', type: 'final', x: 390, y: 910, init: ['CycleComplete = true;', 'READY_TC = true;'], process: [], terminate: [], minTimeMs: 0, maxTimeMs: 0, entryGuard: 'true', activeMonitors: [] }
      ],
      transitions: [
        { id: 'T_START_TO_CLAMP', name: 'T_START_TO_CLAMP', from: ['START'], to: ['CLAMP_PART'], condition: 'PartPresent && EstopOk', priority: 10, action: [], osComment: 'Start when a part is present.' },
        { id: 'T_CLAMP_TO_SPINDLE', name: 'T_CLAMP_TO_SPINDLE', from: ['CLAMP_PART'], to: ['SPINDLE_START'], condition: 'Clamp1Closed && Clamp2Closed && ClampPressureOk', priority: 10, action: [], osComment: 'Clamp verified.' },
        { id: 'T_SPINDLE_TO_FACE', name: 'T_SPINDLE_TO_FACE', from: ['SPINDLE_START'], to: ['FACE_PART'], condition: 'SpindleAtSpeed', priority: 10, action: [], osComment: 'Spindle ready.' },
        { id: 'T_FACE_TO_RETRACT', name: 'T_FACE_TO_RETRACT', from: ['FACE_PART'], to: ['RETRACT'], condition: 'FaceDepthPV >= FaceDepthSP', priority: 10, action: [], osComment: 'Facing depth reached.' },
        { id: 'T_RETRACT_TO_END', name: 'T_RETRACT_TO_END', from: ['RETRACT'], to: ['END'], condition: 'Retracted', priority: 10, action: ['Q_Clamp = false;'], osComment: 'Tool retracted.' }
      ]
    },
    {
      id: 'ERROR',
      name: 'ERROR',
      priority: 255,
      startCondition: 'SFC_FaultActive || !EstopOk',
      description: 'High-priority fault handler sequence.',
      preprocess: ['Q_Fault = true;'],
      postprocess: ['ActiveSequencer = "ERROR";'],
      steps: [
        { id: 'ERR_START', name: 'ERR_START', type: 'start', x: 390, y: 70, init: [], process: [], terminate: [], minTimeMs: 0, maxTimeMs: 0, entryGuard: 'true', activeMonitors: [] },
        { id: 'SAFE_OUTPUTS', name: 'SAFE_OUTPUTS', type: 'normal', x: 390, y: 250, init: ['Q_Feed = false;', 'Q_Spindle = false;', 'Q_Retract = false;', 'Q_Fault = true;'], process: ['Q_Fault = true;', 'OPTIPNO = 2;'], terminate: [], minTimeMs: 0, maxTimeMs: 0, entryGuard: 'true', activeMonitors: [] },
        { id: 'WAIT_RESET', name: 'WAIT_RESET', type: 'normal', x: 390, y: 430, init: [], process: ['Q_Fault = true;'], terminate: [], minTimeMs: 0, maxTimeMs: 0, entryGuard: 'true', activeMonitors: [] }
      ],
      transitions: [
        { id: 'T_ERR_TO_SAFE', name: 'T_ERR_TO_SAFE', from: ['ERR_START'], to: ['SAFE_OUTPUTS'], condition: 'true', priority: 10, action: [], osComment: 'Force safe outputs.' },
        { id: 'T_SAFE_TO_WAIT', name: 'T_SAFE_TO_WAIT', from: ['SAFE_OUTPUTS'], to: ['WAIT_RESET'], condition: 'true', priority: 10, action: [], osComment: 'Wait for reset.' },
        { id: 'T_RESET', name: 'T_RESET', from: ['WAIT_RESET'], to: ['ERR_START'], condition: 'ResetPB && EstopOk', priority: 10, action: ['SFC_FaultActive = false;', 'FaultCode = "";', 'FaultMessage = "";'], osComment: 'Clear fault state.' }
      ]
    }
  ],
  futureTranspiler: {
    target: 'C++ SFC runtime with AngelScript callbacks',
    notes: [
      'C++ owns step activation, timers, command semantics, restart behavior, and diagnostics.',
      'AngelScript supplies transition conditions and step init/process/termination callbacks.',
      'JSON object IDs must map cleanly back to generated code for live debugging.'
    ]
  }
})

const currentSequencer = computed(() => project.sequencers.find(s => s.id === selected.sequencerId) || project.sequencers[0])
const selectedStep = computed(() => currentSequencer.value.steps.find(s => s.id === selected.id))
const selectedTransition = computed(() => currentSequencer.value.transitions.find(t => t.id === selected.id))
const jsonText = computed(() => JSON.stringify(project, null, 2))

const canvasSize = computed(() => {
  const steps = currentSequencer.value.steps
  const maxX = Math.max(1100, ...steps.map(s => s.x + 360))
  const maxY = Math.max(760, ...steps.map(s => s.y + 220))
  return { width: maxX, height: maxY }
})

const validation = computed(() => {
  const issues = []
  const seqIds = new Set(project.sequencers.map(s => s.id))
  for (const seq of project.sequencers) {
    const stepIds = new Set(seq.steps.map(s => s.id))
    const starts = seq.steps.filter(s => s.type === 'start')
    const finals = seq.steps.filter(s => s.type === 'final')
    if (!seq.startCondition?.trim()) issues.push({ level: 'warn', text: `${seq.id}: empty start condition means this sequencer will never start.` })
    if (starts.length !== 1) issues.push({ level: 'error', text: `${seq.id}: should have exactly one start step.` })
    if (finals.length < 1 && seq.id !== 'ERROR') issues.push({ level: 'warn', text: `${seq.id}: no final step.` })
    for (const step of seq.steps) {
      if (step.type !== 'final' && !seq.transitions.some(t => t.from.includes(step.id))) issues.push({ level: 'warn', text: `${seq.id}.${step.id}: no outgoing transition.` })
      if (step.maxTimeMs > 0 && step.minTimeMs > step.maxTimeMs) issues.push({ level: 'error', text: `${seq.id}.${step.id}: min runtime exceeds max runtime.` })
      for (const mon of step.activeMonitors || []) if (mon.targetSequencer && !seqIds.has(mon.targetSequencer)) issues.push({ level: 'error', text: `${seq.id}.${step.id}: monitor target sequencer ${mon.targetSequencer} does not exist.` })
    }
    for (const t of seq.transitions) {
      for (const f of t.from) if (!stepIds.has(f)) issues.push({ level: 'error', text: `${seq.id}.${t.id}: missing source step ${f}.` })
      for (const to of t.to) if (!stepIds.has(to)) issues.push({ level: 'error', text: `${seq.id}.${t.id}: missing target step ${to}.` })
      if (!t.condition?.trim()) issues.push({ level: 'warn', text: `${seq.id}.${t.id}: transition condition is empty.` })
    }
  }
  return issues
})

const angelPreview = computed(() => {
  const seq = currentSequencer.value
  const lines = []
  lines.push('// Future AngelScript callback skeleton generated from PiLab SFC JSON')
  lines.push(`// SFC Type: ${project.type.name}`)
  lines.push(`// Sequencer: ${seq.id}`)
  lines.push('')
  for (const step of seq.steps) {
    const safe = cleanId(step.id)
    lines.push(`bool ${safe}_entryGuard() { return ${step.entryGuard || 'true'}; }`)
    lines.push(`void ${safe}_init() {`)
    ;(step.init || []).forEach(a => lines.push(`    ${a}`))
    lines.push('}')
    lines.push(`void ${safe}_process() {`)
    ;(step.process || []).forEach(a => lines.push(`    ${a}`))
    lines.push('}')
    lines.push(`void ${safe}_terminate() {`)
    ;(step.terminate || []).forEach(a => lines.push(`    ${a}`))
    lines.push('}')
    ;(step.activeMonitors || []).forEach((m, i) => lines.push(`bool ${safe}_monitor_${i + 1}() { return ${m.condition || 'true'}; }`))
    lines.push('')
  }
  for (const t of seq.transitions) lines.push(`bool ${cleanId(t.id)}() { return ${t.condition || 'true'}; }`)
  return lines.join('\n')
})

function cleanId(id) { return String(id || 'UNNAMED').replace(/[^A-Za-z0-9_]/g, '_').replace(/^([0-9])/, '_$1') }
function showToast(message) { toast.value = message; setTimeout(() => { if (toast.value === message) toast.value = '' }, 1800) }
function chooseStep(seqId, id) { selected.kind = 'step'; selected.sequencerId = seqId; selected.id = id; activeTab.value = 'inspector' }
function chooseTransition(seqId, id) { selected.kind = 'transition'; selected.sequencerId = seqId; selected.id = id; activeTab.value = 'inspector' }
function stepById(id) { return currentSequencer.value.steps.find(s => s.id === id) }
function centerOf(step, edge = 'center') {
  const w = 280, h = 108
  if (!step) return { x: 0, y: 0 }
  if (edge === 'bottom') return { x: step.x + w / 2, y: step.y + h }
  if (edge === 'top') return { x: step.x + w / 2, y: step.y }
  return { x: step.x + w / 2, y: step.y + h / 2 }
}
function transitionPoint(t) {
  const from = centerOf(stepById(t.from[0]), 'bottom')
  const to = centerOf(stepById(t.to[0]), 'top')
  const y = (from.y + to.y) / 2
  const x = Math.min(from.x, to.x) - 265
  return { x: Math.max(26, x), y }
}
function pathForTransition(t) {
  const fromStep = stepById(t.from[0])
  const toStep = stepById(t.to[0])
  if (!fromStep || !toStep) return ''
  const from = centerOf(fromStep, 'bottom')
  const to = centerOf(toStep, 'top')
  const midY = (from.y + to.y) / 2
  return `M ${from.x} ${from.y} L ${from.x} ${midY - 18} M ${from.x - 38} ${midY} L ${from.x + 38} ${midY} M ${to.x} ${midY + 18} L ${to.x} ${to.y}`
}
function branchPath(t) {
  const fromStep = stepById(t.from[0])
  const toStep = stepById(t.to[0])
  if (!fromStep || !toStep) return ''
  const from = centerOf(fromStep, 'bottom')
  const to = centerOf(toStep, 'top')
  const sideX = Math.max(from.x, to.x) + 220
  return `M ${from.x} ${from.y} C ${sideX} ${from.y}, ${sideX} ${to.y}, ${to.x} ${to.y}`
}
function isBackTransition(t) {
  const from = stepById(t.from[0])
  const to = stepById(t.to[0])
  return from && to && to.y <= from.y
}
function stepTypeClass(step) {
  if (selected.kind === 'step' && selected.id === step.id) return 'border-cyan-300 bg-cyan-500/20 shadow-cyan-950/60'
  if (step.type === 'start') return 'border-emerald-300/60 bg-emerald-500/10'
  if (step.type === 'final') return 'border-amber-300/60 bg-amber-500/10'
  return 'border-slate-600 bg-slate-950/90'
}
function addSequencer() {
  const id = uid('SEQ')
  project.sequencers.push({ id, name: id, priority: 50, startCondition: 'false', description: 'New sequencer', preprocess: [], postprocess: [], steps: [{ id: 'START', name: 'START', type: 'start', x: 390, y: 80, init: [], process: [], terminate: [], minTimeMs: 0, maxTimeMs: 0, entryGuard: 'true', activeMonitors: [] }], transitions: [] })
  chooseStep(id, 'START')
}
function addStep() {
  const seq = currentSequencer.value
  const id = uid('STEP')
  const maxY = Math.max(60, ...seq.steps.map(s => s.y))
  seq.steps.push({ id, name: id, type: 'normal', x: 390, y: maxY + 170, init: [], process: ['// action;'], terminate: [], minTimeMs: 0, maxTimeMs: 0, entryGuard: 'true', activeMonitors: [] })
  chooseStep(seq.id, id)
}
function addTransition() {
  const seq = currentSequencer.value
  const id = uid('T')
  const from = seq.steps.at(-2)?.id || seq.steps[0]?.id || ''
  const to = seq.steps.at(-1)?.id || ''
  seq.transitions.push({ id, name: id, from: from ? [from] : [], to: to ? [to] : [], condition: 'true', priority: 10, action: [], osComment: 'New transition' })
  chooseTransition(seq.id, id)
}
function autoLayout() {
  currentSequencer.value.steps.forEach((s, i) => { s.x = 390; s.y = 60 + i * 170 })
  showToast('Sequencer auto-laid out')
}
function removeSelected() {
  const seq = currentSequencer.value
  if (selected.kind === 'step') {
    const i = seq.steps.findIndex(s => s.id === selected.id)
    if (i >= 0 && seq.steps[i].type !== 'start') {
      const id = seq.steps[i].id
      seq.steps.splice(i, 1)
      seq.transitions = seq.transitions.filter(t => !t.from.includes(id) && !t.to.includes(id))
    }
  } else {
    const i = seq.transitions.findIndex(t => t.id === selected.id)
    if (i >= 0) seq.transitions.splice(i, 1)
  }
  selected.kind = 'step'; selected.id = seq.steps[0]?.id || ''
}
function downloadJson() {
  const blob = new Blob([jsonText.value], { type: 'application/json' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url; a.download = `${project.type.name}.sfc.json`; a.click()
  URL.revokeObjectURL(url)
  showToast('JSON exported')
}
function saveLocal() { localStorage.setItem('pilab-sfc-type-editor', jsonText.value); showToast('Saved to browser localStorage') }
function loadLocal() {
  const raw = localStorage.getItem('pilab-sfc-type-editor')
  if (!raw) return showToast('No local save found')
  Object.assign(project, JSON.parse(raw)); showToast('Loaded local JSON')
}
async function copyJson() { await navigator.clipboard.writeText(jsonText.value); showToast('JSON copied') }
async function copyAs() { await navigator.clipboard.writeText(angelPreview.value); showToast('AngelScript skeleton copied') }
function importJson(e) {
  const file = e.target.files?.[0]
  if (!file) return
  const reader = new FileReader()
  reader.onload = () => { Object.assign(project, JSON.parse(reader.result)); showToast('Imported JSON') }
  reader.readAsText(file)
}
function listText(arr) { return (arr || []).join('\n') }
function setList(obj, key, value) { obj[key] = value.split('\n').map(s => s.trim()).filter(Boolean) }
function addMonitor(step) { step.activeMonitors.push({ condition: 'true', faultCode: 'FAULT_CODE', message: 'Fault message', targetSequencer: 'ERROR' }) }
function removeMonitor(step, i) { step.activeMonitors.splice(i, 1) }

const drag = reactive({ active: false, id: '', dx: 0, dy: 0 })
function startDrag(e, step) {
  if (e.target.closest('button, input, textarea, select')) return
  chooseStep(currentSequencer.value.id, step.id)
  const host = canvasRef.value
  if (!host) return
  const rect = host.getBoundingClientRect()
  drag.active = true
  drag.id = step.id
  drag.dx = e.clientX - rect.left + host.scrollLeft - step.x
  drag.dy = e.clientY - rect.top + host.scrollTop - step.y
  e.currentTarget.setPointerCapture?.(e.pointerId)
}
function onPointerMove(e) {
  if (!drag.active) return
  const host = canvasRef.value
  const step = stepById(drag.id)
  if (!host || !step) return
  const rect = host.getBoundingClientRect()
  step.x = Math.max(20, Math.round((e.clientX - rect.left + host.scrollLeft - drag.dx) / 10) * 10)
  step.y = Math.max(20, Math.round((e.clientY - rect.top + host.scrollTop - drag.dy) / 10) * 10)
}
function stopDrag() { drag.active = false; drag.id = '' }
onMounted(() => { window.addEventListener('pointermove', onPointerMove); window.addEventListener('pointerup', stopDrag) })
onBeforeUnmount(() => { window.removeEventListener('pointermove', onPointerMove); window.removeEventListener('pointerup', stopDrag) })
</script>

<template>
  <div class="h-screen p-3 grid grid-rows-[76px_1fr] gap-3 overflow-hidden">
    <header class="glass rounded-2xl px-4 py-3 flex items-center justify-between">
      <div class="flex items-center gap-3 min-w-0">
        <div class="w-11 h-11 rounded-xl border border-cyan-300/40 bg-cyan-400/10 text-cyan-300 flex items-center justify-center font-black">P</div>
        <div class="min-w-0">
          <h1 class="text-2xl font-bold leading-tight truncate">PiLab SFC Type Editor</h1>
          <p class="text-sm text-slate-400 truncate">Readable SFC JSON IR · draggable steps · explicit transition bars · future AngelScript target</p>
        </div>
      </div>
      <div class="flex flex-wrap justify-end gap-2">
        <button @click="addSequencer" class="btn px-3 py-2 rounded-xl border border-cyan-300/40 bg-cyan-500/15 text-cyan-100 font-semibold flex items-center gap-2"><Layers3 class="w-4 h-4"/>Sequencer</button>
        <button @click="addStep" class="btn px-3 py-2 rounded-xl border border-emerald-300/35 bg-emerald-500/15 text-emerald-100 font-semibold flex items-center gap-2"><Plus class="w-4 h-4"/>Step</button>
        <button @click="addTransition" class="btn px-3 py-2 rounded-xl border border-blue-300/35 bg-blue-500/15 text-blue-100 font-semibold flex items-center gap-2"><GitBranch class="w-4 h-4"/>Transition</button>
        <button @click="autoLayout" class="btn px-3 py-2 rounded-xl border border-purple-300/35 bg-purple-500/15 text-purple-100 font-semibold flex items-center gap-2"><Route class="w-4 h-4"/>Auto layout</button>
        <button @click="saveLocal" class="btn px-3 py-2 rounded-xl border border-slate-500/60 bg-slate-700/50 font-semibold flex items-center gap-2"><Save class="w-4 h-4"/>Save</button>
        <button @click="downloadJson" class="btn px-3 py-2 rounded-xl border border-amber-300/35 bg-amber-500/15 text-amber-100 font-semibold flex items-center gap-2"><Download class="w-4 h-4"/>JSON</button>
      </div>
    </header>

    <main class="grid grid-cols-[330px_minmax(720px,1fr)_500px] gap-3 min-h-0">
      <aside class="glass rounded-2xl p-4 overflow-auto scrollbar">
        <h2 class="section-title">SFC Type</h2>
        <div class="panel rounded-xl p-3 space-y-3 mb-4">
          <label class="label">Type name</label>
          <input v-model="project.type.name" class="input mono" />
          <div class="grid grid-cols-2 gap-2">
            <div><label class="label">Family</label><input v-model="project.type.family" class="input" /></div>
            <div><label class="label">Version</label><input v-model="project.type.version" class="input" /></div>
          </div>
        </div>

        <h2 class="section-title">Instance defaults</h2>
        <div class="panel rounded-xl p-3 grid grid-cols-2 gap-2 mb-4 text-sm">
          <label class="check"><input type="checkbox" v-model="project.defaultInstance.retainStateOnRestart"/> Retain restart</label>
          <label class="check"><input type="checkbox" v-model="project.defaultInstance.timeMonitoring"/> Time monitor</label>
          <label class="check"><input type="checkbox" v-model="project.defaultInstance.autoStart"/> Autostart</label>
          <label class="check"><input type="checkbox" v-model="project.defaultInstance.cyclicOperation"/> Cyclic</label>
          <select v-model="project.defaultInstance.mode" class="input col-span-1"><option>AUTO</option><option>MANUAL</option></select>
          <input v-model.number="project.defaultInstance.scanMs" type="number" class="input" />
        </div>

        <h2 class="section-title">Sequencers</h2>
        <div class="space-y-2 mb-4">
          <button v-for="seq in project.sequencers" :key="seq.id" @click="selected.sequencerId = seq.id; selected.kind = 'step'; selected.id = seq.steps[0]?.id" class="w-full text-left panel rounded-xl p-3 hover:border-cyan-300/40" :class="seq.id === selected.sequencerId ? 'border-cyan-300/50 bg-cyan-500/10' : ''">
            <div class="flex items-center justify-between gap-2"><span class="font-bold text-slate-100">{{ seq.name }}</span><span class="text-xs text-slate-400">P{{ seq.priority }}</span></div>
            <p class="text-xs text-slate-400 mt-1 truncate">{{ seq.startCondition }}</p>
          </button>
        </div>

        <h2 class="section-title">Commands</h2>
        <div class="panel rounded-xl p-3 space-y-2 text-xs">
          <div v-for="(_, key) in project.commands" :key="key">
            <label class="uppercase text-slate-500">{{ key }}</label>
            <input v-model="project.commands[key]" class="input mt-1 mono text-xs" />
          </div>
        </div>
      </aside>

      <section class="glass rounded-2xl min-h-0 overflow-hidden grid grid-rows-[58px_1fr]">
        <div class="px-4 py-3 border-b border-slate-700/60 flex items-center justify-between">
          <div class="flex items-center gap-3"><Workflow class="w-5 h-5 text-cyan-300"/><div><h2 class="font-bold">{{ currentSequencer.name }} Sequencer</h2><p class="text-xs text-slate-400">Start condition: <span class="mono">{{ currentSequencer.startCondition }}</span></p></div></div>
          <div class="flex items-center gap-2 text-xs"><span class="chip-green">AUTO</span><span class="chip-blue">drag cards to rearrange</span></div>
        </div>

        <div ref="canvasRef" class="relative overflow-auto scrollbar grid-bg">
          <div class="relative" :style="{ width: canvasSize.width + 'px', height: canvasSize.height + 'px' }">
            <svg :width="canvasSize.width" :height="canvasSize.height" class="absolute inset-0 pointer-events-none">
              <defs>
                <marker id="arrow" viewBox="0 0 10 10" refX="5" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M 0 0 L 10 5 L 0 10 z" fill="rgba(34,211,238,.75)"/></marker>
              </defs>
              <g v-for="t in currentSequencer.transitions" :key="t.id">
                <path v-if="isBackTransition(t)" :d="branchPath(t)" class="connector" marker-end="url(#arrow)"/>
                <path v-else :d="pathForTransition(t)" class="connector" marker-end="url(#arrow)"/>
              </g>
            </svg>

            <button v-for="t in currentSequencer.transitions" :key="t.id" @click="chooseTransition(currentSequencer.id, t.id)" class="transition-card absolute text-left" :class="selected.kind === 'transition' && selected.id === t.id ? 'selected-transition' : ''" :style="{ left: transitionPoint(t).x + 'px', top: (transitionPoint(t).y - 28) + 'px' }">
              <div class="flex items-center gap-2"><GitBranch class="w-3.5 h-3.5 text-cyan-300"/><span class="font-bold truncate">{{ t.name }}</span><span class="ml-auto text-[10px] text-slate-400">P{{ t.priority }}</span></div>
              <div class="mono text-[10px] text-slate-400 truncate mt-1">{{ t.condition }}</div>
            </button>

            <div v-for="step in currentSequencer.steps" :key="step.id" @pointerdown="startDrag($event, step)" @dblclick="autoLayout" class="node-card absolute w-[280px] rounded-2xl border p-3 shadow-xl" :class="stepTypeClass(step)" :style="{ left: step.x + 'px', top: step.y + 'px' }">
              <div class="flex items-center gap-2">
                <CircleDot v-if="step.type === 'start'" class="w-4 h-4 text-emerald-300"/>
                <Square v-else-if="step.type === 'final'" class="w-4 h-4 text-amber-300"/>
                <Activity v-else class="w-4 h-4 text-cyan-300"/>
                <div class="font-bold truncate flex-1">{{ step.name }}</div>
                <Move class="w-4 h-4 text-slate-500"/>
              </div>
              <div class="grid grid-cols-3 gap-1 mt-3 text-[10px]">
                <div class="phase-box text-emerald-200">I {{ step.init.length }}</div>
                <div class="phase-box text-cyan-200">P {{ step.process.length }}</div>
                <div class="phase-box text-amber-200">T {{ step.terminate.length }}</div>
              </div>
              <div class="mt-2 text-[10px] text-slate-400 truncate">guard: <span class="mono">{{ step.entryGuard }}</span></div>
              <div class="mt-1 text-[10px] text-slate-400">min {{ step.minTimeMs }} ms · max {{ step.maxTimeMs || '∞' }} ms</div>
              <div v-if="step.activeMonitors.length" class="mt-2 flex items-center gap-1 text-[10px] text-red-200"><ShieldAlert class="w-3 h-3"/> {{ step.activeMonitors.length }} monitor{{ step.activeMonitors.length > 1 ? 's' : '' }}</div>
            </div>
          </div>
        </div>
      </section>

      <aside class="glass rounded-2xl overflow-hidden grid grid-rows-[48px_1fr] min-h-0">
        <div class="border-b border-slate-700/60 flex">
          <button @click="activeTab='inspector'" class="tab" :class="activeTab==='inspector' ? 'tab-active' : ''">Inspector</button>
          <button @click="activeTab='json'" class="tab" :class="activeTab==='json' ? 'tab-active' : ''">JSON</button>
          <button @click="activeTab='as'" class="tab" :class="activeTab==='as' ? 'tab-active' : ''">AngelScript</button>
          <button @click="activeTab='validate'" class="tab" :class="activeTab==='validate' ? 'tab-active' : ''">Check</button>
        </div>

        <div class="overflow-auto scrollbar p-4">
          <div v-if="activeTab === 'inspector' && selected.kind === 'step' && selectedStep" class="space-y-4">
            <div class="flex items-center justify-between"><h2 class="font-bold flex items-center gap-2"><Activity class="w-4 h-4 text-cyan-300"/>Step</h2><button @click="removeSelected" class="text-red-300 hover:text-red-200"><Trash2 class="w-4 h-4"/></button></div>
            <div class="grid grid-cols-2 gap-2"><div><label class="label">ID</label><input v-model="selectedStep.id" class="input mono text-sm"/></div><div><label class="label">Type</label><select v-model="selectedStep.type" class="input text-sm"><option>start</option><option>normal</option><option>final</option></select></div></div>
            <div><label class="label">Name</label><input v-model="selectedStep.name" class="input"/></div>
            <div><label class="label">Entry guard</label><input v-model="selectedStep.entryGuard" class="input mono text-sm"/></div>
            <div class="grid grid-cols-2 gap-2"><div><label class="label">Min time ms</label><input type="number" v-model.number="selectedStep.minTimeMs" class="input"/></div><div><label class="label">Max time ms</label><input type="number" v-model.number="selectedStep.maxTimeMs" class="input"/></div></div>
            <div><label class="label text-emerald-300 flex items-center gap-1"><PlayCircle class="w-3 h-3"/>Initialization action</label><textarea :value="listText(selectedStep.init)" @input="setList(selectedStep, 'init', $event.target.value)" class="textarea h-20"></textarea></div>
            <div><label class="label text-cyan-300 flex items-center gap-1"><Zap class="w-3 h-3"/>Cyclic processing action</label><textarea :value="listText(selectedStep.process)" @input="setList(selectedStep, 'process', $event.target.value)" class="textarea h-24"></textarea></div>
            <div><label class="label text-amber-300 flex items-center gap-1"><GripHorizontal class="w-3 h-3"/>Termination action</label><textarea :value="listText(selectedStep.terminate)" @input="setList(selectedStep, 'terminate', $event.target.value)" class="textarea h-20"></textarea></div>
            <div class="panel rounded-xl p-3"><div class="flex items-center justify-between mb-2"><h3 class="text-sm font-bold text-red-200 flex items-center gap-2"><ShieldAlert class="w-4 h-4"/>Active monitors</h3><button @click="addMonitor(selectedStep)" class="btn text-xs rounded-lg border border-red-300/30 bg-red-500/10 px-2 py-1">Add</button></div><div v-for="(m,i) in selectedStep.activeMonitors" :key="i" class="space-y-2 border-t border-slate-700 py-2"><input v-model="m.condition" class="input mono text-xs"/><div class="grid grid-cols-2 gap-2"><input v-model="m.faultCode" class="input mono text-xs"/><input v-model="m.targetSequencer" class="input mono text-xs"/></div><input v-model="m.message" class="input text-xs"/><button @click="removeMonitor(selectedStep,i)" class="text-xs text-red-300">remove</button></div></div>
          </div>

          <div v-if="activeTab === 'inspector' && selected.kind === 'transition' && selectedTransition" class="space-y-4">
            <div class="flex items-center justify-between"><h2 class="font-bold flex items-center gap-2"><GitBranch class="w-4 h-4 text-cyan-300"/>Transition</h2><button @click="removeSelected" class="text-red-300 hover:text-red-200"><Trash2 class="w-4 h-4"/></button></div>
            <div><label class="label">ID</label><input v-model="selectedTransition.id" class="input mono text-sm"/></div>
            <div><label class="label">Name</label><input v-model="selectedTransition.name" class="input"/></div>
            <div class="grid grid-cols-2 gap-2"><div><label class="label">From</label><select v-model="selectedTransition.from[0]" class="input"><option v-for="s in currentSequencer.steps" :key="s.id">{{ s.id }}</option></select></div><div><label class="label">To</label><select v-model="selectedTransition.to[0]" class="input"><option v-for="s in currentSequencer.steps" :key="s.id">{{ s.id }}</option></select></div></div>
            <div><label class="label">Condition</label><textarea v-model="selectedTransition.condition" class="textarea h-24"></textarea></div>
            <div><label class="label">Transition action</label><textarea :value="listText(selectedTransition.action)" @input="setList(selectedTransition, 'action', $event.target.value)" class="textarea h-20"></textarea></div>
            <div><label class="label">OS/test comment</label><input v-model="selectedTransition.osComment" class="input"/></div>
            <div><label class="label">Priority</label><input type="number" v-model.number="selectedTransition.priority" class="input"/></div>
          </div>

          <div v-if="activeTab === 'json'" class="space-y-3"><div class="flex flex-wrap gap-2"><button @click="copyJson" class="btn-small"><Copy class="w-4 h-4"/>Copy</button><button @click="downloadJson" class="btn-small amber"><FileJson class="w-4 h-4"/>Export</button><button @click="loadLocal" class="btn-small"><Upload class="w-4 h-4"/>Load local</button><label class="btn-small blue cursor-pointer"><Upload class="w-4 h-4"/>Import<input type="file" accept="application/json" @change="importJson" class="hidden"/></label></div><pre class="code-box">{{ jsonText }}</pre></div>
          <div v-if="activeTab === 'as'" class="space-y-3"><button @click="copyAs" class="btn-small"><Braces class="w-4 h-4"/>Copy skeleton</button><pre class="code-box">{{ angelPreview }}</pre></div>
          <div v-if="activeTab === 'validate'" class="space-y-3"><div class="panel rounded-xl p-3"><h2 class="font-bold flex items-center gap-2"><CheckCircle2 class="w-4 h-4 text-emerald-300"/>Validation</h2><p class="text-sm text-slate-400 mt-1">Checks topology, missing links, step timing, monitor targets, and SFC authoring hazards before future transpilation.</p></div><div v-if="validation.length === 0" class="rounded-xl border border-emerald-300/30 bg-emerald-500/10 p-3 text-emerald-100">No validation issues detected.</div><div v-for="issue in validation" :key="issue.text" class="rounded-xl border p-3 text-sm" :class="issue.level === 'error' ? 'border-red-300/30 bg-red-500/10 text-red-100' : 'border-amber-300/30 bg-amber-500/10 text-amber-100'"><div class="flex gap-2"><AlertTriangle class="w-4 h-4 shrink-0 mt-0.5"/>{{ issue.text }}</div></div></div>
        </div>
      </aside>
    </main>
    <div v-if="toast" class="fixed bottom-5 left-1/2 -translate-x-1/2 rounded-xl border border-cyan-300/30 bg-slate-950/95 px-4 py-2 text-cyan-100 shadow-xl">{{ toast }}</div>
  </div>
</template>
