<script lang="ts">
  import './layout.css';
  import { page } from '$app/state';
  import { onMount } from 'svelte';
  import { goto } from '$app/navigation';
  import { playMenuClickSound, playButtonPressSound, playWarningBeep, playToggleSound } from '$lib/audio';

  let { children } = $props();

  // Authentication Guard State
  let isAuthenticated = $state(false);
  let isCheckingAuth = $state(true);

  // Sidebar Collapse / Expand State
  let isSidebarCollapsed = $state(false);

  function toggleSidebar() {
    playButtonPressSound();
    isSidebarCollapsed = !isSidebarCollapsed;
  }

  // Real-time clock for top header
  let currentTime = $state('00:00:00');
  let currentDate = $state('');
  let serverOnline = $state(false);
  let cameraConnected = $state(false);
  let uptime = $state('00:00:00');

  // Camera Management State
  interface CameraDevice {
    id: number;
    name: string;
    resolution: string;
    available: boolean;
    active?: boolean;
  }
  let activeCameraId = $state(0);
  let availableCameras = $state<CameraDevice[]>([]);
  let isCameraDropdownOpen = $state(false);
  let isSwitchingCamera = $state(false);

  async function fetchCameras() {
    try {
      const res = await fetch('/api/cameras');
      if (res.ok) {
        const data = await res.json();
        if (data.cameras && data.cameras.length > 0) {
          availableCameras = data.cameras;
        }
        if (data.active_camera_id !== undefined) {
          activeCameraId = data.active_camera_id;
        }
      }
    } catch (e) {}
  }

  async function handleSwitchCamera(id: number) {
    if (isSwitchingCamera || id === activeCameraId) {
      isCameraDropdownOpen = false;
      return;
    }
    playToggleSound(true);
    isSwitchingCamera = true;
    try {
      const res = await fetch('/api/switch_camera', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ camera_id: id })
      });
      const data = await res.json().catch(() => ({}));
      if (res.ok && data.success) {
        activeCameraId = data.active_camera_id ?? id;
        cameraConnected = true;
        if (typeof window !== 'undefined') {
          window.dispatchEvent(new CustomEvent('camera_switched', { detail: { camera_id: activeCameraId } }));
        }
      }
    } catch (e) {
    } finally {
      isSwitchingCamera = false;
      isCameraDropdownOpen = false;
      await fetchCameras();
    }
  }

  function toggleCameraDropdown() {
    playButtonPressSound();
    isCameraDropdownOpen = !isCameraDropdownOpen;
    if (isCameraDropdownOpen) {
      fetchCameras();
    }
  }

  let isLandingPage = $derived(page.url.pathname === '/');

  // ── Smooth Dashboard Expansion Transition ────
  let isExpandingDashboard = $state(false);
  let expandTimer: ReturnType<typeof setTimeout> | null = null;

  // Soft high-tech arrival chime
  function playDashboardArrivalSound() {
    if (typeof window === 'undefined') return;
    try {
      const AudioCtx = window.AudioContext || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
      if (!AudioCtx) return;
      const ctx = new AudioCtx();
      const now = ctx.currentTime;

      // Soft melodic chord (F# -> A -> C# -> E)
      const notes = [740, 880, 1108, 1318];
      notes.forEach((freq, idx) => {
        const osc = ctx.createOscillator();
        const gain = ctx.createGain();
        osc.type = 'sine';
        osc.frequency.setValueAtTime(freq, now + idx * 0.05);
        gain.gain.setValueAtTime(0.04, now + idx * 0.05);
        gain.gain.exponentialRampToValueAtTime(0.0001, now + idx * 0.05 + 0.35);
        osc.connect(gain);
        gain.connect(ctx.destination);
        osc.start(now + idx * 0.05);
        osc.stop(now + idx * 0.05 + 0.38);
      });
    } catch (e) {}
  }

  function triggerDashboardExpansion() {
    if (expandTimer) clearTimeout(expandTimer);
    isExpandingDashboard = true;
    playDashboardArrivalSound();

    expandTimer = setTimeout(() => {
      isExpandingDashboard = false;
    }, 700);
  }

  // Strict Auth Guard Effect & Dynamic Dashboard Expansion Trigger
  $effect(() => {
    if (typeof window !== 'undefined') {
      const isAuth = sessionStorage.getItem('face_auth') === 'true';
      isAuthenticated = isAuth;
      isCheckingAuth = false;

      if (!isLandingPage && !isAuth) {
        goto('/');
      } else if (!isLandingPage && isAuth) {
        if (sessionStorage.getItem('trigger_dive_screen') === 'true' || sessionStorage.getItem('trigger_transition') === 'true') {
          sessionStorage.removeItem('trigger_dive_screen');
          sessionStorage.removeItem('trigger_transition');
          triggerDashboardExpansion();
        }
      }
    }
  });

  onMount(() => {
    // Initial Auth Check & Auto-trigger transition on manual browser refresh
    if (typeof window !== 'undefined') {
      const isAuth = sessionStorage.getItem('face_auth') === 'true';
      isAuthenticated = isAuth;
      isCheckingAuth = false;
      if (!isLandingPage && !isAuth) {
        goto('/');
      } else if (!isLandingPage && isAuth) {
        triggerDashboardExpansion();
      }
    }

    const updateTime = () => {
      const now = new Date();
      currentTime = now.toTimeString().split(' ')[0];
      const day = now.getDate();
      const months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
      const month = months[now.getMonth()];
      const year = now.getFullYear();
      currentDate = `${day} ${month} ${year}`;
    };
    updateTime();
    const timer = setInterval(updateTime, 1000);

    const checkServerStatus = async () => {
      try {
        const res = await fetch('/api/status');
        if (res.ok) {
          const data = await res.json();
          serverOnline = data.status === 'online';
          cameraConnected = data.camera_connected ?? true;
          uptime = data.uptime || '00:00:00';
          if (data.active_camera_id !== undefined) {
            activeCameraId = data.active_camera_id;
          }
        } else {
          serverOnline = false;
          cameraConnected = false;
        }
      } catch (e) {
        serverOnline = false;
        cameraConnected = false;
      }
    };

    checkServerStatus();
    fetchCameras();
    const statusTimer = setInterval(checkServerStatus, 3000);

    return () => {
      clearInterval(timer);
      clearInterval(statusTimer);
      if (expandTimer) clearTimeout(expandTimer);
    };
  });

  // Navigation menus
  const navItems = [
    { name: 'Face Recognition', path: '/face-recognition', icon: 'scan' },
    { name: 'Register Face', path: '/register-face', icon: 'register' },
    { name: 'Registered Persons', path: '/registered-persons', icon: 'users' },
    { name: 'Benchmark Demo', path: '/system-pipeline', icon: 'pipeline' },
    { name: 'Settings', path: '/settings', icon: 'settings' }
  ];

  function isActive(itemPath: string) {
    return page.url.pathname.startsWith(itemPath);
  }

  function handleNavClick() {
    playMenuClickSound();
  }

  function handleLogout() {
    playWarningBeep();
    try {
      sessionStorage.removeItem('face_auth');
      sessionStorage.removeItem('user');
      sessionStorage.removeItem('trigger_tv_static');
    } catch (e) {}
    isAuthenticated = false;
    goto('/');
  }
