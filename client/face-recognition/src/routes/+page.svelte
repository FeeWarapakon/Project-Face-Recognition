<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { goto } from '$app/navigation';
  import { PUBLIC_ADMIN_USERNAME, PUBLIC_ADMIN_PASSWORD } from '$env/static/public';

  // Environment credentials
  const EXPECTED_USER = PUBLIC_ADMIN_USERNAME || 'admin';
  const EXPECTED_PASS = PUBLIC_ADMIN_PASSWORD || 'admin@staff';

  // CRT Terminal & Power States
  // bootStage:
  // 0 = Powered OFF
  // 1 = Stage 1: Running Program / System Boot Diagnostics
  // 2 = Stage 2: Smooth Block Progress Bar (Loads up to 78%)
  // 3 = Stage 2.5: Glitch / Error System Override Alert
  // 4 = Stage 3: Cyberpunk Admin Login Form
  // 5 = Stage 4: Access Granted // Cinematic Warp Entrance to Dashboard
  let isPowered = $state(false);
  let bootStage = $state(0);
  let litTubesCount = $state(0);
  let progressPercent = $state(0);
  let isAuthenticating = $state(false);

  // Form States
  let usernameInput = $state('');
  let passwordInput = $state('');
  let showPassword = $state(false);
  let loginError = $state('');
  let usernameInputRef: HTMLInputElement | null = $state(null);

  // Timers
  let sequenceTimers: number[] = [];
  let progressInterval: number | null = null;

  // Web Audio Context for realistic Retro Mechanical Toggle, CRT hum, Beeps, and Warp Chime
  let audioCtx: AudioContext | null = null;

  function initAudio() {
    if (!audioCtx && typeof window !== 'undefined') {
      const AudioContextClass = window.AudioContext || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
      if (AudioContextClass) {
        audioCtx = new AudioContextClass();
      }
    }
    if (audioCtx && audioCtx.state === 'suspended') {
      audioCtx.resume().catch(() => {});
    }
  }

  function playKeyClickSound() {
    initAudio();
    if (!audioCtx) return;
    try {
      const now = audioCtx.currentTime;
      const osc = audioCtx.createOscillator();
      const gain = audioCtx.createGain();
      osc.type = 'triangle';
      osc.frequency.setValueAtTime(1200 + Math.random() * 300, now);
      osc.frequency.exponentialRampToValueAtTime(300, now + 0.02);
      gain.gain.setValueAtTime(0.06, now);
      gain.gain.exponentialRampToValueAtTime(0.001, now + 0.02);
      osc.connect(gain);
      gain.connect(audioCtx.destination);
      osc.start(now);
      osc.stop(now + 0.025);
    } catch (e) {}
  }

  function playBeep(freq = 880, duration = 0.08, type: OscillatorType = 'sine', volume = 0.1) {
    initAudio();
    if (!audioCtx) return;
    try {
      const now = audioCtx.currentTime;
      const osc = audioCtx.createOscillator();
      const gain = audioCtx.createGain();
      osc.type = type;
      osc.frequency.setValueAtTime(freq, now);
      gain.gain.setValueAtTime(volume, now);
      gain.gain.exponentialRampToValueAtTime(0.001, now + duration);
      osc.connect(gain);
      gain.connect(audioCtx.destination);
      osc.start(now);
      osc.stop(now + duration + 0.01);
    } catch (e) {}
  }

  function playGlitchAlertSound() {
    initAudio();
    if (!audioCtx) return;
    try {
      const now = audioCtx.currentTime;
      const osc = audioCtx.createOscillator();
      const gain = audioCtx.createGain();
      osc.type = 'sawtooth';
      osc.frequency.setValueAtTime(240, now);
      osc.frequency.setValueAtTime(180, now + 0.08);
      osc.frequency.setValueAtTime(320, now + 0.16);
      gain.gain.setValueAtTime(0.2, now);
      gain.gain.exponentialRampToValueAtTime(0.001, now + 0.3);
      osc.connect(gain);
      gain.connect(audioCtx.destination);
      osc.start(now);
      osc.stop(now + 0.32);
    } catch (e) {}
  }

  function playAccessGrantedSound() {
    initAudio();
    if (!audioCtx) return;
    try {
      const now = audioCtx.currentTime;

      // 1. Futuristic 4-Note Major Cyber Arpeggio (C5 -> E5 -> G5 -> C6)
      const freqs = [523.25, 659.25, 783.99, 1046.5];
      freqs.forEach((freq, idx) => {
        const osc = audioCtx!.createOscillator();
        const gain = audioCtx!.createGain();
        const noteTime = now + idx * 0.08;
        osc.type = 'sine';
        osc.frequency.setValueAtTime(freq, noteTime);
        gain.gain.setValueAtTime(0.12, noteTime);
        gain.gain.exponentialRampToValueAtTime(0.001, noteTime + 0.35);
        osc.connect(gain);
        gain.connect(audioCtx!.destination);
        osc.start(noteTime);
        osc.stop(noteTime + 0.36);
      });

      // 2. Sub Bass Impact Boom
      const subOsc = audioCtx.createOscillator();
      const subGain = audioCtx.createGain();
      subOsc.type = 'triangle';
      subOsc.frequency.setValueAtTime(120, now);
      subOsc.frequency.exponentialRampToValueAtTime(35, now + 0.6);
      subGain.gain.setValueAtTime(0.35, now);
      subGain.gain.exponentialRampToValueAtTime(0.001, now + 0.65);
      subOsc.connect(subGain);
      subGain.connect(audioCtx.destination);
      subOsc.start(now);
      subOsc.stop(now + 0.7);

      // 3. High-Tech Cyber Warp Swoosh
      const warpOsc = audioCtx.createOscillator();
      const warpGain = audioCtx.createGain();
      warpOsc.type = 'sawtooth';
      warpOsc.frequency.setValueAtTime(400, now + 0.3);
      warpOsc.frequency.exponentialRampToValueAtTime(2400, now + 0.9);
      warpGain.gain.setValueAtTime(0.001, now + 0.3);
      warpGain.gain.linearRampToValueAtTime(0.08, now + 0.6);
      warpGain.gain.exponentialRampToValueAtTime(0.001, now + 0.95);
      warpOsc.connect(warpGain);
      warpGain.connect(audioCtx.destination);
      warpOsc.start(now + 0.3);
      warpOsc.stop(now + 1.0);
    } catch (e) {}
  }

  function playPowerSwitchSound(turningOn: boolean) {
    initAudio();
    if (!audioCtx) return;

    try {
      const now = audioCtx.currentTime;

      // 1. Heavy Mechanical Switch Snap Click
      const osc1 = audioCtx.createOscillator();
      const gain1 = audioCtx.createGain();
      osc1.type = 'triangle';
      osc1.frequency.setValueAtTime(turningOn ? 460 : 300, now);
      osc1.frequency.exponentialRampToValueAtTime(70, now + 0.05);

      gain1.gain.setValueAtTime(0.4, now);
      gain1.gain.exponentialRampToValueAtTime(0.001, now + 0.05);

      osc1.connect(gain1);
      gain1.connect(audioCtx.destination);
      osc1.start(now);
      osc1.stop(now + 0.06);

      if (turningOn) {
        // 2. High-Voltage CRT Capacitor Hum
        const osc2 = audioCtx.createOscillator();
        const gain2 = audioCtx.createGain();
        osc2.type = 'sawtooth';
        osc2.frequency.setValueAtTime(80, now + 0.04);
        osc2.frequency.exponentialRampToValueAtTime(2200, now + 0.28);

        gain2.gain.setValueAtTime(0.001, now + 0.04);
        gain2.gain.linearRampToValueAtTime(0.1, now + 0.14);
        gain2.gain.exponentialRampToValueAtTime(0.001, now + 0.35);

        osc2.connect(gain2);
        gain2.connect(audioCtx.destination);
        osc2.start(now + 0.04);
        osc2.stop(now + 0.38);

        // 3. Crisp BIOS Post Beep
        setTimeout(() => {
          playBeep(1080, 0.1, 'sine', 0.15);
        }, 220);
      }
    } catch (e) {}
  }

  function clearAllSequences() {
    sequenceTimers.forEach(t => clearTimeout(t));
    sequenceTimers = [];
    if (progressInterval) {
      clearInterval(progressInterval);
      progressInterval = null;
    }
  }

  function startBootSequence() {
    clearAllSequences();
    progressPercent = 0;
    
    // Step 1: Running Program Stream (Tube 1 lit)
    bootStage = 1;
    litTubesCount = 1;
    playBeep(640, 0.06, 'square', 0.08);

    // Step 2: Smooth Block Progress Bar (Tube 2 lit) after 1.4s
    const t1 = window.setTimeout(() => {
      bootStage = 2;
      litTubesCount = 2;
      playBeep(880, 0.05, 'sine', 0.08);

      // Smooth progress bar up to 78%
      let currentP = 0;
      progressInterval = window.setInterval(() => {
        if (currentP < 78) {
          const step = Math.min(3, 78 - currentP);
          currentP += step;
          progressPercent = currentP;
          if (currentP % 12 === 0) {
            playBeep(440 + currentP * 6, 0.02, 'triangle', 0.04);
          }
        } else {
          if (progressInterval) {
            clearInterval(progressInterval);
            progressInterval = null;
          }

          // Step 3: Trigger Glitch / Error System Override
          const t2 = window.setTimeout(() => {
            bootStage = 3;
            playGlitchAlertSound();

            // Step 4: Final Cyber Admin Login Form (Tube 3 lit - All tubes on!)
            const t3 = window.setTimeout(() => {
              bootStage = 4;
              litTubesCount = 3;
              playBeep(1200, 0.12, 'sine', 0.12);
              setTimeout(() => {
                if (usernameInputRef) usernameInputRef.focus();
              }, 150);
            }, 1200);
            sequenceTimers.push(t3);

          }, 400);
          sequenceTimers.push(t2);
        }
      }, 35);

    }, 1400);
    sequenceTimers.push(t1);
  }

  function togglePowerSwitch() {
    const nextState = !isPowered;
    playPowerSwitchSound(nextState);
    isPowered = nextState;

    if (nextState) {
      startBootSequence();
    } else {
      clearAllSequences();
      bootStage = 0;
      litTubesCount = 0;
      progressPercent = 0;
    }
  }

  function play8BitWarpSound() {
    initAudio();
    if (!audioCtx) return;
    try {
      const now = audioCtx.currentTime;

      // 1. Classic 8-Bit Chiptune Fast Arpeggio (Square Wave Matrix Bleeps)
      const chiptuneNotes = [261.63, 329.63, 392.00, 523.25, 659.25, 783.99, 1046.50, 1318.51, 1567.98, 2093.00];
      chiptuneNotes.forEach((freq, idx) => {
        const osc = audioCtx!.createOscillator();
        const gain = audioCtx!.createGain();
        const noteTime = now + idx * 0.04;

        osc.type = 'square';
        osc.frequency.setValueAtTime(freq, noteTime);

        gain.gain.setValueAtTime(0.07, noteTime);
        gain.gain.exponentialRampToValueAtTime(0.001, noteTime + 0.07);

        osc.connect(gain);
        gain.connect(audioCtx!.destination);

        osc.start(noteTime);
        osc.stop(noteTime + 0.08);
      });

      // 2. Retro 8-bit Digital Crunch Swell
      const bufferSize = audioCtx.sampleRate * 0.48;
      const buffer = audioCtx.createBuffer(1, bufferSize, audioCtx.sampleRate);
      const data = buffer.getChannelData(0);
      for (let i = 0; i < bufferSize; i++) {
        data[i] = (Math.random() > 0.5 ? 1 : -1) * 0.12 * (i / bufferSize);
      }
      const noise = audioCtx.createBufferSource();
      noise.buffer = buffer;
      const filter = audioCtx.createBiquadFilter();
      filter.type = 'bandpass';
      filter.frequency.setValueAtTime(600, now);
      filter.frequency.exponentialRampToValueAtTime(3200, now + 0.42);
      filter.Q.value = 2.5;

      const gainNoise = audioCtx.createGain();
      gainNoise.gain.setValueAtTime(0.02, now);
      gainNoise.gain.linearRampToValueAtTime(0.18, now + 0.35);
      gainNoise.gain.exponentialRampToValueAtTime(0.001, now + 0.48);

      noise.connect(filter);
      filter.connect(gainNoise);
      gainNoise.connect(audioCtx.destination);
      noise.start(now);
    } catch (e) {}
  }

  let isDivingIntoScreen = $state(false);

  function handleLogin(e?: Event) {
    if (e) e.preventDefault();
    loginError = '';

    if (!usernameInput.trim() || !passwordInput.trim()) {
      loginError = 'กรุณากรอก Username และ Password ให้ครบถ้วน';
      return;
    }

    isAuthenticating = true;

    setTimeout(() => {
      if (usernameInput.trim() === EXPECTED_USER && passwordInput.trim() === EXPECTED_PASS) {
        // Step 1: Display ACCESS GRANTED clearly on CRT screen
        bootStage = 5; 
        playAccessGrantedSound();

        // Step 2: Allow user to comfortably see ACCESS GRANTED (500ms), then gently & slowly zoom into the CRT screen
        setTimeout(() => {
          isDivingIntoScreen = true;
          play8BitWarpSound();
        }, 500);

        try {
          sessionStorage.setItem('face_auth', 'true');
          sessionStorage.setItem('user', usernameInput.trim());
          sessionStorage.setItem('trigger_dive_screen', 'true');
          sessionStorage.setItem('trigger_transition', 'true');
        } catch (e) {}

        // Step 3: Seamlessly navigate to dashboard as camera calmly glides into CRT center
        setTimeout(() => {
          isAuthenticating = false;
          goto('/face-recognition');
        }, 1750);
      } else {
        isAuthenticating = false;
        loginError = 'Username หรือ Password ไม่ถูกต้อง กรุณาลองใหม่อีกครั้ง';
      }
    }, 150);
  }

  onMount(() => {
    const unlock = () => {
      initAudio();
      window.removeEventListener('click', unlock);
    };
    window.addEventListener('click', unlock);
    return () => {
      window.removeEventListener('click', unlock);
    };
  });

  onDestroy(() => {
    clearAllSequences();
  });
