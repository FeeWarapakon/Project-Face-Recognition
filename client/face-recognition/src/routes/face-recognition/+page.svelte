<script lang="ts">
  import { onMount } from 'svelte';
  import { playSound, playButtonPressSound, playToggleSound, playWarningBeep, type SoundType } from '$lib/audio';

  // Live telemetry states
  let serverOnline = $state(true);
  let cameraConnected = $state(true);
  let cameraError = $state(false);
  let camFps = $state(28.3);
  let displayFps = $state(28.3);
  let inferenceTime = $state(42);

  // Sound cooldown & settings states
  let lastSoundPlayedAt = 0;
  let lastIdentifiedPerson = '';
  let soundMuted = $state(false);

  // ── Resizable Split-Pane Layout State (Desktop & Pi 7" Touchscreen) ──
  let sidebarWidth = $state(420);
  let isDraggingSplitter = $state(false);
  let isSidebarCollapsed = $state(false);
  let containerRef: HTMLDivElement | null = $state(null);

  let isNarrowSidebar = $derived(sidebarWidth < 340);
  let isMediumSidebar = $derived(sidebarWidth >= 340 && sidebarWidth < 460);
  let isWideSidebar = $derived(sidebarWidth >= 460);

  function startDragging(e: MouseEvent | TouchEvent) {
    isDraggingSplitter = true;
    if (typeof window !== 'undefined') {
      window.addEventListener('mousemove', handleDragging);
      window.addEventListener('mouseup', stopDragging);
      window.addEventListener('touchmove', handleDragging, { passive: false });
      window.addEventListener('touchend', stopDragging);
    }
  }

  function handleDragging(e: MouseEvent | TouchEvent) {
    if (!isDraggingSplitter || !containerRef) return;
    if (e.cancelable) e.preventDefault();

    const clientX = 'touches' in e ? e.touches[0].clientX : e.clientX;
    const containerRect = containerRef.getBoundingClientRect();
    const newWidth = containerRect.right - clientX;

    const minW = 220;
    const maxW = Math.max(minW, containerRect.width - 240);

    sidebarWidth = Math.max(minW, Math.min(newWidth, maxW));
    if (isSidebarCollapsed) isSidebarCollapsed = false;
  }

  function stopDragging() {
    isDraggingSplitter = false;
    if (typeof window !== 'undefined') {
      window.removeEventListener('mousemove', handleDragging);
      window.removeEventListener('mouseup', stopDragging);
      window.removeEventListener('touchmove', handleDragging);
      window.removeEventListener('touchend', stopDragging);
      localStorage.setItem('face_rec_sidebar_width', String(sidebarWidth));
      localStorage.setItem('face_rec_sidebar_collapsed', String(isSidebarCollapsed));
    }
  }

  function setSidebarPreset(preset: 'pi7' | 'balanced' | 'wide' | 'collapse') {
    playButtonPressSound();
    if (preset === 'collapse') {
      isSidebarCollapsed = !isSidebarCollapsed;
    } else if (preset === 'pi7') {
      isSidebarCollapsed = false;
      sidebarWidth = 260;
    } else if (preset === 'balanced') {
      isSidebarCollapsed = false;
      sidebarWidth = 420;
    } else if (preset === 'wide') {
      isSidebarCollapsed = false;
      sidebarWidth = 520;
    }
    if (typeof window !== 'undefined') {
      localStorage.setItem('face_rec_sidebar_width', String(sidebarWidth));
      localStorage.setItem('face_rec_sidebar_collapsed', String(isSidebarCollapsed));
    }
  }

  // Anti-Spoof backend telemetry
  let antiSpoofEnabled = $state(false);
  let antiSpoofAvailable = $state(false);
  let antispoofLatency = $state(0);

  import { PUBLIC_ADMIN_PASSWORD } from '$env/static/public';

  // ── Password Confirmation Modal for Anti-Spoof Toggle ─────────────────────
  const ADMIN_PASSWORD = PUBLIC_ADMIN_PASSWORD || 'admin@staff';
  let isAntiSpoofModalOpen = $state(false);
  let adminPasswordInput = $state('');
  let passwordModalError = $state('');
  let isTogglingAntiSpoof = $state(false);
  let showModalPassword = $state(false);

  function openAntiSpoofModal() {
    playButtonPressSound();
    isAntiSpoofModalOpen = true;
    adminPasswordInput = '';
    passwordModalError = '';
    showModalPassword = false;
  }

  function closeAntiSpoofModal() {
    playButtonPressSound();
    isAntiSpoofModalOpen = false;
    adminPasswordInput = '';
    passwordModalError = '';
  }

  async function handleConfirmAntiSpoofToggle(e?: Event) {
    if (e) e.preventDefault();
    if (!adminPasswordInput.trim()) {
      passwordModalError = 'กรุณากรอกรหัสผ่านผู้ดูแลระบบ';
      return;
    }
    if (adminPasswordInput.trim() !== ADMIN_PASSWORD) {
      passwordModalError = 'รหัสผ่านไม่ถูกต้อง กรุณาลองใหม่อีกครั้ง';
      return;
    }

    isTogglingAntiSpoof = true;
    passwordModalError = '';
    try {
      const res = await fetch('/api/toggle_antispoof', { method: 'POST' });
      if (res.ok) {
        const data = await res.json();
        antiSpoofEnabled = data.antispoof_enabled ?? !antiSpoofEnabled;
        closeAntiSpoofModal();
      } else {
        passwordModalError = 'ไม่สามารถเปลี่ยนสถานะ Anti-Spoof ได้';
      }
    } catch (err) {
      passwordModalError = 'เกิดข้อผิดพลาดในการเชื่อมต่อเซิร์ฟเวอร์';
    } finally {
      isTogglingAntiSpoof = false;
    }
  }

  // ── Screen-Replay Grace Period (เหลือง → แดงหลัง 3 วิ) ─────────────────────
  const SPOOF_RED_DELAY_MS = 3000;
  let spoofFirstSeenAt = 0;
  let spoofIsConfirmed = $state(false);

  interface RecognitionRecord {
    id: string;
    name: string;
    student_id?: string;
    role?: string;
    similarity: number;
    confidence: number;
    status: 'KNOWN' | 'UNKNOWN' | 'SPOOF';
    timestamp: string;
    latency_ms: number;
    image?: string | null;
    is_real?: boolean;
    liveness_score?: number;
    attack_type?: string;
  }

  // Snapshot memory & freeze state
  let currentSnapshot: string | null = $state(null);
  let lastSnapshotPerson = '';
  let lastSnapshotTime = 0;
  let lastFaceSeenAt = 0;

  let latestPerson = $state<RecognitionRecord>({
    id: 'active-init',
    name: 'Scanning...',
    student_id: '-',
    role: 'System Ready',
    similarity: 0.0,
    confidence: 0.0,
    status: 'UNKNOWN',
    timestamp: '--:--:--',
    latency_ms: 0,
    image: null,
    is_real: true,
    liveness_score: 1.0,
    attack_type: 'GENUINE_LIVE'
  });

  // History with smart debounce & mode control
  let historyList = $state<RecognitionRecord[]>([]);
  let eventLogMode = $state<'continuous' | 'once'>('continuous');
  const lastLoggedMap = new Map<string, number>();
  const loggedPersonsSet = new Set<string>();

  function setEventLogMode(mode: 'continuous' | 'once') {
    eventLogMode = mode;
    if (typeof localStorage !== 'undefined') {
      localStorage.setItem('face_event_log_mode', mode);
    }
    playButtonPressSound();
  }

  function clearHistory() {
    playButtonPressSound();
    historyList = [];
    lastLoggedMap.clear();
    loggedPersonsSet.clear();
    currentSnapshot = null;
    lastSnapshotPerson = '';
    lastSnapshotTime = 0;
  }

  function shouldLogToHistory(record: RecognitionRecord): boolean {
    const now = Date.now();
    const isUnknown = record.status === 'UNKNOWN';
    const key = isUnknown ? 'UNKNOWN_EVENT' : record.name;
    const lastTime = lastLoggedMap.get(key) || 0;

    // Mode: Once per person (บันทึกแค่คนละ 1 ครั้ง)
    if (eventLogMode === 'once') {
      if (!isUnknown) {
        const personKey = (record.student_id && record.student_id !== '-') ? record.student_id : record.name;
        if (loggedPersonsSet.has(personKey) || historyList.some(r => r.name === record.name || (r.student_id && r.student_id !== '-' && r.student_id === record.student_id))) {
          return false;
        }
      } else {
        if (now - lastTime < 5000) return false;
      }
    }

    // Unknown face: บันทึกซ้ำทุก 2.5 วิ
    if (isUnknown) {
      if (now - lastTime > 2500) {
        lastLoggedMap.set(key, now);
        return true;
      }
      return false;
    }

    // Known face: บันทึกซ้ำถ้าผ่านไป 3.0 วินาที หรือเป็นคนใหม่
    if (now - lastTime > 3000) {
      lastLoggedMap.set(key, now);
      return true;
    }
    return false;
  }

  let videoFeedImg: HTMLImageElement | null = $state(null);
  let videoFeedSrc = $state('/api/video_feed');
  let canvasWave: HTMLCanvasElement | null = $state(null);

  onMount(() => {
    let active = true;

    if (typeof localStorage !== 'undefined') {
      const savedMode = localStorage.getItem('face_event_log_mode');
      if (savedMode === 'continuous' || savedMode === 'once') {
        eventLogMode = savedMode;
      }
    }

    function handleCameraSwitched() {
      videoFeedSrc = `/api/video_feed?t=${Date.now()}`;
      cameraError = false;
    }
    if (typeof window !== 'undefined') {
      window.addEventListener('camera_switched', handleCameraSwitched);
    }

    // 1. Animated cyber wave
    if (canvasWave) {
      const cvs = canvasWave;
      const ctx = cvs.getContext('2d');
      if (ctx) {
        let step = 0;
        const renderWave = () => {
          step += 0.04;
          const w = (cvs.width = cvs.parentElement?.clientWidth || 600);
          const h = (cvs.height = 36);
          ctx.clearRect(0, 0, w, h);
          ctx.beginPath();
          ctx.strokeStyle = 'rgba(245, 158, 11, 0.85)';
          ctx.lineWidth = 2;
          ctx.shadowBlur = 8;
          ctx.shadowColor = '#f59e0b';
          for (let x = 0; x < w; x += 10) {
            const y = h / 2 + Math.sin(x * 0.02 + step) * 10 + Math.sin(x * 0.05 - step * 0.8) * 4;
            if (x === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
            if (x % 40 === 0) { ctx.fillStyle = '#fef08a'; ctx.fillRect(x - 2, y - 2, 4, 4); }
          }
          ctx.stroke();
          ctx.beginPath();
          ctx.strokeStyle = 'rgba(16, 185, 129, 0.4)';
          ctx.lineWidth = 1.5;
          for (let x = 0; x < w; x += 15) {
            const y = h / 2 + Math.cos(x * 0.03 - step * 1.2) * 8;
            if (x === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
          }
          ctx.stroke();
          if (active) requestAnimationFrame(renderWave);
        };
        renderWave();
      }
    }

    // 2. Load sound & layout preferences from localStorage
    if (typeof window !== 'undefined') {
      const savedWidth = localStorage.getItem('face_rec_sidebar_width');
      if (savedWidth) {
        const parsed = parseInt(savedWidth, 10);
        if (!isNaN(parsed) && parsed >= 180 && parsed <= 900) {
          sidebarWidth = parsed;
        }
      } else if (window.innerWidth <= 860) {
        // Auto-fit 260px for Raspberry Pi 7" display (800x480)
        sidebarWidth = 260;
      }
      const savedCollapsed = localStorage.getItem('face_rec_sidebar_collapsed');
      if (savedCollapsed === 'true') {
        isSidebarCollapsed = true;
      }
      const savedMute = localStorage.getItem('face_sound_enabled');
      if (savedMute !== null) {
        soundMuted = savedMute === 'false';
      }
    }

    // 3. Telemetry and Recognition Polling
    async function fetchData() {
      if (!active) return;
      try {
        const [recRes, statusRes] = await Promise.allSettled([
          fetch('/api/latest_recognition'),
          fetch('/api/status')
        ]);

        if (statusRes.status === 'fulfilled' && statusRes.value.ok) {
          const statusData = await statusRes.value.json();
          serverOnline = true;
          cameraConnected = statusData.camera_connected ?? true;
          camFps = statusData.cam_fps || 28.3;
          inferenceTime = statusData.latency?.total_ms || 42;
          if (statusData.antispoof_available !== undefined) antiSpoofAvailable = statusData.antispoof_available;
          if (statusData.antispoof_enabled !== undefined) antiSpoofEnabled = statusData.antispoof_enabled;
        }

        if (recRes.status === 'fulfilled' && recRes.value.ok) {
          const recData = await recRes.value.json();
          const face = recData.latest_face || recData.last_identified || recData.latest;
          if (face && face.name && face.name !== 'Scanning...') {
            serverOnline = true;
            lastFaceSeenAt = Date.now();
            const isSpoof = face.status === 'SPOOF' || face.is_real === false;
            const now = Date.now();

            if (isSpoof) {
              if (spoofFirstSeenAt === 0) spoofFirstSeenAt = now;
              spoofIsConfirmed = (now - spoofFirstSeenAt) >= SPOOF_RED_DELAY_MS;
            } else {
              spoofFirstSeenAt = 0;
              spoofIsConfirmed = false;
            }

            // Freeze snapshot logic: Captured still image (updates only on person change, event debounce, or initial load)
            const isPersonChanged = face.name !== lastSnapshotPerson;
            const isCooldownExpired = (now - lastSnapshotTime) > 3500;
            if (face.face_crop_base64 && (isPersonChanged || isCooldownExpired || !currentSnapshot)) {
              currentSnapshot = face.face_crop_base64;
              lastSnapshotPerson = face.name;
              lastSnapshotTime = now;
            }

            const faceImage = currentSnapshot || face.face_crop_base64 || face.image;

            const newRecord: RecognitionRecord = {
              id: `${Date.now()}-${Math.random()}`,
              name: face.name,
              student_id: face.student_id || '-',
              role: face.role || 'นักศึกษา',
              similarity: face.similarity || 0.0,
              confidence: face.confidence || face.similarity || 0.0,
              status: isSpoof ? 'SPOOF' : (face.status === 'KNOWN' ? 'KNOWN' : 'UNKNOWN'),
              timestamp: face.timestamp || new Date().toLocaleTimeString('th-TH', { hour12: false }),
              latency_ms: face.latency_ms || inferenceTime,
              image: faceImage,
              is_real: face.is_real ?? !isSpoof,
              liveness_score: face.liveness_score ?? 1.0,
              attack_type: face.attack_type || 'GENUINE_LIVE'
            };

            latestPerson = newRecord;

            const isNewCaptureEvent = shouldLogToHistory(newRecord);

            // Trigger sound effects for recognized faces
            if (newRecord.status === 'KNOWN' && !soundMuted && isNewCaptureEvent) {
              const soundCooldown = 2500;
              if (now - lastSoundPlayedAt > soundCooldown || lastIdentifiedPerson !== newRecord.name) {
                lastSoundPlayedAt = now;
                lastIdentifiedPerson = newRecord.name;
                const savedSound = (localStorage.getItem('face_sound_type') || 'cyber_ping') as SoundType;
                const savedVol = Number(localStorage.getItem('face_sound_volume') || 85);
                playSound(savedSound, savedVol);
              }
            }

            // History logging with Smart Debounce (Max 20 for continuous, max 100 for once)
            if (!isSpoof && isNewCaptureEvent) {
              if (newRecord.status === 'KNOWN') {
                const personKey = (newRecord.student_id && newRecord.student_id !== '-') ? newRecord.student_id : newRecord.name;
                loggedPersonsSet.add(personKey);
              }
              const maxLogs = eventLogMode === 'once' ? 100 : 20;
              historyList = [newRecord, ...historyList].slice(0, maxLogs);
            }
          } else if (Date.now() - lastFaceSeenAt > 2000) {
            // เมื่อไม่มีใบหน้าในเฟรมเกิน 2 วินาที สลับการ์ดกลับสู่สถานะ Scanning... อัตโนมัติ ไม่ค้างที่คนเดิม
            latestPerson = {
              id: 'scanning-idle',
              name: 'Scanning...',
              student_id: '-',
              role: 'System Ready',
              similarity: 0.0,
              confidence: 0.0,
              status: 'UNKNOWN',
              timestamp: '--:--:--',
              latency_ms: 0,
              image: null,
              is_real: true,
              liveness_score: 1.0,
              attack_type: 'GENUINE_LIVE'
            };
            currentSnapshot = null;
            lastSnapshotPerson = '';
          }
        }
      } catch (e) {
        // offline fallback
      }
    }

    fetchData();
    const interval = setInterval(fetchData, 350);
    return () => {
      active = false;
      clearInterval(interval);
      if (typeof window !== 'undefined') {
        window.removeEventListener('camera_switched', handleCameraSwitched);
      }
    };
  });
</script>

<svelte:head>
  <title>Face Recognition // ระบบรู้จำใบหน้า</title>
</svelte:head>

<div class="h-full flex flex-col min-h-0 select-none w-full max-w-[1920px] mx-auto overflow-hidden">
  <!-- Interactive Resizable Layout Container (Supports Touch & Mouse Drag on Pi 7" and Desktop) -->
  <div
    bind:this={containerRef}
    class="flex flex-col lg:flex-row gap-2 lg:gap-0 items-stretch flex-1 min-h-0 overflow-y-auto lg:overflow-hidden relative"
  >

    <!-- LEFT COLUMN: Live Stream CRT Console (Expands to fill available space) -->
    <div
      class="chassis-panel p-2.5 sm:p-3.5 rounded-2xl flex flex-col justify-between min-h-[280px] sm:min-h-[340px] lg:min-h-0 overflow-hidden space-y-2 relative min-w-0 flex-1 transition-[flex] duration-75"
    >
      <!-- Corner Screws on Chassis -->
      <div class="absolute top-1.5 left-2 screw-marker">+</div>
      <div class="absolute top-1.5 right-2 screw-marker">+</div>

      <!-- Top Bar: Industrial Header & HUD Status -->
      <div class="h-8 flex justify-between items-center shrink-0 px-1 gap-2 overflow-hidden border-b border-[#283348] pb-1.5">
        <div class="flex items-center gap-2 min-w-0 overflow-hidden">
          <span class="bg-rose-950/80 text-rose-300 text-[10px] sm:text-xs px-2.5 py-0.5 rounded border border-rose-500/50 flex items-center gap-1.5 font-mono font-bold tracking-wider shrink-0 shadow-[0_0_8px_rgba(244,63,94,0.3)]">
            <span class="w-1.5 h-1.5 rounded-full bg-rose-500 animate-pulse"></span>
            REC // V4L2
          </span>
          <span class="text-xs sm:text-sm font-mono font-bold tracking-wider text-amber-300 whitespace-nowrap shrink-0 hidden sm:inline">
            CAMERA_SENSOR // OPTIC_STREAM
          </span>

          <!-- Security Status Pill in Top Bar -->
          {#if spoofIsConfirmed}
            <span class="bg-rose-950/90 text-rose-300 text-[10px] sm:text-xs font-bold px-2.5 py-0.5 rounded border border-rose-500 flex items-center gap-1 shadow-[0_0_12px_rgba(244,63,94,0.4)] animate-pulse whitespace-nowrap shrink-0 font-mono">
              🚨 SPOOF_REJECTED
            </span>
          {:else if latestPerson.status === 'SPOOF'}
            <span class="bg-amber-950/90 text-amber-300 text-[10px] sm:text-xs font-bold px-2.5 py-0.5 rounded border border-amber-500/80 flex items-center gap-1 shadow-[0_0_12px_rgba(245,158,11,0.3)] whitespace-nowrap shrink-0 font-mono">
              ⚠️ LIVENESS_VERIFYING
            </span>
          {/if}
        </div>

        <div class="flex items-center gap-2 shrink-0 font-mono text-xs">
          <!-- Quick Screen Layout Presets for Pi 7" and Desktop -->
          <div class="hidden lg:flex items-center gap-1 bg-[#020406] px-1.5 py-0.5 rounded border border-[#283348]">
            <button
              onclick={() => setSidebarPreset('pi7')}
              class="px-1.5 py-0.5 rounded text-[10px] font-bold text-slate-300 hover:text-amber-300 hover:bg-amber-500/10 transition cursor-pointer"
              title="ปรับสัดส่วนสำหรับหน้าจอ Raspberry Pi 7 นิ้ว (เน้นกล้องใหญ่)"
            >🖥️ Pi 7"</button>
            <button
              onclick={() => setSidebarPreset('balanced')}
              class="px-1.5 py-0.5 rounded text-[10px] font-bold text-slate-300 hover:text-amber-300 hover:bg-amber-500/10 transition cursor-pointer"
              title="ปรับสัดส่วนมาตรฐาน 50:50"
            >⚖️ 50:50</button>
            <button
              onclick={() => setSidebarPreset('collapse')}
              class="px-1.5 py-0.5 rounded text-[10px] font-bold {isSidebarCollapsed ? 'text-amber-300 bg-amber-500/20' : 'text-slate-400 hover:text-white'} transition cursor-pointer"
              title="พับ/กางแถบข้อมูลเพื่อดูกล้องแบบเต็มจอ"
            >{isSidebarCollapsed ? '👁️ SHOW' : '✖ HIDE'}</button>
          </div>

          <span class="font-bold text-amber-400 bg-[#020406] px-2.5 py-0.5 rounded border border-[#283348] whitespace-nowrap">{camFps} FPS</span>
          <span class="font-bold text-emerald-400 bg-[#020406] px-2.5 py-0.5 rounded border border-[#283348] whitespace-nowrap">NPU: {inferenceTime}ms</span>
        </div>
      </div>

      <!-- Camera Viewport: Crystal Clear Video Stream (Clean & Zero Scanline Obstruction) -->
      <div class="relative terminal-screen rounded-xl flex-1 min-h-[220px] sm:min-h-[260px] flex items-center justify-center border-2 {
        spoofIsConfirmed
          ? 'border-rose-500 shadow-[0_0_30px_rgba(244,63,94,0.4)]'
          : (latestPerson.status === 'SPOOF' && !spoofIsConfirmed)
            ? 'border-amber-400 shadow-[0_0_30px_rgba(245,158,11,0.35)]'
            : 'border-[#283348] shadow-[inset_0_0_20px_rgba(0,0,0,0.8)]'
      } overflow-hidden group transition-all duration-300">
        
        <!-- Subtle Corner Viewfinder Crosshairs -->
        <div class="absolute top-3 left-3 w-4 h-4 border-t-2 border-l-2 border-amber-400 z-20 pointer-events-none"></div>
        <div class="absolute top-3 right-3 w-4 h-4 border-t-2 border-r-2 border-amber-400 z-20 pointer-events-none"></div>
        <div class="absolute bottom-3 left-3 w-4 h-4 border-b-2 border-l-2 border-amber-400 z-20 pointer-events-none"></div>
        <div class="absolute bottom-3 right-3 w-4 h-4 border-b-2 border-r-2 border-amber-400 z-20 pointer-events-none"></div>

        <!-- Live Camera Stream Image -->
        <img
          bind:this={videoFeedImg}
          src={videoFeedSrc}
          alt="Live Face Recognition Stream"
          class="w-full h-full object-contain bg-black relative z-10"
          onload={() => { cameraConnected = true; serverOnline = true; cameraError = false; }}
          onerror={() => {
            cameraError = true;
            setTimeout(() => {
              videoFeedSrc = `/api/video_feed?t=${Date.now()}`;
            }, 2000);
          }}
        />

        {#if cameraError}
          <div class="absolute inset-0 z-20 flex flex-col items-center justify-center bg-[#020406] text-slate-500 p-4">
            <div class="relative w-44 sm:w-56 h-48 sm:h-60 rounded-2xl border-2 border-dashed border-amber-500/40 flex flex-col items-center justify-center p-3 shadow-[0_0_25px_rgba(245,158,11,0.15)]">
              <svg class="w-16 sm:w-20 h-16 sm:h-20 text-slate-600 animate-pulse" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.2" d="M16 7a4 4 0 11-8 0 4 4 0 018 0zM12 14a7 7 0 00-7 7h14a7 7 0 00-7-7z" />
              </svg>
              <div class="mt-3 bg-amber-400 text-slate-950 px-3 py-1 rounded text-xs font-black font-mono tracking-wider shadow">AWAITING CAMERA SENSOR...</div>
            </div>
          </div>
        {/if}
      </div>

      <!-- High-Tech Hardware Telemetry & Vacuum Tubes Deck -->
      <div class="bg-[#020406] px-3 py-1.5 sm:py-2 rounded-xl border border-[#283348] flex items-center justify-between gap-3 shrink-0 shadow-inner overflow-hidden font-mono">
        <!-- Left: Glowing Vacuum Tubes Array (V1 -> V2 -> V3) + Engine Info -->
        <div class="flex items-center gap-2.5 sm:gap-3.5 shrink-0">
          <div class="flex items-center gap-1.5 sm:gap-2.5 py-0.5">
            {#each [1, 2, 3] as tubeIdx}
              <div class="relative flex flex-col items-center">
                <div class="relative w-3.5 sm:w-4.5 h-6 sm:h-7.5 rounded-t-full bg-gradient-to-b from-white/20 via-white/5 to-transparent border border-white/25 shadow-[0_0_12px_rgba(245,158,11,0.65)] flex items-center justify-center overflow-hidden">
                  <div class="absolute top-0.5 left-0.5 w-0.5 h-2 rounded-full bg-white/40 blur-[0.5px]"></div>
                  <div class="relative w-1 h-3 flex flex-col items-center justify-between">
                    <div class="w-1 h-1 rounded-full bg-[#ffedd5] shadow-[0_0_6px_2px_#f59e0b] scale-110"></div>
                    <div class="w-[1px] h-full bg-[#fef08a] shadow-[0_0_5px_#f97316]"></div>
                    <div class="w-1 h-1 rounded-full bg-[#ffedd5] shadow-[0_0_6px_2px_#f59e0b] scale-110"></div>
                  </div>
                </div>
                <div class="w-4 sm:w-5 h-1 bg-gradient-to-r from-[#475569] via-[#94a3b8] to-[#334155] rounded-b-sm border-t border-black shadow-sm flex items-center justify-center">
                  <span class="text-[5px] text-black font-extrabold">V{tubeIdx}</span>
                </div>
              </div>
            {/each}
          </div>

          <div class="flex flex-col justify-center">
            <span class="text-xs sm:text-sm font-bold text-amber-300 flex items-center gap-1.5">
              <span class="w-2 h-2 rounded-full bg-emerald-400 animate-pulse"></span>
              <span>TENSOR_CORE // 512-D</span>
            </span>
            <span class="text-[9px] sm:text-[10px] text-slate-400">MOBILEFACENET INT8</span>
          </div>
        </div>

        <!-- Right: Diagnostic Status Badges -->
        <div class="flex items-center gap-1.5 sm:gap-2 text-[10px] sm:text-xs">
          <span class="px-2 py-0.5 rounded bg-[#0f1420] text-amber-300 border border-[#283348] font-bold whitespace-nowrap">
            PI-5 ACTIVE
          </span>
        </div>
      </div>
    </div>

    <!-- RESIZABLE SPLITTER HANDLE (Interactive Drag Bar for Mouse & Pi Touchscreen) -->
    <!-- svelte-ignore a11y_no_noninteractive_element_interactions -->
    <div
      class="hidden lg:flex flex-col items-center justify-center w-3.5 shrink-0 mx-0.5 cursor-col-resize select-none group relative z-30"
      onmousedown={startDragging}
      ontouchstart={startDragging}
      title="คลิกลากซ้าย-ขวา เพื่อปรับขนาดยืดหดหน้าจอ"
      role="separator"
      tabindex="0"
      aria-valuenow={sidebarWidth}
      aria-orientation="vertical"
    >
      <div class="w-1.5 h-full rounded-full transition-all duration-200 {isDraggingSplitter ? 'bg-amber-400 shadow-[0_0_12px_#f59e0b] scale-x-150' : 'bg-[#283348] group-hover:bg-amber-500/80 group-hover:shadow-[0_0_8px_rgba(245,158,11,0.5)]'} flex flex-col items-center justify-center gap-2 py-6">
        <div class="w-1 h-1 rounded-full bg-amber-200/60"></div>
        <div class="w-1 h-1 rounded-full bg-amber-200/90"></div>
        <div class="w-1 h-1 rounded-full bg-amber-200/60"></div>
      </div>
    </div>

    <!-- RIGHT COLUMN: Snug Recognition Details & Event History (Resizable Width) -->
    <div
      class="flex flex-col gap-2 min-h-0 overflow-hidden h-full shrink-0 transition-all duration-75 {isSidebarCollapsed ? 'w-0 hidden lg:hidden' : 'w-full lg:w-auto'}"
      style={isSidebarCollapsed ? 'display: none;' : `width: ${sidebarWidth}px; max-width: 100%;`}
    >

      <!-- 1. Recognition Result Card (Adaptive Height & Layout) -->
      <div class="chassis-panel p-2.5 sm:p-3.5 rounded-2xl shadow-2xl flex flex-col justify-between shrink-0 transition-colors duration-300 relative overflow-hidden {isNarrowSidebar ? 'min-h-[220px]' : 'min-h-[260px]'} {
        spoofIsConfirmed ? 'border-rose-500/70 shadow-[0_0_20px_rgba(244,63,94,0.3)]'
        : (latestPerson.status === 'SPOOF' ? 'border-amber-500/70 shadow-[0_0_20px_rgba(245,158,11,0.3)]' : 'border-[#283348]')
      }">
        
        <!-- Corner Screws -->
        <div class="absolute top-1.5 left-2 screw-marker">+</div>
        <div class="absolute top-1.5 right-2 screw-marker">+</div>

        <!-- Header -->
        <div class="flex items-center justify-between gap-1.5 shrink-0 min-w-0 border-b border-[#283348] pb-1.5 mb-1.5">
          <h3 class="text-xs font-black uppercase tracking-wider text-amber-300 font-mono flex items-center gap-1.5 min-w-0 truncate">
            <span class="w-2 h-2 rounded-full shrink-0 {
              spoofIsConfirmed ? 'bg-rose-500 animate-ping'
              : (latestPerson.status === 'SPOOF' ? 'bg-amber-400 animate-ping' : 'bg-emerald-400 animate-ping')
            }"></span>
            <span class="truncate">{isNarrowSidebar ? '>> RECOGNITION' : '>> RECOGNITION_OUTPUT'}</span>
          </h3>
          
          <div class="flex items-center gap-1.5 shrink-0">
            <!-- Anti-Spoof Toggle -->
            <button
              onclick={openAntiSpoofModal}
              class="text-[10px] sm:text-xs font-mono font-bold {antiSpoofEnabled ? 'text-amber-300 bg-amber-950/80 border-amber-500/60 shadow-[0_0_8px_rgba(245,158,11,0.3)]' : 'text-slate-400 bg-black/60 border-slate-700'} px-2 py-0.5 sm:px-2.5 sm:py-1 rounded-lg border whitespace-nowrap shrink-0 cursor-pointer hover:scale-105 transition active:scale-95 flex items-center gap-1"
              title="สลับโหมดป้องกันการปลอมแปลง (ต้องยืนยันรหัสผ่าน)"
            >
              <span>{antiSpoofEnabled ? (isNarrowSidebar ? '🛡️ ON' : '🛡️ SPOOF: ON') : (isNarrowSidebar ? '🛡️ OFF' : '🛡️ SPOOF: OFF')}</span>
            </button>

            <!-- Sound Toggle -->
            <button
              onclick={() => {
                soundMuted = !soundMuted;
                playToggleSound(!soundMuted);
                if (typeof window !== 'undefined') localStorage.setItem('face_sound_enabled', String(!soundMuted));
                if (!soundMuted) playSound('cyber_ping', 85);
              }}
              class="text-[10px] sm:text-xs font-mono font-bold {soundMuted ? 'text-slate-400 bg-black/60 border-slate-700' : 'text-emerald-300 bg-emerald-950/80 border-emerald-500/60 shadow-[0_0_8px_rgba(16,185,129,0.3)]'} px-2 py-0.5 sm:px-2.5 sm:py-1 rounded-lg border whitespace-nowrap shrink-0 cursor-pointer hover:scale-105 transition active:scale-95 flex items-center gap-1"
              title={soundMuted ? 'เปิดเสียง' : 'ปิดเสียง'}
            >
              <span>{soundMuted ? '🔇' : (isNarrowSidebar ? '🔊' : '🔊 ON')}</span>
            </button>
          </div>
        </div>

        <!-- Profile Photo & Identity Details Responsive Card -->
        <div class="bg-[#020406] p-2 sm:p-3 rounded-xl border border-[#283348] shadow-inner min-w-0 flex-1 overflow-hidden relative flex flex-col justify-between gap-1.5">
          
          <!-- Upper Section (Left: Photo, Right: Identity Info) -->
          <div class="flex flex-row items-center gap-2.5 sm:gap-3.5 flex-1 min-h-0 min-w-0">
            
            <!-- Snapshot Box (Left - Dynamic Size based on sidebar width) -->
            <div class="relative {
              isNarrowSidebar 
                ? 'w-20 h-24 sm:w-24 sm:h-28' 
                : (isMediumSidebar ? 'w-24 sm:w-28 aspect-[3/4]' : 'w-28 sm:w-36 aspect-[3/4]')
            } rounded-xl overflow-hidden border-2 {
              spoofIsConfirmed
                ? 'border-rose-500 shadow-[0_0_15px_rgba(244,63,94,0.4)]'
                : (latestPerson.status === 'SPOOF'
                    ? 'border-amber-400 shadow-[0_0_15px_rgba(245,158,11,0.35)]'
                    : (latestPerson.status === 'KNOWN'
                        ? 'border-emerald-400 shadow-[0_0_15px_rgba(16,185,129,0.35)]'
                        : 'border-[#283348]'))
            } bg-black flex items-center justify-center shrink-0">
              {#if latestPerson.image}
                <img src={latestPerson.image} alt={latestPerson.name} class="w-full h-full object-cover object-center" />
              {:else}
                <div class="w-full h-full flex flex-col items-center justify-center bg-[#05080e] text-slate-500 p-1">
                  <svg class="{isNarrowSidebar ? 'w-6 h-6' : 'w-10 h-10'} text-slate-600" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.5" d="M16 7a4 4 0 11-8 0 4 4 0 018 0zM12 14a7 7 0 00-7 7h14a7 7 0 00-7-7z" />
                  </svg>
                  {#if !isNarrowSidebar}
                    <span class="text-[10px] font-mono text-amber-400 mt-1 font-bold tracking-wider">SNAPSHOT</span>
                  {/if}
                </div>
              {/if}
              <div class="absolute bottom-1 right-1 px-1.5 py-0.5 rounded bg-black/90 text-[9px] font-mono font-black {
                spoofIsConfirmed ? 'text-rose-300 border-rose-500/60'
                : (latestPerson.status === 'SPOOF' ? 'text-amber-300 border-amber-500/60'
                : (latestPerson.status === 'KNOWN' ? 'text-emerald-300 border-emerald-500/60' : 'text-slate-400 border-slate-700'))
              } border shadow-md">
                {latestPerson.status}
              </div>
            </div>

            <!-- Identity Info (Right - Adaptive Text & Badges) -->
            <div class="flex-1 min-w-0 flex flex-col justify-center gap-1 sm:gap-1.5 overflow-hidden">
              
              <!-- Status Badge -->
              <div class="min-w-0">
                <span class="inline-flex items-center gap-1 px-2 py-0.5 rounded-md text-[10px] sm:text-xs font-black font-mono tracking-wider shadow-sm truncate max-w-full {
                  spoofIsConfirmed
                    ? 'bg-rose-600 text-white shadow-[0_0_10px_rgba(244,63,94,0.45)]'
                    : (latestPerson.status === 'SPOOF'
                        ? 'bg-amber-400 text-slate-950 shadow-[0_0_10px_rgba(245,158,11,0.4)]'
                        : (latestPerson.status === 'KNOWN'
                            ? 'bg-emerald-400 text-slate-950 shadow-[0_0_10px_rgba(16,185,129,0.4)]'
                            : 'bg-[#141822] text-slate-400 border border-[#283348]'))
                }">
                  <span class="truncate">{latestPerson.status === 'SPOOF' ? '⚠️ SPOOF_DETECTED' : (latestPerson.status === 'KNOWN' ? '✓ IDENTIFIED' : 'SCANNING...')}</span>
                </span>
              </div>

              <!-- Full Person Name -->
              <h2 class="{isNarrowSidebar ? 'text-sm' : (isMediumSidebar ? 'text-base sm:text-lg' : 'text-lg sm:text-xl')} font-black {
                spoofIsConfirmed ? 'text-rose-300'
                : (latestPerson.status === 'SPOOF' ? 'text-amber-300' : 'text-white')
              } tracking-wide font-sans leading-snug drop-shadow truncate min-w-0">
                {(latestPerson.status === 'SPOOF')
                  ? (latestPerson.attack_type?.replace('_', ' ') ?? 'FAKE FACE')
                  : latestPerson.name}
              </h2>

              <!-- ID & Role Badges (Adaptive Flex/Wrap) -->
              <div class="flex {isNarrowSidebar ? 'flex-col gap-1' : 'flex-wrap gap-1.5'} min-w-0">
                {#if latestPerson.student_id && latestPerson.student_id !== '-'}
                  <span class="text-[10px] sm:text-xs font-black font-mono text-amber-300 bg-amber-950/90 px-2 py-0.5 rounded border border-amber-500/50 shadow-sm truncate max-w-full inline-block">
                    ID: {latestPerson.student_id}
                  </span>
                {/if}
                {#if latestPerson.role && latestPerson.role !== '-'}
                  <span class="text-[10px] sm:text-xs font-bold text-slate-100 bg-[#162032] px-2 py-0.5 rounded border border-[#283b5c] font-sans shadow-sm truncate max-w-full inline-block">
                    {latestPerson.role}
                  </span>
                {/if}
              </div>

              <!-- Gauges for Narrow & Medium screens (Inline inside identity info) -->
              {#if !isWideSidebar}
                <div class="grid grid-cols-2 gap-1 font-mono pt-1 min-w-0">
                  <div class="bg-[#05080e] px-1.5 py-0.5 rounded border border-[#283348] flex items-center justify-between gap-1 min-w-0">
                    <span class="text-slate-400 font-bold text-[9px] truncate">MATCH</span>
                    <span class="font-black text-[10px] sm:text-xs shrink-0 {latestPerson.status === 'KNOWN' ? 'text-emerald-400' : 'text-amber-400'}">
                      {(latestPerson.confidence * 100).toFixed(1)}%
                    </span>
                  </div>

                  <div class="bg-[#05080e] px-1.5 py-0.5 rounded border border-[#283348] flex items-center justify-between gap-1 min-w-0">
                    <span class="text-slate-400 font-bold text-[9px] truncate">LAT</span>
                    <span class="font-black text-[10px] sm:text-xs shrink-0 text-amber-300">
                      {Math.round(latestPerson.latency_ms || inferenceTime)}ms
                    </span>
                  </div>
                </div>
              {/if}

            </div>
          </div>

          <!-- Gauges for Wide Sidebar (Spanning Full Width at Bottom) -->
          {#if isWideSidebar}
            <div class="pt-1.5 mt-1 border-t border-[#1e2638] shrink-0 min-w-0">
              <div class="grid grid-cols-2 gap-2 font-mono">
                <div class="bg-[#05080e] px-2.5 py-1 rounded-lg border border-[#283348] flex items-center justify-between gap-1 shadow-inner min-w-0">
                  <span class="text-slate-400 font-bold text-xs flex items-center gap-1 shrink-0">
                    <span class="w-1.5 h-1.5 rounded-full {latestPerson.status === 'KNOWN' ? 'bg-emerald-400 shadow-[0_0_6px_#10b981]' : 'bg-amber-400'}"></span>
                    <span>CONFIDENCE</span>
                  </span>
                  <span class="font-black text-xs sm:text-sm whitespace-nowrap {latestPerson.status === 'KNOWN' ? 'text-emerald-400 drop-shadow-[0_0_8px_rgba(16,185,129,0.4)]' : 'text-amber-400'}">
                    {(latestPerson.confidence * 100).toFixed(1)}%
                  </span>
                </div>

                <div class="bg-[#05080e] px-2.5 py-1 rounded-lg border border-[#283348] flex items-center justify-between gap-1 shadow-inner min-w-0">
                  <span class="text-slate-400 font-bold text-xs flex items-center gap-1 shrink-0">
                    <span class="w-1.5 h-1.5 rounded-full bg-amber-400 shadow-[0_0_6px_#f59e0b]"></span>
                    <span>LATENCY</span>
                  </span>
                  <span class="font-black text-xs sm:text-sm whitespace-nowrap text-amber-300 drop-shadow-[0_0_8px_rgba(245,158,11,0.4)]">
                    {Math.round(latestPerson.latency_ms || inferenceTime)}ms
                  </span>
                </div>
              </div>
            </div>
          {/if}

        </div>
      </div>

      <!-- 2. RECENT RECOGNITION HISTORY (Fluid Flex Allocation) -->
      <div class="chassis-panel p-2.5 sm:p-3.5 rounded-2xl flex-1 min-h-0 flex flex-col justify-between overflow-hidden relative">
        <div class="absolute top-1.5 left-2 screw-marker">+</div>
        <div class="absolute top-1.5 right-2 screw-marker">+</div>

        <!-- Header -->
        <div class="flex items-center justify-between gap-1.5 shrink-0 pb-1.5 border-b border-[#283348] min-w-0">
          <div class="flex items-center gap-1.5 min-w-0 truncate">
            <span class="w-2 h-2 rounded-full bg-amber-400 animate-pulse shrink-0"></span>
            <h3 class="text-xs font-black uppercase tracking-wider text-amber-300 font-mono flex items-center gap-1 min-w-0 truncate">
              <span class="truncate">{isNarrowSidebar ? '>> LOGS' : '>> EVENT_STREAM'}</span>
            </h3>
          </div>
          
          <div class="flex items-center gap-1.5 shrink-0">
            <!-- Mode Selector -->
            <div class="inline-flex rounded-lg bg-[#05080e] p-0.5 border border-[#283348]" role="group" aria-label="โหมดบันทึกอีเวนต์">
              <button
                type="button"
                onclick={() => setEventLogMode('continuous')}
                class="px-1.5 py-0.5 text-[9px] sm:text-[10px] font-mono font-bold rounded transition cursor-pointer flex items-center gap-1 {eventLogMode === 'continuous' ? 'bg-amber-500/20 text-amber-300 border border-amber-500/50 shadow-sm' : 'text-slate-400 hover:text-slate-200 border border-transparent'}"
                title="จับอีเวนต์เรื่อยๆ (Continuous Mode)"
              >
                <span class="w-1.5 h-1.5 rounded-full {eventLogMode === 'continuous' ? 'bg-amber-400 animate-pulse' : 'bg-slate-600'}"></span>
                <span>{isNarrowSidebar ? 'Auto' : 'เรื่อยๆ'}</span>
              </button>
              <button
                type="button"
                onclick={() => setEventLogMode('once')}
                class="px-1.5 py-0.5 text-[9px] sm:text-[10px] font-mono font-bold rounded transition cursor-pointer flex items-center gap-1 {eventLogMode === 'once' ? 'bg-cyan-500/20 text-cyan-300 border border-cyan-500/50 shadow-sm' : 'text-slate-400 hover:text-slate-200 border border-transparent'}"
                title="บันทึกแค่คนละ 1 ครั้ง (Once per person)"
              >
                <span class="w-1.5 h-1.5 rounded-full {eventLogMode === 'once' ? 'bg-cyan-400 animate-pulse' : 'bg-slate-600'}"></span>
                <span>{isNarrowSidebar ? '1x' : '1 ครั้ง'}</span>
              </button>
            </div>

            <span class="text-[10px] sm:text-xs font-mono font-bold text-amber-300 bg-amber-950/80 px-2 py-0.5 rounded border border-amber-500/40 shrink-0">
              {historyList.length}
            </span>
            {#if historyList.length > 0}
              <button
                onclick={clearHistory}
                class="text-[10px] sm:text-xs font-mono font-bold text-rose-300 hover:text-white px-2 py-0.5 rounded border border-rose-500/50 hover:bg-rose-900/60 transition cursor-pointer bg-rose-950/40 active:scale-95 shrink-0"
              >
                CLR
              </button>
            {/if}
          </div>
        </div>

        <!-- History Terminal List: Adaptive Thumbnails & Typography -->
        <div class="space-y-1.5 flex-1 min-h-0 overflow-y-auto pr-1 pt-1.5 custom-scrollbar">
          {#if historyList.length === 0}
            <div class="h-full flex flex-col items-center justify-center text-slate-500 text-xs font-mono py-8 space-y-2">
              <svg class="w-8 h-8 text-slate-700" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.5" d="M12 8v4l3 3m6-3a9 9 0 11-18 0 9 9 0 0118 0z" />
              </svg>
              <span>&gt;&gt; AWAITING RECOGNITION...</span>
            </div>
          {:else}
            {#each historyList as item, idx (item.id)}
              <div class="bg-[#05080e] p-2 sm:p-2.5 rounded-xl border {
                item.status === 'SPOOF'
                  ? 'border-rose-500/70 shadow-[0_0_10px_rgba(244,63,94,0.25)]'
                  : (idx === 0 ? 'border-amber-400 shadow-[0_0_12px_rgba(245,158,11,0.25)] bg-amber-950/20' : 'border-[#283348] hover:border-amber-500/50')
              } flex items-center justify-between gap-2 sm:gap-3 transition-all min-w-0">
                
                <div class="flex items-center gap-2 sm:gap-2.5 min-w-0 flex-1">
                  <!-- Adaptive Thumbnail -->
                  <div class="{isNarrowSidebar ? 'w-9 h-9' : 'w-11 h-11 sm:w-12 sm:h-12'} rounded-lg overflow-hidden bg-black border-2 {item.status === 'KNOWN' ? 'border-emerald-400' : 'border-rose-500'} shrink-0 shadow-md">
                    {#if item.image}
                      <img src={item.image} alt={item.name} class="w-full h-full object-cover" />
                    {:else}
                      <div class="w-full h-full flex items-center justify-center text-slate-600 font-mono text-[9px]">NO_IMG</div>
                    {/if}
                  </div>

                  <!-- Name, ID & Telemetry in compact lines -->
                  <div class="min-w-0 flex-1 space-y-0.5">
                    <div class="flex items-center gap-1.5 min-w-0">
                      <span class="{isNarrowSidebar ? 'text-xs' : 'text-sm sm:text-base'} font-black {item.status === 'KNOWN' ? 'text-white' : 'text-rose-300'} truncate font-sans">
                        {item.name}
                      </span>
                      {#if item.student_id && item.student_id !== '-'}
                        <span class="text-[9px] sm:text-[10px] font-mono font-bold text-amber-300 bg-amber-950/90 px-1.5 py-0.5 rounded border border-amber-500/40 shrink-0">
                          {item.student_id}
                        </span>
                      {/if}
                    </div>

                    <!-- Telemetry in single line (truncated cleanly) -->
                    <div class="text-[10px] font-mono text-slate-400 flex items-center gap-1.5 whitespace-nowrap overflow-hidden text-ellipsis min-w-0">
                      <span class="text-slate-300 font-medium">{item.timestamp}</span>
                      <span class="text-slate-600">•</span>
                      <span class="text-emerald-400 font-bold">{(item.confidence * 100).toFixed(0)}%</span>
                      <span class="text-slate-600">•</span>
                      <span class="text-amber-400 font-bold">{item.latency_ms}ms</span>
                    </div>
                  </div>
                </div>

                <!-- Status Badge -->
                <div class="shrink-0 font-mono font-black text-[10px] sm:text-xs px-2 py-0.5 rounded border {
                  item.status === 'KNOWN' 
                    ? 'text-emerald-300 bg-emerald-950/80 border-emerald-500/50 shadow-[0_0_8px_rgba(16,185,129,0.2)]' 
                    : 'text-rose-300 bg-rose-950/80 border-rose-500/50 shadow-[0_0_8px_rgba(244,63,94,0.2)]'
                }">
                  {item.status}
                </div>
              </div>
            {/each}
          {/if}
        </div>

      </div>

    </div>
  </div>
</div>

<!-- ══════════════════════════════════════════════════════════════════════════
     ADMIN PASSWORD CONFIRMATION MODAL
══════════════════════════════════════════════════════════════════════════ -->
{#if isAntiSpoofModalOpen}
  <div class="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/85 backdrop-blur-md animate-fade-in font-mono">
    <div class="relative w-full max-w-md chassis-panel border-2 border-amber-500/60 rounded-2xl p-5 sm:p-6 shadow-[0_0_50px_rgba(245,158,11,0.25)] space-y-4">
      <div class="absolute top-1.5 left-2 screw-marker">+</div>
      <div class="absolute top-1.5 right-2 screw-marker">+</div>

      <!-- Header -->
      <div class="flex items-start justify-between gap-3">
        <div class="flex items-center gap-3">
          <div class="w-10 h-10 rounded-xl bg-amber-500/20 border border-amber-500/40 flex items-center justify-center text-amber-400 shrink-0 shadow-[0_0_15px_rgba(245,158,11,0.2)]">
            <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 15v2m-6 4h12a2 2 0 002-2v-6a2 2 0 00-2-2H6a2 2 0 00-2 2v6a2 2 0 002 2zm10-10V7a4 4 0 00-8 0v4h8z" />
            </svg>
          </div>
          <div>
            <h3 class="text-xs sm:text-sm font-black text-white tracking-wide uppercase">&gt;&gt; ADMIN_AUTHORIZATION</h3>
            <p class="text-[11px] text-amber-300 font-bold">
              สลับโหมดป้องกันการปลอมแปลง (ANTI-SPOOF)
            </p>
          </div>
        </div>

        <button
          onclick={closeAntiSpoofModal}
          aria-label="ปิดหน้าต่าง"
          class="text-slate-400 hover:text-white p-1 rounded-lg bg-slate-900/60 border border-[#283348] hover:border-slate-500 transition cursor-pointer"
        >
          <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
          </svg>
        </button>
      </div>

      <div class="text-xs text-slate-300 bg-[#05080e] p-3 rounded-xl border border-[#283348] space-y-1 leading-relaxed">
        <p class="text-[11px] text-slate-400 font-sans">
          กรุณากรอกรหัสผ่านผู้ดูแลระบบ (Admin Password) เพื่อยืนยันการเปลี่ยนสถานะโหมดป้องกันการปลอมแปลงใบหน้า
        </p>
      </div>

      <form onsubmit={handleConfirmAntiSpoofToggle} class="space-y-3.5 pt-1">
        <div>
          <label for="admin-pwd" class="block text-[11px] font-bold text-amber-400 uppercase mb-1">
            ACCESS_KEY &gt;
          </label>
          <div class="relative">
            <input
              id="admin-pwd"
              type={showModalPassword ? 'text' : 'password'}
              bind:value={adminPasswordInput}
              placeholder="••••••••"
              class="w-full terminal-input rounded-xl px-3.5 py-2 text-xs focus:outline-none focus:ring-1 pr-10 font-mono transition"
              autocomplete="current-password"
            />
            <button
              type="button"
              onclick={() => (showModalPassword = !showModalPassword)}
              class="absolute right-2.5 top-1/2 -translate-y-1/2 text-amber-400 hover:text-amber-200 text-[10px] font-bold"
            >
              {showModalPassword ? '[HIDE]' : '[SHOW]'}
            </button>
          </div>

          {#if passwordModalError}
            <p class="text-[11px] text-rose-400 mt-1.5 flex items-center gap-1 font-semibold">
              <svg class="w-3.5 h-3.5 shrink-0" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 8v4m0 4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" /></svg>
              <span>{passwordModalError}</span>
            </p>
          {/if}
        </div>

        <div class="flex items-center justify-end gap-2.5 pt-2">
          <button
            type="button"
            onclick={closeAntiSpoofModal}
            class="px-4 py-2 rounded-xl text-xs font-bold text-slate-300 bg-slate-900 border border-[#283348] hover:border-slate-600 hover:text-white transition cursor-pointer uppercase"
          >
            CANCEL
          </button>
          <button
            type="submit"
            disabled={isTogglingAntiSpoof}
            class="px-5 py-2 rounded-xl text-xs font-black text-slate-950 bg-gradient-to-r from-amber-500 via-amber-400 to-yellow-500 hover:from-amber-400 hover:to-yellow-400 active:scale-95 transition shadow-[0_0_15px_rgba(245,158,11,0.35)] disabled:opacity-50 flex items-center gap-1.5 cursor-pointer uppercase"
          >
            {#if isTogglingAntiSpoof}
              <div class="w-3.5 h-3.5 border-2 border-slate-950 border-t-transparent rounded-full animate-spin"></div>
              <span>AUTHENTICATING...</span>
            {:else}
              <span>CONFIRM_OVERRIDE</span>
            {/if}
          </button>
        </div>
      </form>
    </div>
  </div>
{/if}

<style>
  .custom-scrollbar::-webkit-scrollbar { width: 5px; }
  .custom-scrollbar::-webkit-scrollbar-track { background: #05080e; border-radius: 4px; }
  .custom-scrollbar::-webkit-scrollbar-thumb { background: #283348; border-radius: 4px; }
  .custom-scrollbar::-webkit-scrollbar-thumb:hover { background: #f59e0b; }
</style>