</script>

{#if isLandingPage}
  {@render children()}
{:else if isAuthenticated}
  <div class="relative flex h-screen bg-[#050811] text-slate-100 font-sans overflow-hidden select-none {isExpandingDashboard ? 'dashboard-expansion-anim' : ''}">

    <!-- Heavy Gunmetal Industrial Sidebar (Matching Login Chassis) -->
    <aside class="{isSidebarCollapsed ? 'w-16 px-1.5' : 'w-56 sm:w-60 px-2.5'} bg-gradient-to-b from-[#141822] via-[#0f131a] to-[#080a0f] border-r-2 border-[#222b3a] flex flex-col justify-between py-2.5 z-20 shrink-0 shadow-[10px_0_30px_rgba(0,0,0,0.8)] transition-all duration-300 ease-in-out overflow-hidden relative">
      
      <!-- Top Screws on Sidebar -->
      <div class="absolute top-1.5 left-2 screw-marker">+</div>
      <div class="absolute top-1.5 right-2 screw-marker">+</div>

      <div class="overflow-hidden pt-2">
        <!-- Terminal Header Brand with Raspberry Pi Logo -->
        <div class="mb-3 px-1 border-b border-[#222b3a] pb-2">
          <a
            href="/"
            onclick={handleNavClick}
            class="flex items-center space-x-2.5 p-1 rounded-lg hover:bg-white/[0.04] transition-all duration-200 group overflow-hidden w-full"
            title="กลับหน้าแรก Landing Page"
          >
            <div class="w-8 h-8 rounded-lg bg-gradient-to-br from-rose-500/20 via-pink-600/20 to-amber-500/20 border border-amber-500/40 p-1 flex items-center justify-center shadow-[0_0_12px_rgba(245,158,11,0.25)] group-hover:scale-105 transition-transform shrink-0">
              <img src="/raspberry-pi.svg" alt="Raspberry Pi Logo" class="w-full h-full object-contain filter drop-shadow-[0_0_6px_rgba(245,158,11,0.6)]" />
            </div>
            <div class="overflow-hidden transition-all duration-300 ease-in-out {isSidebarCollapsed ? 'opacity-0 max-w-0 -translate-x-3 pointer-events-none' : 'opacity-100 max-w-[170px] translate-x-0'}">
              <h1 class="text-[11px] font-black tracking-wider text-amber-400 font-mono whitespace-nowrap leading-tight">
                TERMINAL // 88
              </h1>
              <p class="text-[9.5px] font-semibold text-slate-400 flex items-center gap-1.5 whitespace-nowrap font-mono leading-none mt-0.5">
                <span class="text-amber-300/80">PI-5 NPU</span>
                <span class="w-1.5 h-1.5 rounded-full bg-emerald-400 animate-pulse shadow-[0_0_6px_#10b981]"></span>
              </p>
            </div>
          </a>
        </div>

        <!-- Terminal Navigation Links with Tactile Feedback & Amber Glowing Edges -->
        <nav class="space-y-1">
          {#each navItems as item}
            {@const active = isActive(item.path)}
            <a
              href={item.path}
              onclick={handleNavClick}
              title={isSidebarCollapsed ? item.name : ''}
              class="flex items-center {isSidebarCollapsed ? 'justify-center px-1.5 py-2' : 'space-x-2.5 px-3 py-2'} rounded-lg text-xs font-bold transition-all duration-200 group relative overflow-hidden {
                active
                  ? 'bg-gradient-to-r from-amber-500/20 via-amber-950/40 to-transparent text-amber-300 border border-amber-500/50 shadow-[inset_0_0_12px_rgba(245,158,11,0.15)] font-mono'
                  : 'text-slate-400 hover:text-amber-200 hover:bg-white/[0.03] border border-transparent hover:border-[#222b3a]'
              }"
            >
              {#if active}
                <div class="absolute left-0 top-1/2 -translate-y-1/2 w-1 h-5 bg-amber-400 rounded-r shadow-[0_0_8px_#f59e0b]"></div>
              {/if}

              <span class="w-4.5 h-4.5 flex items-center justify-center shrink-0">
                {#if item.icon === 'scan'}
                  <svg class="w-4 h-4 {active ? 'text-amber-400' : 'text-slate-400 group-hover:text-amber-400'}" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M7 3H5a2 2 0 00-2 2v2m0 10v2a2 2 0 002 2h2m10 0h2a2 2 0 002-2v-2m0-10V5a2 2 0 00-2-2H2M9 10a3 3 0 116 0 3 3 0 01-6 0zm-2 8a5 5 0 0110 0" />
                  </svg>
                {:else if item.icon === 'register'}
                  <svg class="w-4 h-4 {active ? 'text-amber-400' : 'text-slate-400 group-hover:text-amber-400'}" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M18 9v3m0 0v3m0-3h3m-3 0h-3m-2-5a4 4 0 11-8 0 4 4 0 018 0zM3 20a6 6 0 0112 0v1H3v-1z" />
                  </svg>
                {:else if item.icon === 'users'}
                  <svg class="w-4 h-4 {active ? 'text-amber-400' : 'text-slate-400 group-hover:text-amber-400'}" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 4.354a4 4 0 110 5.292M15 21H3v-1a6 6 0 0112 0v1zm0 0h6v-1a6 6 0 00-9-5.197M13 7a4 4 0 11-8 0 4 4 0 018 0z" />
                  </svg>
                {:else if item.icon === 'pipeline'}
                  <svg class="w-4 h-4 {active ? 'text-amber-400' : 'text-slate-400 group-hover:text-amber-400'}" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 10V3L4 14h7v7l9-11h-7z" />
                  </svg>
                {:else if item.icon === 'settings'}
                  <svg class="w-4 h-4 {active ? 'text-amber-400' : 'text-slate-400 group-hover:text-amber-400'}" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M10.325 4.317c.426-1.756 2.924-1.756 3.35 0a1.724 1.724 0 002.573 1.066c1.543-.94 3.31.826 2.37 2.37a1.724 1.724 0 001.065 2.572c1.756.426 1.756 2.924 0 3.35a1.724 1.724 0 00-1.066 2.573c.94 1.543-.826 3.31-2.37 2.37a1.724 1.724 0 00-2.572 1.065c-.426 1.756-2.924 1.756-3.35 0a1.724 1.724 0 00-2.573-1.066c-1.543.94-3.31-.826-2.37-2.37a1.724 1.724 0 00-1.065-2.572c-1.756-.426-1.756-2.924 0-3.35a1.724 1.724 0 001.066-2.573c-.94-1.543.826-3.31 2.37-2.37.996.608 2.296.07 2.572-1.065z" />
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M15 12a3 3 0 11-6 0 3 3 0 016 0z" />
                  </svg>
                {/if}
              </span>
              <span class="tracking-wide text-xs whitespace-nowrap overflow-hidden transition-all duration-300 ease-in-out {isSidebarCollapsed ? 'opacity-0 max-w-0 -translate-x-2 pointer-events-none' : 'opacity-100 max-w-[160px] translate-x-0'}">
                {item.name}
              </span>
            </a>
          {/each}

          <!-- Logout Button -->
          <button
            onclick={handleLogout}
            title={isSidebarCollapsed ? 'ออกจากระบบ' : ''}
            class="w-full flex items-center {isSidebarCollapsed ? 'justify-center px-1.5 py-2' : 'space-x-2.5 px-3 py-1.5'} rounded-lg text-xs font-bold text-rose-400 hover:text-rose-300 hover:bg-rose-950/40 border border-rose-500/20 hover:border-rose-500/50 transition-all duration-200 group mt-3 cursor-pointer text-left overflow-hidden font-mono"
          >
            <span class="w-4.5 h-4.5 flex items-center justify-center shrink-0">
              <svg class="w-4 h-4 text-rose-400 group-hover:scale-110 transition-transform" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M17 16l4-4m0 0l-4-4m4 4H7m6 4v1a3 3 0 01-3 3H6a3 3 0 01-3-3V7a3 3 0 013-3h4a3 3 0 013 3v1" />
              </svg>
            </span>
            <span class="tracking-wide text-xs whitespace-nowrap overflow-hidden transition-all duration-300 ease-in-out {isSidebarCollapsed ? 'opacity-0 max-w-0 -translate-x-2 pointer-events-none' : 'opacity-100 max-w-[160px] translate-x-0'}">
              LOGOUT // ออก
            </span>
          </button>
        </nav>
      </div>

      <!-- Bottom Hardware Telemetry on Sidebar -->
      <div class="bg-[#020406] p-2 rounded-lg border border-[#1c2433] shadow-[inset_0_0_12px_rgba(0,0,0,0.8)] overflow-hidden transition-all duration-300">
        {#if !isSidebarCollapsed}
          <div class="space-y-1 font-mono text-[10px]">
            <div class="flex justify-between items-center text-slate-400">
              <span>HOST:</span>
              {#if serverOnline}
                <span class="text-emerald-400 font-bold flex items-center gap-1">
                  <span class="w-1.5 h-1.5 rounded-full bg-emerald-400 animate-pulse"></span>
                  ONLINE
                </span>
              {:else}
                <span class="text-rose-400 font-bold">OFFLINE</span>
              {/if}
            </div>
            <div class="flex justify-between items-center text-slate-400">
              <span>CAMERA:</span>
              {#if cameraConnected}
                <span class="text-emerald-400 font-bold">V4L2_OK</span>
              {:else}
                <span class="text-amber-400 font-bold">NO_SENSOR</span>
              {/if}
            </div>
          </div>
        {:else}
          <div class="flex flex-col items-center gap-1.5" title="Pi 5: {serverOnline ? 'Online' : 'Offline'} | Camera: {cameraConnected ? 'Connected' : 'Disconnected'}">
            <span class="w-2 h-2 rounded-full {serverOnline ? 'bg-emerald-400 animate-pulse shadow-[0_0_6px_#10b981]' : 'bg-rose-500'}"></span>
            <span class="w-2 h-2 rounded-full {cameraConnected ? 'bg-emerald-400 shadow-[0_0_6px_#10b981]' : 'bg-amber-400'}"></span>
          </div>
        {/if}
      </div>
    </aside>

    <!-- Main Content Area -->
    <main class="flex-1 flex flex-col min-w-0 bg-[#050608] overflow-y-auto">
      <!-- Top Industrial Chassis Bar (Compact for Pi 7" 480px, Authentic Telemetry) -->
      <header class="py-1 px-3 sm:px-4 border-b-2 border-[#1c2433] flex flex-wrap items-center justify-between gap-2 bg-[#0c0f16] sticky top-0 z-30 shrink-0 shadow-md">
        
        <!-- Left: Sidebar Toggle + Telemetry Chips -->
        <div class="flex flex-wrap items-center gap-2">
          <button
            onclick={toggleSidebar}
            class="p-1 rounded-md bg-[#161c28] hover:bg-[#202838] border border-[#2c374c] text-slate-300 hover:text-amber-400 transition-all cursor-pointer"
            title={isSidebarCollapsed ? 'ขยายเมนู' : 'ย่อเมนู'}
          >
            <svg class="w-3.5 h-3.5 transition-transform duration-300 {isSidebarCollapsed ? 'rotate-180' : ''}" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M11 19l-7-7 7-7m8 14l-7-7 7-7" />
            </svg>
          </button>

          <!-- 1. Node Chip -->
          <div class="flex items-center gap-1.5 px-2 py-0.5 rounded bg-[#020406] border border-[#222b3a] font-mono text-[10px]">
            <span class="text-slate-500">SYS:</span>
            <span class="font-bold {serverOnline ? 'text-emerald-400' : 'text-rose-400'}">
              {serverOnline ? 'RPI5-ACTIVE' : 'OFFLINE'}
            </span>
          </div>

          <!-- 2. Engine Chip -->
          <div class="hidden sm:flex items-center gap-1.5 px-2 py-0.5 rounded bg-[#020406] border border-[#222b3a] font-mono text-[10px]">
            <span class="text-slate-500">CORE:</span>
            <span class="font-bold text-amber-300">MOBILEFACENET_INT8</span>
          </div>

          <!-- 3. Uptime Chip -->
          <div class="hidden md:flex items-center gap-1.5 px-2 py-0.5 rounded bg-[#020406] border border-[#222b3a] font-mono text-[10px]">
            <span class="text-slate-500">UP:</span>
            <span class="font-bold text-amber-400">{uptime}</span>
          </div>

          <!-- 4. Interactive Camera Selector Chip with Dropdown -->
          <div class="relative">
            <button
              onclick={toggleCameraDropdown}
              class="flex items-center gap-1.5 px-2 py-0.5 rounded bg-[#020406] hover:bg-[#161f30] border {cameraConnected ? 'border-emerald-500/40 text-emerald-300' : 'border-amber-500/40 text-amber-300'} font-mono text-[10px] transition-all cursor-pointer shadow-sm active:scale-95"
              title="คลิกเพื่อเลือกหรือสลับกล้อง (Switch Camera Device)"
            >
              <span class="w-1.5 h-1.5 rounded-full {cameraConnected ? 'bg-emerald-400 animate-pulse' : 'bg-amber-400'}"></span>
              <span class="text-slate-400">CAM:</span>
              <span class="font-bold whitespace-nowrap">
                {isSwitchingCamera ? 'SWITCHING...' : (availableCameras.find(c => c.id === activeCameraId)?.name || `CAM ${activeCameraId}`)}
              </span>
              <svg class="w-2.5 h-2.5 text-slate-400 transition-transform {isCameraDropdownOpen ? 'rotate-180' : ''}" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
              </svg>
            </button>

            {#if isCameraDropdownOpen}
              <!-- Dropdown Menu -->
              <div class="absolute left-0 top-full mt-1.5 w-64 sm:w-72 bg-[#090d16] border border-amber-500/50 rounded-xl shadow-2xl z-50 p-2 space-y-1.5 font-mono text-xs backdrop-blur-xl animate-in fade-in zoom-in duration-150">
                <div class="px-1.5 py-1 text-[9.5px] font-bold text-slate-400 uppercase tracking-wider border-b border-[#1f2838] flex items-center justify-between">
                  <span>&gt;&gt; SELECT ACTIVE CAMERA</span>
                  <button onclick={fetchCameras} class="text-amber-400 hover:text-amber-300 text-[9px] underline cursor-pointer" title="สแกนหากล้องใหม่">
                    REFRESH
                  </button>
                </div>
                {#if availableCameras.length === 0}
                  <div class="text-center py-2 text-slate-500 text-[10px]">
                    กำลังสแกนหากล้องในระบบ...
                  </div>
                {:else}
                  {#each availableCameras as cam}
                    <button
                      onclick={() => handleSwitchCamera(cam.id)}
                      class="w-full text-left px-2 py-1.5 rounded-lg flex items-center justify-between gap-2 transition-all cursor-pointer {cam.id === activeCameraId ? 'bg-amber-500/20 text-amber-300 border border-amber-500/40 font-bold' : 'text-slate-300 hover:bg-[#161f30] hover:text-white'}"
                    >
                      <div class="min-w-0 flex items-center gap-1.5 truncate">
                        <span class="text-[9px] px-1 py-0.2 rounded bg-black/60 border border-slate-700">{cam.id}</span>
                        <span class="truncate text-[10.5px]">{cam.name}</span>
                      </div>
                      {#if cam.id === activeCameraId}
                        <span class="text-[8.5px] text-emerald-400 font-bold shrink-0">● ACTIVE</span>
                      {/if}
                    </button>
                  {/each}
                {/if}
              </div>
            {/if}
          </div>
        </div>

        <!-- Right: Time & Grid Status -->
        <div class="flex items-center space-x-2">
          <div class="flex items-center space-x-2 bg-[#020406] px-2.5 py-0.5 rounded border border-[#222b3a] font-mono text-[11px]">
            <span class="w-2 h-2 rounded-full bg-amber-400 animate-pulse shadow-[0_0_6px_#f59e0b]"></span>
            <span class="text-amber-300 font-bold tracking-wider">{currentTime}</span>
            <span class="text-slate-500 text-[9px] hidden sm:inline">{currentDate}</span>
          </div>
        </div>
      </header>

      <!-- Page Content Container (Smooth Scroll on Small/Windowed Viewports) -->
      <div class="p-2 sm:p-3 flex-1 flex flex-col min-h-0 overflow-y-auto">
        {@render children()}
      </div>
    </main>
  </div>
{:else}
  <!-- Unauthorized Screen / Auto Redirecting -->
  <div class="flex h-screen w-screen items-center justify-center bg-[#040814] text-slate-400 font-mono text-xs select-none">
    <div class="flex flex-col items-center gap-3.5 text-center p-6 rounded-2xl bg-[#091224] border border-cyan-500/30 shadow-2xl">
      <div class="w-8 h-8 border-2 border-cyan-400 border-t-transparent rounded-full animate-spin"></div>
      <div>
        <span class="text-xs font-bold text-white block">กำลังตรวจสอบสิทธิ์ความปลอดภัย...</span>
        <span class="text-[10px] text-slate-500 mt-1 block">กรุณาเข้าสู่ระบบผ่านหน้าแรก</span>
      </div>
    </div>
  </div>
{/if}

<style>
  /* Smooth Dashboard Expansion Animation (60/120fps GPU-Accelerated) */
  .dashboard-expansion-anim {
    animation: smoothDashboardExpand 0.65s cubic-bezier(0.16, 1, 0.3, 1) forwards;
    will-change: transform, opacity;
  }

  @keyframes smoothDashboardExpand {
    0% {
      transform: scale(0.92);
      opacity: 0;
    }
    40% {
      opacity: 1;
    }
    100% {
      transform: scale(1);
      opacity: 1;
    }
  }
</style>