</script>

<svelte:head>
  <title>CYBER TERMINAL // MOBILEFACENET AI</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin="anonymous">
  <link href="https://fonts.googleapis.com/css2?family=Plus+Jakarta+Sans:wght@500;600;700;800;900&family=JetBrains+Mono:wght@400;500;600;700&family=Prompt:wght@400;500;600;700&display=swap" rel="stylesheet">
</svelte:head>

<!-- Full-Screen Zero-Scrollbar Cyber Laboratory (Always fits exact viewport 100vh) -->
<div 
  class="relative w-screen h-screen h-[100dvh] max-h-screen overflow-hidden select-none font-['Prompt',sans-serif] bg-[#050608] text-amber-400 flex flex-col items-center justify-between p-1.5 sm:p-3 md:p-4 box-border"
  style="transform-origin: center 46%; transform: {isDivingIntoScreen ? 'scale(3.8)' : 'scale(1)'}; opacity: {isDivingIntoScreen ? 0 : 1}; transition: {isDivingIntoScreen ? 'transform 1.25s cubic-bezier(0.22, 1, 0.36, 1), opacity 1.15s cubic-bezier(0.45, 0, 1, 1)' : 'none'}; will-change: {isDivingIntoScreen ? 'transform, opacity' : 'auto'};"
>

  <!-- Ambient Dark Laboratory Glow -->
  <div class="absolute inset-0 pointer-events-none z-0 bg-[radial-gradient(circle_at_center,_rgba(245,158,11,0.06),_transparent_75%)]"></div>

  <!-- ========================================================================= -->
  <!-- Top Industrial Header: Model Badge & Vacuum Tubes Array (Responsive 3-Col)-->
  <!-- ========================================================================= -->
  <header class="relative z-10 w-full max-w-5xl grid grid-cols-3 items-center px-1.5 sm:px-4 py-0.5 sm:py-1 border-b border-[#202533]/80 gap-1 sm:gap-2 shrink-0">
    
    <!-- Left Column: Model Branding -->
    <div class="flex items-center gap-1 sm:gap-2 min-w-0">
      <div class="w-1.5 sm:w-2.5 h-1.5 sm:h-2.5 rounded-full {isPowered ? 'bg-amber-400 shadow-[0_0_12px_#f59e0b]' : 'bg-slate-700'} transition-all shrink-0"></div>
      <div class="text-[9px] sm:text-xs font-['JetBrains_Mono',monospace] tracking-wider text-slate-400 font-bold uppercase truncate">
        TERMINAL-88 // <span class="text-amber-400 hidden xs:inline">MOBILEFACENET</span><span class="text-amber-400 xs:hidden">AI</span>
      </div>
    </div>

    <!-- Center Column: Vacuum Tubes Array (V1 -> V2 -> V3) -->
    <div class="flex items-center justify-center gap-2.5 sm:gap-5 md:gap-7 py-0.5">
      {#each [1, 2, 3] as tubeIdx}
        {@const isTubeLit = isPowered && tubeIdx <= litTubesCount}
        <div class="relative flex flex-col items-center">
          <!-- Glass Vacuum Tube -->
          <div class="relative w-4.5 sm:w-6.5 md:w-8 h-7 sm:h-11 md:h-13 rounded-t-full bg-gradient-to-b from-white/15 via-white/5 to-transparent border border-white/20 shadow-lg flex items-center justify-center overflow-hidden transition-all duration-700 {isTubeLit ? 'shadow-[0_0_22px_rgba(245,158,11,0.75)]' : 'opacity-30'} {bootStage === 5 ? 'shadow-[0_0_30px_#10b981]' : ''}">
            <!-- Glass Highlight Line -->
            <div class="absolute top-0.5 left-0.5 sm:top-1 sm:left-1.5 w-0.5 sm:w-1 h-3 sm:h-6 rounded-full bg-white/30 blur-[0.5px]"></div>
            
            <!-- Tungsten Filament Core -->
            <div class="relative w-1 sm:w-2 h-3 sm:h-5 md:h-7 flex flex-col items-center justify-between">
              <div class="w-1 sm:w-1.5 h-1 sm:h-1.5 rounded-full transition-all duration-700 {isTubeLit ? (bootStage === 5 ? 'bg-emerald-200 shadow-[0_0_12px_5px_#10b981] scale-125' : 'bg-[#ffedd5] shadow-[0_0_12px_5px_#f59e0b] scale-125') : 'bg-slate-600'}"></div>
              <div class="w-[1px] h-full transition-all duration-500 {isTubeLit ? (bootStage === 5 ? 'bg-emerald-300 shadow-[0_0_8px_#34d399]' : 'bg-[#fef08a] shadow-[0_0_8px_#f97316]') : 'bg-slate-700'}"></div>
              <div class="w-1 sm:w-1.5 h-1 sm:h-1.5 rounded-full transition-all duration-700 {isTubeLit ? (bootStage === 5 ? 'bg-emerald-200 shadow-[0_0_12px_5px_#10b981] scale-125' : 'bg-[#ffedd5] shadow-[0_0_12px_5px_#f59e0b] scale-125') : 'bg-slate-600'}"></div>
            </div>
          </div>
          <!-- Metal Socket -->
          <div class="w-5 sm:w-7.5 md:w-9 h-1.5 sm:h-2 bg-gradient-to-r from-[#475569] via-[#94a3b8] to-[#334155] rounded-b-sm border-t border-black shadow-sm flex items-center justify-center">
            <span class="text-[5px] sm:text-[6px] text-black font-extrabold tracking-tighter">V{tubeIdx}</span>
          </div>
        </div>
      {/each}
    </div>

    <!-- Right Column: Power Grid Telemetry -->
    <div class="flex items-center justify-end gap-1 sm:gap-2 text-[9px] sm:text-xs font-['JetBrains_Mono',monospace] text-slate-400">
      <span class="hidden sm:inline">GRID:</span>
      <span class="w-14 sm:w-24 text-right truncate {isPowered ? 'text-emerald-400 font-bold' : 'text-slate-600'}">
        {isPowered ? "240V" : "OFF"}
      </span>
    </div>
  </header>

  <!-- ========================================================================= -->
  <!-- Center Stage: Flexibly Sized CRT Monitor Frame & Power Deck               -->
  <!-- ========================================================================= -->
  <main class="relative z-10 flex-1 w-full max-w-5xl flex flex-col items-center justify-center min-h-0 py-1 sm:py-2 overflow-hidden">

    <!-- Heavy Industrial Chassis Frame: Enlarged for a prominent, expansive monitor view -->
    <div 
      class="relative w-full max-w-[890px] h-full max-h-[min(570px,70vh)] min-h-[280px] rounded-[20px] sm:rounded-[30px] md:rounded-[34px] bg-gradient-to-b from-[#1e2330] via-[#141822] to-[#0c0f16] p-2.5 sm:p-4 md:p-5 border-2 sm:border-4 border-[#2b3344] shadow-[0_20px_50px_rgba(0,0,0,0.95),inset_0_2px_4px_rgba(255,255,255,0.15)] flex flex-col items-center justify-between {bootStage === 5 ? 'shadow-[0_0_50px_rgba(16,185,129,0.3)]' : ''}"
    >
      
      <!-- Corner Chassis Screws -->
      <div class="absolute top-2 sm:top-3.5 left-2.5 sm:left-4 w-2 sm:w-2.5 h-2 sm:h-2.5 rounded-full bg-[#384256] border border-black flex items-center justify-center text-[6px] sm:text-[7px] text-black font-bold">+</div>
      <div class="absolute top-2 sm:top-3.5 right-2.5 sm:right-4 w-2 sm:w-2.5 h-2 sm:h-2.5 rounded-full bg-[#384256] border border-black flex items-center justify-center text-[6px] sm:text-[7px] text-black font-bold">+</div>
      <div class="absolute bottom-2 sm:bottom-3.5 left-2.5 sm:left-4 w-2 sm:w-2.5 h-2 sm:h-2.5 rounded-full bg-[#384256] border border-black flex items-center justify-center text-[6px] sm:text-[7px] text-black font-bold">+</div>
      <div class="absolute bottom-2 sm:bottom-3.5 right-2.5 sm:right-4 w-2 sm:w-2.5 h-2 sm:h-2.5 rounded-full bg-[#384256] border border-black flex items-center justify-center text-[6px] sm:text-[7px] text-black font-bold">+</div>

      <!-- Curved CRT Glass Tube Screen (Flexibly occupies inner space) -->
      <div class="relative w-full flex-1 min-h-0 rounded-[12px] sm:rounded-[18px] md:rounded-[22px] bg-[#020406] border-2 border-[#181d28] overflow-hidden flex flex-col items-center justify-between p-2.5 sm:p-4 md:p-5 shadow-[inset_0_0_65px_rgba(0,0,0,0.95)]">
        
        <!-- CRT Phosphor Scanline Filter -->
        <div class="absolute inset-0 pointer-events-none z-20 crt-scanlines opacity-75"></div>
        <div class="absolute inset-0 pointer-events-none z-20 crt-vignette {bootStage === 5 ? 'crt-vignette-emerald' : ''}"></div>

        <!-- Glass Reflection Glare -->
        <div class="absolute top-0 right-0 w-2/3 h-28 bg-gradient-to-b from-white/[0.035] to-transparent transform -skew-x-12 pointer-events-none z-20"></div>

        <!-- ----------------------------------------------------------------- -->
        <!-- SCREEN STAGE 0: POWER OFF                                         -->
        <!-- ----------------------------------------------------------------- -->
        {#if bootStage === 0}
          <div class="relative z-10 my-auto flex flex-col items-center text-center space-y-1.5 sm:space-y-3 text-slate-600 animate-fade-in px-2">
            <div class="w-9 sm:w-14 h-9 sm:h-14 rounded-full border-2 border-dashed border-slate-700 flex items-center justify-center">
              <svg class="w-4.5 sm:w-7 h-4.5 sm:h-7 text-slate-700" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.5" d="M13 10V3L4 14h7v7l9-11h-7z" />
              </svg>
            </div>
            
            <div class="space-y-0.5 sm:space-y-1">
              <div class="text-[11px] sm:text-base md:text-lg font-bold text-slate-500 font-['JetBrains_Mono',monospace] tracking-wider">
                [ TERMINAL OFFLINE // NO POWER ]
              </div>
              <p class="text-[10px] sm:text-xs text-slate-600 max-w-sm mx-auto">
                ระบบรักษาความปลอดภัยสแกนใบหน้ากำลังปิดอยู่ สับสวิตช์ Power ด้านล่างเพื่อเริ่มการทำงาน
              </p>
            </div>
          </div>

        <!-- ----------------------------------------------------------------- -->
        <!-- SCREEN STAGE 1: RUNNING PROGRAM / SYSTEM BOOT DIAGNOSTICS         -->
        <!-- ----------------------------------------------------------------- -->
        {:else if bootStage === 1}
          <div class="relative z-10 w-full h-full flex flex-col justify-between text-emerald-400 font-mono text-[9px] sm:text-xs md:text-sm animate-fade-in p-0.5 sm:p-1">
            <!-- Header -->
            <div class="flex items-center justify-between border-b border-emerald-500/30 pb-1 shrink-0">
              <div class="flex items-center gap-1.5 text-emerald-300 font-bold truncate">
                <span class="w-1.5 sm:w-2 h-1.5 sm:h-2 rounded-full bg-emerald-400 animate-ping shrink-0"></span>
                <span class="truncate">BOOT_SEQUENCE // V4L2_TENSOR_CORE</span>
              </div>
              <span class="text-[8px] sm:text-xs text-slate-500 shrink-0">STAGE 1/3</span>
            </div>

            <!-- Terminal Diagnostic Logs -->
            <div class="my-auto space-y-0.5 sm:space-y-1.5 py-1 text-emerald-400/90 leading-tight font-mono overflow-hidden text-[9px] sm:text-xs">
              <div class="flex items-center gap-1.5 truncate">
                <span class="text-slate-500 shrink-0">[0.012s]</span>
                <span class="text-emerald-300 font-bold shrink-0">&gt;&gt; HOST:</span>
                <span class="truncate">RASPBERRY PI 5 (ARM64)</span>
              </div>
              <div class="flex items-center gap-1.5 truncate">
                <span class="text-slate-500 shrink-0">[0.140s]</span>
                <span class="text-emerald-300 font-bold shrink-0">&gt;&gt; NPU ACCEL:</span>
                <span class="text-amber-300 truncate">INT8 QUANTIZED TENSOR ENGINE LOADED</span>
              </div>
              <div class="flex items-center gap-1.5 truncate">
                <span class="text-slate-500 shrink-0">[0.380s]</span>
                <span class="text-emerald-300 font-bold shrink-0">&gt;&gt; EMBEDDING:</span>
                <span class="truncate">MOBILEFACENET 512-D VECTOR SPACE</span>
              </div>
              <div class="flex items-center gap-1.5 truncate">
                <span class="text-slate-500 shrink-0">[0.620s]</span>
                <span class="text-emerald-300 font-bold shrink-0">&gt;&gt; ANTI-SPOOF:</span>
                <span class="truncate">MINIFASNET_V2 DUAL-SPECTRUM [ONLINE]</span>
              </div>
              <div class="flex items-center gap-1.5 truncate">
                <span class="text-slate-500 shrink-0">[0.950s]</span>
                <span class="text-emerald-300 font-bold shrink-0">&gt;&gt; CAMERA:</span>
                <span class="text-cyan-300 truncate">USB-V4L2 1080P SENSOR CALIBRATED</span>
              </div>
              <div class="flex items-center gap-1.5 animate-pulse text-amber-400 font-bold truncate">
                <span class="text-slate-500 shrink-0">[1.220s]</span>
                <span class="truncate">&gt;&gt; ALLOCATING MEMORY BUFFERS &amp; PREPARING...</span>
              </div>
            </div>

            <!-- Stage Footer -->
            <div class="flex items-center justify-between border-t border-emerald-500/20 pt-1 text-[8px] sm:text-xs text-slate-500 shrink-0">
              <span class="truncate">STATUS: INITIALIZING SUBSYSTEMS</span>
              <span class="text-emerald-400 animate-pulse shrink-0">EXECUTING...</span>
            </div>
          </div>

        <!-- ----------------------------------------------------------------- -->
        <!-- SCREEN STAGE 2: SMOOTH BLOCK PROGRESS BAR (0% -> ~78%)            -->
        <!-- ----------------------------------------------------------------- -->
        {:else if bootStage === 2}
          <div class="relative z-10 w-full h-full flex flex-col justify-between text-amber-400 font-mono animate-fade-in p-0.5 sm:p-1">
            <!-- Header -->
            <div class="flex items-center justify-between border-b border-amber-500/30 pb-1 shrink-0">
              <div class="flex items-center gap-1.5 text-amber-300 font-bold text-[9px] sm:text-xs md:text-sm truncate">
                <span class="w-1.5 sm:w-2 h-1.5 sm:h-2 rounded-full bg-amber-400 animate-pulse shrink-0"></span>
                <span class="truncate">RUNTIME LOADING // NEURAL PIPELINE</span>
              </div>
              <span class="text-[8px] sm:text-xs text-slate-500 shrink-0">STAGE 2/3</span>
            </div>

            <!-- Center Progress Box -->
            <div class="my-auto w-full max-w-lg mx-auto flex flex-col items-center space-y-2 sm:space-y-3.5 px-1">
              
              <div class="text-center space-y-0.5">
                <div class="text-[9px] sm:text-xs text-slate-400 tracking-wider">
                  MOUNTING MODEL WEIGHTS &amp; DATABASE VECTORS
                </div>
                <div class="text-base sm:text-2xl md:text-3xl font-black text-amber-300 tracking-tight font-mono">
                  {progressPercent}% <span class="text-[10px] sm:text-xs font-normal text-slate-500">COMPLETE</span>
                </div>
              </div>

              <!-- Retro Block Progress Bar Container -->
              <div class="w-full bg-[#0b0e14] border-2 border-amber-500/50 p-1 rounded-lg shadow-[0_0_20px_rgba(245,158,11,0.2)]">
                <!-- 25 Blocks Row -->
                <div class="grid grid-cols-25 gap-0.5 h-4 sm:h-6 md:h-7">
                  {#each Array(25) as _, bIdx}
                    {@const filled = (bIdx + 1) * 4 <= progressPercent}
                    <div class="h-full rounded-[1px] transition-all duration-150 {filled ? 'bg-amber-400 shadow-[0_0_8px_#f59e0b]' : 'bg-[#181d28] border border-white/5'}"></div>
                  {/each}
                </div>
              </div>

              <!-- Diagnostic Subtext -->
              <div class="w-full flex items-center justify-between text-[8px] sm:text-xs text-slate-400 px-0.5">
                <span>SECTORS: {(progressPercent * 24.5).toFixed(0)} / 2,450 KB</span>
                <span class="text-amber-400 animate-pulse">CHECKING CHECKSUM...</span>
              </div>

            </div>

            <!-- Stage Footer -->
            <div class="flex items-center justify-between border-t border-amber-500/20 pt-1 text-[8px] sm:text-xs text-slate-500 shrink-0">
              <span class="truncate">PIPELINE: MOBILEFACENET_INT8</span>
              <span class="text-amber-400 shrink-0">VERIFYING SIGNATURE</span>
            </div>
          </div>

        <!-- ----------------------------------------------------------------- -->
        <!-- SCREEN STAGE 2.5: SYSTEM OVERRIDE / GLITCH / ERROR SCREEN         -->
        <!-- ----------------------------------------------------------------- -->
        {:else if bootStage === 3}
          <div class="relative z-10 w-full h-full flex flex-col justify-between text-rose-400 font-mono animate-shake p-0.5 sm:p-1">
            <!-- Header -->
            <div class="flex items-center justify-between border-b border-rose-500/40 pb-1 bg-rose-950/30 px-2 rounded shrink-0">
              <div class="flex items-center gap-1.5 text-rose-300 font-bold text-[9px] sm:text-xs md:text-sm truncate">
                <span class="w-1.5 sm:w-2 h-1.5 sm:h-2 rounded-full bg-rose-500 animate-ping shrink-0"></span>
                <span class="truncate">[ ! ] SYSTEM OVERRIDE // EXCEPTION DETECTED</span>
              </div>
              <span class="text-[8px] sm:text-xs text-rose-400 font-bold shrink-0">[HALT @ {progressPercent}%]</span>
            </div>

            <!-- Error Warning Box -->
            <div class="my-auto w-full max-w-md mx-auto flex flex-col items-center text-center space-y-1.5 sm:space-y-2.5 bg-rose-950/40 border-2 border-rose-500/80 p-2.5 sm:p-4 rounded-xl shadow-[0_0_35px_rgba(244,63,94,0.35)]">
              <div class="w-8 sm:w-11 h-8 sm:h-11 rounded-full bg-rose-900/60 border border-rose-500 flex items-center justify-center">
                <svg class="w-4 sm:w-6 h-4 sm:h-6 text-rose-400 animate-pulse" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
                </svg>
              </div>

              <div class="space-y-0.5">
                <div class="text-[11px] sm:text-sm md:text-base font-black text-rose-300 tracking-wider">
                  SECURITY OVERRIDE TRIGGERED
                </div>
                <div class="text-[9px] sm:text-xs text-rose-200/90 font-sans">
                  ตรวจพบสิทธิ์ระดับ Root จำเป็นต้องยืนยันตัวตนเจ้าหน้าที่ผู้ดูแลระบบ
                </div>
              </div>

              <div class="text-[8px] sm:text-[10px] text-amber-300 font-mono bg-black/60 px-2 py-0.5 rounded border border-amber-500/40 animate-pulse">
                &gt;&gt; INITIATING MANUAL OPERATOR AUTH PROTOCOL...
              </div>
            </div>

            <!-- Stage Footer -->
            <div class="flex items-center justify-between border-t border-rose-500/30 pt-1 text-[8px] sm:text-xs text-rose-400/80 shrink-0">
              <span class="truncate">ERR_CODE: 0x88F2_AUTH_REQUIRED</span>
              <span class="animate-pulse shrink-0">REDIRECTING...</span>
            </div>
          </div>

        <!-- ----------------------------------------------------------------- -->
        <!-- SCREEN STAGE 3: FULL CYBERPUNK ADMIN LOGIN FORM                   -->
        <!-- ----------------------------------------------------------------- -->
        {:else if bootStage === 4}
          <div class="relative z-10 w-full h-full flex flex-col justify-between text-amber-400 space-y-1.5 sm:space-y-2.5 animate-fade-in">
            
            <!-- Terminal Header Bar -->
            <div class="flex items-center justify-between border-b border-amber-500/30 pb-1 shrink-0">
              <div class="flex items-center gap-1.5 min-w-0">
                <span class="inline-block w-1.5 sm:w-2 h-1.5 sm:h-2 rounded-full bg-amber-400 animate-pulse shrink-0"></span>
                <span class="text-[10px] sm:text-xs md:text-sm font-bold tracking-wider text-amber-300 font-['JetBrains_Mono',monospace] truncate">
                  CYBER TERMINAL // ADMIN AUTHENTICATION
                </span>
              </div>
              <span class="text-[8px] sm:text-xs text-emerald-400 font-['JetBrains_Mono',monospace] font-bold shrink-0">[NPU ONLINE]</span>
            </div>

            <!-- Error Banner -->
            {#if loginError}
              <div class="p-1 sm:p-1.5 rounded bg-rose-950/80 border border-rose-500 text-rose-300 text-[9px] sm:text-xs font-mono font-bold flex items-center gap-1.5 animate-shake shrink-0">
                <svg class="w-3.5 h-3.5 text-rose-400 shrink-0" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 8v4m0 4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
                </svg>
                <span class="truncate">{loginError}</span>
              </div>
            {/if}

            <!-- Terminal Login Form -->
            <form onsubmit={handleLogin} class="space-y-1.5 sm:space-y-2.5 my-auto max-w-md w-full mx-auto text-xs">
              <!-- Username -->
              <div class="space-y-0.5 sm:space-y-1">
                <label for="terminal-user" class="flex items-center gap-1 text-amber-300 font-mono text-[9px] sm:text-xs font-bold">
                  <span>ROOT_USER &gt;</span>
                </label>
                <div class="relative">
                  <input
                    id="terminal-user"
                    bind:this={usernameInputRef}
                    type="text"
                    bind:value={usernameInput}
                    onkeydown={playKeyClickSound}
                    placeholder="กรอกชื่อผู้ใช้ (Admin Username)"
                    class="w-full px-2.5 sm:px-3 py-1.5 sm:py-2.5 rounded-lg bg-black/90 border border-amber-500/60 focus:border-amber-300 focus:ring-1 focus:ring-amber-300 text-amber-300 font-mono text-xs outline-none placeholder-amber-700/60 shadow-[inset_0_0_12px_rgba(245,158,11,0.25)] font-['Prompt',sans-serif]"
                    autocomplete="username"
                    required
                  />
                </div>
              </div>

              <!-- Password -->
              <div class="space-y-0.5 sm:space-y-1">
                <label for="terminal-pass" class="flex items-center gap-1 text-amber-300 font-mono text-[9px] sm:text-xs font-bold">
                  <span>ACCESS_KEY &gt;</span>
                </label>
                <div class="relative">
                  <input
                    id="terminal-pass"
                    type={showPassword ? 'text' : 'password'}
                    bind:value={passwordInput}
                    onkeydown={playKeyClickSound}
                    placeholder="กรอกรหัสผ่าน (Admin Password)"
                    class="w-full pl-2.5 sm:pl-3 pr-14 sm:pr-20 py-1.5 sm:py-2.5 rounded-lg bg-black/90 border border-amber-500/60 focus:border-amber-300 focus:ring-1 focus:ring-amber-300 text-amber-300 font-mono text-xs outline-none placeholder-amber-700/60 shadow-[inset_0_0_12px_rgba(245,158,11,0.25)] font-['Prompt',sans-serif]"
                    autocomplete="current-password"
                    required
                  />
                  <button
                    type="button"
                    onclick={() => showPassword = !showPassword}
                    class="absolute right-2 top-1.5 sm:top-2 text-[9px] sm:text-xs text-amber-400 hover:text-amber-200 font-mono font-bold cursor-pointer"
                  >
                    {showPassword ? '[ซ่อน]' : '[ดูรหัส]'}
                  </button>
                </div>
              </div>

              <!-- Submit Execution Button -->
              <div class="pt-0.5 sm:pt-1">
                <button
                  type="submit"
                  disabled={isAuthenticating}
                  class="w-full py-2 sm:py-2.5 rounded-lg bg-gradient-to-r from-amber-500 via-amber-400 to-yellow-500 hover:from-amber-400 hover:to-yellow-400 active:scale-[0.99] text-slate-950 font-black text-[11px] sm:text-xs tracking-wider uppercase transition-all shadow-[0_0_20px_rgba(245,158,11,0.45)] cursor-pointer flex items-center justify-center gap-1.5"
                >
                  {#if isAuthenticating}
                    <svg class="w-3.5 h-3.5 animate-spin text-slate-950" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2.5" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
                    </svg>
                    <span>กำลังตรวจสอบสิทธิ์...</span>
                  {:else}
                    <span>ยืนยันเข้าสู่ระบบ (EXECUTE // LOGIN)</span>
                    <svg class="w-3.5 h-3.5 text-slate-950" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2.5" d="M13 7l5 5m0 0l-5 5m5-5H6" />
                    </svg>
                  {/if}
                </button>
              </div>
            </form>

            <!-- Bottom Console Prompt -->
            <div class="text-[9px] sm:text-xs text-slate-400 flex items-center justify-between border-t border-amber-500/20 pt-0.5 sm:pt-1 font-mono shrink-0">
              <span>STATUS: SYSTEM READY</span>
              <span class="text-amber-400">MOBILEFACENET • INT8</span>
            </div>

          </div>

        <!-- ----------------------------------------------------------------- -->
        <!-- SCREEN STAGE 5: CINEMATIC ACCESS GRANTED // WARP TO DASHBOARD     -->
        <!-- ----------------------------------------------------------------- -->
        {:else if bootStage === 5}
          <div class="relative z-10 w-full h-full flex flex-col justify-between text-emerald-400 font-mono animate-warp p-0.5 sm:p-2">
            
            <!-- High-Tech Header -->
            <div class="flex items-center justify-between border-b border-emerald-500/40 pb-0.5 sm:pb-1 bg-emerald-950/40 px-2 rounded shrink-0">
              <div class="flex items-center gap-1.5 text-emerald-300 font-bold text-[10px] sm:text-sm">
                <span class="w-2 sm:w-2.5 h-2 sm:h-2.5 rounded-full bg-emerald-400 animate-ping"></span>
                <span>AUTHENTICATION GRANTED // ROOT OPERATOR</span>
              </div>
              <span class="text-[9px] sm:text-xs text-emerald-300 font-bold hidden xs:inline">[SESSION ESTABLISHED]</span>
            </div>

            <!-- Central Holographic Access Badge & Matrix Stream -->
            <div class="my-auto w-full max-w-md mx-auto flex flex-col items-center text-center space-y-1.5 sm:space-y-3">
              
              <!-- Glowing Holographic Ripple Icon -->
              <div class="relative flex items-center justify-center">
                <div class="absolute -inset-3 sm:-inset-4 rounded-full bg-emerald-500/30 blur-xl animate-pulse"></div>
                <div class="w-11 sm:w-16 h-11 sm:h-16 rounded-full bg-emerald-950/90 border-2 border-emerald-400 flex items-center justify-center shadow-[0_0_35px_#10b981] animate-bounce-short">
                  <svg class="w-5 sm:w-8 h-5 sm:h-8 text-emerald-300" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2.5" d="M8 11V7a4 4 0 118 0m-4 8v2m-6 4h12a2 2 0 002-2v-6a2 2 0 00-2-2H6a2 2 0 00-2 2v6a2 2 0 002 2z" />
                  </svg>
                </div>
              </div>

              <!-- Identity & Authorization Details -->
              <div class="space-y-0.5">
                <div class="text-sm sm:text-lg md:text-xl font-black text-emerald-300 tracking-wider font-mono uppercase">
                  ACCESS GRANTED // {usernameInput.trim().toUpperCase() || 'BALTHAZAR'}
                </div>
                <div class="text-[10px] sm:text-xs md:text-sm text-emerald-200/90 font-['Prompt',sans-serif]">
                  ยินดีต้อนรับเข้าสู่ระบบ กำลังเปิดศูนย์ควบคุมและสตรีมกล้อง AI...
                </div>
              </div>

              <!-- High-Speed Decryption Matrix Logs -->
              <div class="w-full bg-black/85 border border-emerald-500/50 p-1.5 sm:p-2.5 rounded-lg text-[9px] sm:text-xs font-mono text-left text-emerald-400 space-y-0.5 sm:space-y-1 shadow-[inset_0_0_20px_rgba(16,185,129,0.25)]">
                <div class="flex items-center gap-1.5 truncate">
                  <span class="text-emerald-400 font-bold">[✓]</span>
                  <span class="truncate">TOKEN KEY: <span class="text-emerald-300 font-bold">SHA-256 MATCHED</span></span>
                </div>
                <div class="flex items-center gap-1.5 truncate">
                  <span class="text-emerald-400 font-bold">[✓]</span>
                  <span class="truncate">NEURAL CORE: <span class="text-amber-300 font-bold">ATTACHED (512-D INT8)</span></span>
                </div>
                <div class="flex items-center gap-1.5 animate-pulse text-cyan-300 font-bold truncate">
                  <span>&gt;&gt;</span>
                  <span class="truncate">WARPING TO NEURAL COMMAND CENTER...</span>
                </div>
              </div>

            </div>

            <!-- Stage Footer -->
            <div class="flex items-center justify-between border-t border-emerald-500/30 pt-0.5 sm:pt-1 text-[9px] sm:text-xs text-emerald-400/80 shrink-0">
              <span>SECURITY: VERIFIED</span>
              <span class="animate-pulse font-bold text-emerald-300">LAUNCHING INTERFACE...</span>
            </div>

          </div>
        {/if}

      </div>

    </div>

    <!-- ========================================================================= -->
    <!-- Physical Heavy Industrial Toggle Switch Deck (Responsive Fixed Sizing)    -->
    <!-- ========================================================================= -->
    <div class="relative -mt-2.5 sm:-mt-4 z-30 w-[260px] sm:w-[340px] md:w-[380px] max-w-[92%] h-12 sm:h-15 md:h-16 rounded-xl sm:rounded-2xl bg-gradient-to-r from-[#1b202c] via-[#222838] to-[#1b202c] border-2 border-[#333e54] shadow-2xl grid grid-cols-3 items-center px-2.5 sm:px-4 shrink-0">
      
      <!-- Left Fixed-Width Label Area -->
      <div class="flex flex-col items-start justify-center pl-1 sm:pl-2">
        <span class="text-[8px] sm:text-[10px] md:text-[11px] font-bold text-white tracking-wider font-mono">MAIN POWER</span>
        <span class="text-[7px] sm:text-[8px] md:text-[9px] text-slate-400 font-mono">240V // 60Hz</span>
      </div>

      <!-- Center Heavy Chrome Toggle Switch -->
      <div class="flex flex-col items-center justify-center">
        <button
          type="button"
          onclick={togglePowerSwitch}
          class="relative w-7 sm:w-9 md:w-10 h-8 sm:h-10 md:h-11 rounded-lg sm:rounded-xl bg-gradient-to-b from-[#2e3748] via-[#161a24] to-[#0c0e14] border-2 border-[#475569] p-0.5 sm:p-1 flex flex-col items-center justify-between shadow-inner focus:outline-none cursor-pointer transform active:scale-95 transition-transform"
          title={isPowered ? "สับสวิตช์ปิดเครื่อง (POWER OFF)" : "สับสวิตช์เปิดเครื่อง (POWER ON)"}
        >
          <!-- Pilot Status Lamp (ไฟบนสวิตช์) -->
          <div class="w-1.5 sm:w-2.5 h-1.5 sm:h-2.5 rounded-full transition-all duration-300 {isPowered ? (bootStage === 5 ? 'bg-emerald-400 shadow-[0_0_12px_#10b981]' : 'bg-amber-400 shadow-[0_0_10px_#f59e0b]') : 'bg-red-950 border border-red-800'}"></div>

          <!-- Chrome Switch Lever (ก้านโยกสวิตช์) -->
          <div class="relative w-2 sm:w-2.5 md:w-3 h-3.5 sm:h-4.5 rounded-full bg-gradient-to-r from-[#94a3b8] via-[#f1f5f9] to-[#64748b] border border-black shadow-md transition-all duration-200 transform {isPowered ? '-translate-y-0.5' : 'translate-y-0.5'}"></div>

          <!-- Status Indicator Text -->
          <span class="text-[5px] sm:text-[6px] md:text-[7px] font-mono font-bold {isPowered ? (bootStage === 5 ? 'text-emerald-400' : 'text-amber-400') : 'text-slate-500'}">
            {isPowered ? 'ON' : 'OFF'}
          </span>
        </button>
      </div>

      <!-- Right Fixed-Width Status Area -->
      <div class="flex flex-col items-end justify-center pr-1 sm:pr-2">
        <span class="text-[8px] sm:text-[10px] md:text-[11px] font-bold {isPowered ? (bootStage === 5 ? 'text-emerald-400' : 'text-amber-400') : 'text-slate-500'} tracking-wider font-mono">
          {isPowered ? 'ONLINE' : 'OFFLINE'}
        </span>
        <span class="text-[7px] sm:text-[8px] md:text-[9px] text-slate-400 font-mono">NPU INT8</span>
      </div>

    </div>

  </main>

  <!-- ========================================================================= -->
  <!-- Bottom Laboratory Telemetry & Copyright Footer Bar (Fully Visible on All Screens) -->
  <!-- ========================================================================= -->
  <footer class="relative z-10 w-full max-w-5xl flex items-center justify-between px-2 sm:px-4 py-1 border-t border-[#202533]/80 text-[8px] xs:text-[9px] sm:text-xs text-slate-500 font-mono shrink-0 gap-1 sm:gap-2">
    <!-- Left: Brand / Copyright -->
    <div class="flex items-center gap-1 shrink-0">
      <span>© 2026</span>
      <span class="text-amber-400 font-bold tracking-wider">BALTHAZAR</span>
    </div>

    <!-- Center: Engine & Hardware Info (Always visible on mobile) -->
    <div class="flex items-center gap-1 sm:gap-1.5 text-slate-400 truncate">
      <span class="hidden md:inline">ENGINE:</span>
      <span class="text-emerald-400 font-bold truncate">MOBILEFACENET</span>
      <span class="text-slate-600">•</span>
      <span class="text-amber-400 font-bold shrink-0">PI 5</span>
    </div>

    <!-- Right: Anti-spoof Security -->
    <div class="flex items-center gap-1 shrink-0">
      <span class="hidden md:inline">SEC:</span>
      <span class="text-cyan-400 font-bold">MINIFASNET</span>
    </div>
  </footer>

</div>

<style>
  /* 25-column grid for progress bar blocks */
  .grid-cols-25 {
    grid-template-columns: repeat(25, minmax(0, 1fr));
  }

  /* CRT Scanline Horizontal Lines */
  .crt-scanlines {
    background: linear-gradient(
      rgba(18, 16, 16, 0) 50%, 
      rgba(0, 0, 0, 0.4) 50%
    );
    background-size: 100% 4px;
  }

  /* CRT Curved Screen Vignette */
  .crt-vignette {
    box-shadow: inset 0 0 70px rgba(0, 0, 0, 0.95),
                inset 0 0 25px rgba(245, 158, 11, 0.12);
  }

  .crt-vignette-emerald {
    box-shadow: inset 0 0 70px rgba(0, 0, 0, 0.95),
                inset 0 0 35px rgba(16, 185, 129, 0.25);
  }

  @keyframes fadeIn {
    from { opacity: 0; transform: scale(0.98); }
    to { opacity: 1; transform: scale(1); }
  }

  .animate-fade-in {
    animation: fadeIn 0.25s cubic-bezier(0.16, 1, 0.3, 1) forwards;
  }

  @keyframes warpIn {
    0% { opacity: 0; transform: scale(0.95); filter: blur(4px); }
    30% { opacity: 1; transform: scale(1.02); filter: blur(0px); }
    100% { opacity: 1; transform: scale(1); filter: blur(0px); }
  }

  .animate-warp {
    animation: warpIn 0.4s cubic-bezier(0.16, 1, 0.3, 1) forwards;
  }

  @keyframes bounceShort {
    0%, 100% { transform: translateY(0); }
    50% { transform: translateY(-4px); }
  }

  .animate-bounce-short {
    animation: bounceShort 1.2s ease-in-out infinite;
  }

  @keyframes shake {
    0%, 100% { transform: translateX(0); }
    20%, 60% { transform: translateX(-4px); }
    40%, 80% { transform: translateX(4px); }
  }

  .animate-shake {
    animation: shake 0.35s ease-in-out;
  }
</style>