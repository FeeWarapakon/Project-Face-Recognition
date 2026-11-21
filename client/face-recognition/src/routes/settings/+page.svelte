<script lang="ts">
	import { onMount } from 'svelte';
	import { playSound, playMenuClickSound, playButtonPressSound, playToggleSound, playWarningBeep, SOUND_OPTIONS, type SoundType } from '$lib/audio';

	// ── Auth Gate ─────────────────────────────────────────────────────────────
	const SETTINGS_PASSWORD = 'admin@staff';

	let authPassed = $state(false);
	let authUsername = $state('');
	let authPassword = $state('');
	let authError = $state('');
	let authLoading = $state(false);
	let showPassword = $state(false);

	function handleAuth() {
		playButtonPressSound();
		authLoading = true;
		authError = '';
		setTimeout(() => {
			if (authUsername.trim() === 'admin' && authPassword === SETTINGS_PASSWORD) {
				authPassed = true;
				authError = '';
			} else {
				playWarningBeep();
				authError = 'ชื่อผู้ใช้หรือรหัสผ่านไม่ถูกต้อง (INVALID_CREDENTIALS)';
				authPassword = '';
			}
			authLoading = false;
		}, 300);
	}

	function handleAuthKeydown(e: KeyboardEvent) {
		if (e.key === 'Enter') handleAuth();
	}

	// ── Settings Page State ───────────────────────────────────────────────────
	let activeTab = $state('Recognition');
	const tabs = ['Recognition', 'Audio & Alerts', 'Database'];

	function switchTab(tab: string) {
		playMenuClickSound();
		activeTab = tab;
	}

	// Settings State
	let similarityThreshold = $state(0.65);
	let detectionThreshold = $state(0.5);
	let enableClarity = $state(true);
	let autoBackup = $state(true);

	// Audio Settings State
	let audioNotification = $state(true);
	let selectedSound = $state<SoundType>('cyber_ping');
	let alertVolume = $state(80);

	// Anti-Spoof State
	let antiSpoofEnabled = $state(false);
	let antiSpoofAvailable = $state(false);
	let antiSpoofToggling = $state(false);

	let showSavedToast = $state(false);
	let isSaving = $state(false);

	// Clear Database Modal State
	let isClearDbModalOpen = $state(false);
	let deleteConfirmationText = $state('');
	let isClearingDb = $state(false);
	let clearDbError = $state('');

	// Result Notification Modal State
	interface ClearResultState {
		isOpen: boolean;
		isEmpty: boolean;
		title: string;
		message: string;
		details?: string;
	}
	let clearDbResult = $state<ClearResultState | null>(null);

	function openClearDbModal() {
		playWarningBeep();
		isClearDbModalOpen = true;
		deleteConfirmationText = '';
		clearDbError = '';
	}

	function closeClearDbModal() {
		playButtonPressSound();
		isClearDbModalOpen = false;
		deleteConfirmationText = '';
		clearDbError = '';
	}

	function closeResultModal() {
		playButtonPressSound();
		clearDbResult = null;
	}

	async function handleClearDatabase(e?: Event) {
		if (e) e.preventDefault();
		if (deleteConfirmationText.trim() !== 'DELETE') {
			playWarningBeep();
			clearDbError = 'กรุณาพิมพ์คำว่า DELETE ตัวพิมพ์ใหญ่ให้ถูกต้อง';
			return;
		}

		playWarningBeep();
		isClearingDb = true;
		clearDbError = '';
		try {
			const res = await fetch('/api/database/clear', {
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({ confirm: 'DELETE' })
			});
			const data = await res.json().catch(() => ({}));
			if (res.ok && data.success) {
				closeClearDbModal();
				if (data.is_empty) {
					clearDbResult = {
						isOpen: true,
						isEmpty: true,
						title: 'ℹ️ ฐานข้อมูลว่างเปล่า (Database Empty)',
						message: 'ขณะนี้ไม่มีข้อมูลบุคคลหรือเวกเตอร์ใดๆ อยู่ในฐานข้อมูล',
						details: 'ฐานข้อมูลพร้อมสำหรับการลงทะเบียนใบหน้าใหม่ได้ทันที'
					};
				} else {
					clearDbResult = {
						isOpen: true,
						isEmpty: false,
						title: '🎉 ล้างฐานข้อมูลสำเร็จเรียบร้อยแล้ว!',
						message: data.message || 'ลบข้อมูลบุคคล เวกเตอร์ 512 มิติ และประวัติการสแกนทั้งหมดเรียบร้อยแล้ว',
						details: `ลบรายชื่อบุคคล: ${data.deleted_persons ?? 0} คน | เวกเตอร์ใบหน้า: ${data.deleted_embeddings ?? 0} ชุด | บันทึกประวัติ: ${data.deleted_logs ?? 0} รายการ`
					};
				}
				playSound('access_granted', alertVolume);
			} else {
				clearDbError = data.error || 'เกิดข้อผิดพลาดในการล้างฐานข้อมูล';
			}
		} catch (err: any) {
			clearDbError = `เกิดข้อผิดพลาดในการเชื่อมต่อ: ${err?.message || err}`;
		} finally {
			isClearingDb = false;
		}
	}

	// Camera Device Settings State
	interface CameraDevice {
		id: number;
		name: string;
		resolution: string;
		available: boolean;
		active?: boolean;
	}
	let activeCameraId = $state(0);
	let availableCameras = $state<CameraDevice[]>([]);
	let isScanningCameras = $state(false);
	let isSwitchingCam = $state(false);

	async function fetchCameras() {
		isScanningCameras = true;
		try {
			const res = await fetch('/api/cameras');
			if (res.ok) {
				const data = await res.json();
				if (data.cameras) availableCameras = data.cameras;
				if (data.active_camera_id !== undefined) activeCameraId = data.active_camera_id;
			}
		} catch (e) {
		} finally {
			isScanningCameras = false;
		}
	}

	async function switchCamera(id: number) {
		if (isSwitchingCam || id === activeCameraId) return;
		playToggleSound(true);
		isSwitchingCam = true;
		try {
			const res = await fetch('/api/switch_camera', {
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({ camera_id: id })
			});
			const data = await res.json().catch(() => ({}));
			if (res.ok && data.success) {
				activeCameraId = data.active_camera_id ?? id;
				if (typeof window !== 'undefined') {
					window.dispatchEvent(new CustomEvent('camera_switched', { detail: { camera_id: activeCameraId } }));
				}
			}
		} catch (e) {
		} finally {
			isSwitchingCam = false;
			await fetchCameras();
		}
	}

	onMount(async () => {
		authPassed = false;
		authUsername = '';
		authPassword = '';
		authError = '';

		// Load local audio preferences
		if (typeof window !== 'undefined') {
			const savedSound = localStorage.getItem('face_sound_type') as SoundType;
			if (savedSound && ['cyber_ping', 'welcome_bell', 'access_granted'].includes(savedSound)) {
				selectedSound = savedSound;
			}
			const savedAudioEnabled = localStorage.getItem('face_sound_enabled');
			if (savedAudioEnabled !== null) {
				audioNotification = savedAudioEnabled === 'true';
			}
			const savedVolume = localStorage.getItem('face_sound_volume');
			if (savedVolume !== null) {
				alertVolume = Number(savedVolume);
			}
		}

		try {
			const res = await fetch('/api/settings');
			if (res.ok) {
				const data = await res.json();
				if (data.threshold !== undefined) similarityThreshold = data.threshold;
				if (data.clarity_booster !== undefined) enableClarity = data.clarity_booster;
				if (data.antispoof !== undefined) antiSpoofEnabled = data.antispoof;
				if (data.antispoof_available !== undefined) antiSpoofAvailable = data.antispoof_available;
			}
		} catch (e) {}

		fetchCameras();
	});

	async function toggleAntiSpoof() {
		if (antiSpoofToggling || !antiSpoofAvailable) return;
		playToggleSound(!antiSpoofEnabled);
		antiSpoofToggling = true;
		try {
			const res = await fetch('/api/toggle_antispoof', { method: 'POST' });
			if (res.ok) {
				const data = await res.json();
				antiSpoofEnabled = data.antispoof_enabled ?? !antiSpoofEnabled;
			}
		} catch (e) {
		} finally {
			antiSpoofToggling = false;
		}
	}

	function testAudio(soundId: SoundType) {
		playSound(soundId, alertVolume);
	}

	async function handleSave() {
		playButtonPressSound();
		isSaving = true;

		// Save audio settings locally
		if (typeof window !== 'undefined') {
			localStorage.setItem('face_sound_enabled', String(audioNotification));
			localStorage.setItem('face_sound_type', selectedSound);
			localStorage.setItem('face_sound_volume', String(alertVolume));
		}

		try {
			await fetch('/api/settings', {
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({
					threshold: Number(similarityThreshold),
					clarity_booster: enableClarity,
					antispoof: antiSpoofEnabled
				})
			});
			showSavedToast = true;
			setTimeout(() => {
				showSavedToast = false;
			}, 3000);
		} catch (e) {
			showSavedToast = true;
			setTimeout(() => {
				showSavedToast = false;
			}, 3000);
		} finally {
			isSaving = false;
		}
	}
</script>

<svelte:head>
	<title>System Settings // กำหนดค่าระบบ</title>
</svelte:head>

<!-- ══════════════════════════════════════════════════════════════════════════
     AUTH GATE — Retro Terminal Authentication
══════════════════════════════════════════════════════════════════════════ -->
{#if !authPassed}
	<div
		class="fixed inset-0 z-50 flex items-center justify-center bg-black/90 p-4 backdrop-blur-md"
	>
		<!-- Login Card -->
		<div class="w-full max-w-sm">
			<div class="chassis-panel rounded-2xl p-5 shadow-2xl space-y-4 border border-amber-500/50 relative">
				<div class="absolute top-1.5 left-2 screw-marker">+</div>
				<div class="absolute top-1.5 right-2 screw-marker">+</div>

				<!-- Header -->
				<div class="flex flex-col items-center text-center">
					<div
						class="mb-2.5 flex h-11 w-11 items-center justify-center rounded-xl border border-amber-500/50 bg-gradient-to-b from-amber-500/20 to-black text-amber-400 shadow-[0_0_20px_rgba(245,158,11,0.25)]"
					>
						<svg class="h-6 w-6 text-amber-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
							<path
								stroke-linecap="round"
								stroke-linejoin="round"
								stroke-width="1.8"
								d="M10.325 4.317c.426-1.756 2.924-1.756 3.35 0a1.724 1.724 0 002.573 1.066c1.543-.94 3.31.826 2.37 2.37a1.724 1.724 0 001.065 2.572c1.756.426 1.756 2.924 0 3.35a1.724 1.724 0 00-1.066 2.573c.94 1.543-.826 3.31-2.37 2.37a1.724 1.724 0 00-2.572 1.065c-.426 1.756-2.924 1.756-3.35 0a1.724 1.724 0 00-2.573-1.066c-1.543.94-3.31-.826-2.37-2.37a1.724 1.724 0 00-1.065-2.572c-1.756-.426-1.756-2.924 0-3.35a1.724 1.724 0 001.066-2.573c-.94-1.543.826-3.31 2.37-2.37.996.608 2.296.07 2.572-1.065z"
							/>
							<path
								stroke-linecap="round"
								stroke-linejoin="round"
								stroke-width="1.8"
								d="M15 12a3 3 0 11-6 0 3 3 0 016 0z"
							/>
						</svg>
					</div>
					<h1 class="text-sm sm:text-base font-black tracking-wider text-amber-300 font-mono uppercase">&gt;&gt; SYSTEM_CONFIG_AUTH</h1>
					<p class="mt-0.5 text-center text-[10px] text-slate-400 font-mono">
						ADMINISTRATIVE OPERATOR CLEARANCE REQUIRED
					</p>
				</div>

				<!-- Form Fields -->
				<div class="space-y-3 font-mono text-xs">
					<!-- Username -->
					<div class="space-y-1">
						<label for="auth-username" class="block text-[10px] font-bold text-amber-400 uppercase">ROOT_USER &gt;</label>
						<input
							id="auth-username"
							type="text"
							bind:value={authUsername}
							onkeydown={handleAuthKeydown}
							placeholder="admin"
							autocomplete="off"
							class="w-full terminal-input rounded-xl py-2 px-3 text-xs"
						/>
					</div>

					<!-- Password -->
					<div class="space-y-1">
						<label for="auth-password" class="block text-[10px] font-bold text-amber-400 uppercase">ACCESS_KEY &gt;</label>
						<div class="relative">
							<input
								id="auth-password"
								type={showPassword ? 'text' : 'password'}
								bind:value={authPassword}
								onkeydown={handleAuthKeydown}
								placeholder="••••••••"
								autocomplete="current-password"
								class="w-full terminal-input rounded-xl py-2 pr-14 pl-3 text-xs"
							/>
							<button
								type="button"
								onclick={() => (showPassword = !showPassword)}
								class="absolute top-1/2 right-2.5 -translate-y-1/2 text-[9px] font-bold text-amber-400 hover:text-amber-200 cursor-pointer"
								tabindex="-1"
							>
								{showPassword ? '[HIDE]' : '[SHOW]'}
							</button>
						</div>
					</div>

					<!-- Error Message -->
					{#if authError}
						<div
							class="flex items-center gap-2 rounded-lg border border-rose-500/60 bg-rose-950/80 px-3 py-1.5 text-rose-300 text-[10px] font-mono"
						>
							<svg
								class="h-3.5 w-3.5 shrink-0 text-rose-400"
								fill="none"
								stroke="currentColor"
								viewBox="0 0 24 24"
							>
								<path
									stroke-linecap="round"
									stroke-linejoin="round"
									stroke-width="2"
									d="M12 8v4m0 4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z"
								/>
							</svg>
							<span>{authError}</span>
						</div>
					{/if}

					<!-- Submit Button -->
					<button
						onclick={handleAuth}
						disabled={authLoading || !authUsername || !authPassword}
						class="w-full cursor-pointer bg-gradient-to-r from-amber-500 via-amber-400 to-yellow-500 hover:from-amber-400 hover:to-yellow-400 py-2.5 text-xs font-black tracking-wider text-slate-950 rounded-xl shadow-[0_0_20px_rgba(245,158,11,0.35)] transition-all active:scale-95 disabled:cursor-not-allowed disabled:opacity-40 uppercase flex items-center justify-center gap-2"
					>
						{#if authLoading}
							<div class="h-3.5 w-3.5 border-2 border-slate-950 border-t-transparent rounded-full animate-spin"></div>
							<span>AUTHENTICATING...</span>
						{:else}
							<svg class="h-4 w-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
								<path
									stroke-linecap="round"
									stroke-linejoin="round"
									stroke-width="2.5"
									d="M8 11V7a4 4 0 118 0m-4 8v2m-6 4h12a2 2 0 002-2v-6a2 2 0 00-2-2H6a2 2 0 00-2 2v6a2 2 0 002 2z"
								/>
							</svg>
							<span>UNLOCK_CONFIG</span>
						{/if}
					</button>
				</div>
			</div>
		</div>
	</div>
{/if}

<!-- ══════════════════════════════════════════════════════════════════════════
     SETTINGS DASHBOARD (Optimized for Pi 7", iPad & Desktop)
══════════════════════════════════════════════════════════════════════════ -->
<div
	class="mx-auto flex flex-1 flex-col min-h-0 w-full max-w-4xl select-none gap-3 {!authPassed
		? 'pointer-events-none opacity-30 blur-sm'
		: ''}"
>
	<!-- Header Bar -->
	<div class="relative chassis-panel p-3 sm:p-4 rounded-2xl flex items-center justify-between gap-3 shrink-0 overflow-hidden">
		<div class="absolute top-1.5 left-2 screw-marker">+</div>
		<div class="absolute top-1.5 right-2 screw-marker">+</div>

		<div class="flex items-center gap-3 relative z-10">
			<div
				class="flex h-9 w-9 items-center justify-center rounded-xl border border-amber-500/50 bg-gradient-to-b from-amber-500/20 to-black text-amber-400 shadow-[0_0_15px_rgba(245,158,11,0.2)]"
			>
				<svg class="h-4 w-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
					<path
						stroke-linecap="round"
						stroke-linejoin="round"
						stroke-width="2"
						d="M10.325 4.317c.426-1.756 2.924-1.756 3.35 0a1.724 1.724 0 002.573 1.066c1.543-.94 3.31.826 2.37 2.37a1.724 1.724 0 001.065 2.572c1.756.426 1.756 2.924 0 3.35a1.724 1.724 0 00-1.066 2.573c.94 1.543-.826 3.31-2.37 2.37a1.724 1.724 0 00-2.572 1.065c-.426 1.756-2.924 1.756-3.35 0a1.724 1.724 0 00-2.573-1.066c-1.543.94-3.31-.826-2.37-2.37a1.724 1.724 0 00-1.065-2.572c-1.756-.426-1.756-2.924 0-3.35a1.724 1.724 0 001.066-2.573c-.94-1.543.826-3.31 2.37-2.37.996.608 2.296.07 2.572-1.065z"
					/>
					<path
						stroke-linecap="round"
						stroke-linejoin="round"
						stroke-width="2"
						d="M15 12a3 3 0 11-6 0 3 3 0 016 0z"
					/>
				</svg>
			</div>
			<div>
				<div class="flex items-center gap-2">
					<span class="text-[10px] font-mono font-bold text-amber-400/90 tracking-widest uppercase">&gt;&gt; SYSTEM_CONFIG_CONSOLE</span>
					<span class="rounded bg-emerald-950/80 border border-emerald-500/40 px-1.5 py-0.5 font-mono text-[9px] font-bold text-emerald-400">
						🔓 ROOT_UNLOCKED
					</span>
				</div>
				<h2 class="text-sm sm:text-base font-black tracking-wide text-white leading-tight mt-0.5">
					การตั้งค่าระบบ (System Parameters & Audio)
				</h2>
			</div>
		</div>

		<!-- Single Dedicated Save Button -->
		<button
			onclick={handleSave}
			disabled={isSaving}
			class="relative z-10 flex cursor-pointer items-center gap-1.5 rounded-xl bg-gradient-to-r from-amber-500 via-amber-400 to-yellow-500 px-4 py-2 text-xs font-black tracking-wider text-slate-950 shadow-[0_0_20px_rgba(245,158,11,0.35)] transition-all hover:from-amber-400 hover:to-yellow-400 active:scale-95 disabled:opacity-50 uppercase font-mono"
		>
			<svg class="h-3.5 w-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
				<path
					stroke-linecap="round"
					stroke-linejoin="round"
					stroke-width="2.5"
					d="M5 13l4 4L19 7"
				/>
			</svg>
			<span>{isSaving ? 'COMMITTING...' : 'COMMIT_CONFIG'}</span>
		</button>
	</div>

	<!-- Toast Notification -->
	{#if showSavedToast}
		<div
			class="flex items-center gap-2 rounded-xl border border-emerald-500/50 bg-emerald-950/80 px-3 py-2 text-xs font-mono font-bold text-emerald-300 shadow-sm shrink-0"
		>
			<svg
				class="h-4 w-4 shrink-0 text-emerald-400"
				fill="none"
				stroke="currentColor"
				viewBox="0 0 24 24"
			>
				<path
					stroke-linecap="round"
					stroke-linejoin="round"
					stroke-width="2.5"
					d="M9 12l2 2 4-4m6 2a9 9 0 11-18 0 9 9 0 0118 0z"
				/>
			</svg>
			<span>&gt;&gt; SYSTEM PARAMETERS COMMITTED AND PERSISTED SUCCESSFULLY.</span>
		</div>
	{/if}

	<!-- Tab Navigation Bar -->
	<div
		class="flex items-center gap-1.5 rounded-xl border border-[#283348] bg-[#0a0d14] p-1.5 shrink-0 font-mono text-xs"
	>
		{#each tabs as tab, idx}
			<button
				onclick={() => switchTab(tab)}
				class="rounded-lg px-3.5 py-1.5 font-bold transition-all duration-200 cursor-pointer {activeTab === tab
					? 'bg-amber-500 text-slate-950 font-black shadow-[0_0_12px_rgba(245,158,11,0.4)]'
					: 'text-slate-400 hover:bg-[#141822] hover:text-slate-200'}"
			>
				[{idx+1}] {tab.toUpperCase()}
			</button>
		{/each}
	</div>

	<!-- Main Settings Panel Card -->
	<div class="flex-1 flex flex-col min-h-0 rounded-2xl chassis-panel p-4 shadow-xl overflow-y-auto relative">
		<div class="absolute top-1.5 left-2 screw-marker">+</div>
		<div class="absolute top-1.5 right-2 screw-marker">+</div>

		{#if activeTab === 'Recognition'}
			<!-- ── Tab 1: Recognition Parameters ──────────────────────────────────── -->
			<div class="space-y-3.5 relative z-10">
				<h3 class="text-[11px] font-mono font-bold tracking-wider text-amber-300 uppercase">
					&gt;&gt; COSINE_SIMILARITY_&_INFERENCE_CALIBRATION
				</h3>

				<div class="grid grid-cols-1 gap-3 text-xs md:grid-cols-2">
					<!-- Cosine Threshold Slider -->
					<div class="space-y-1.5 rounded-xl border border-[#283348] bg-[#05080e] p-3.5">
						<div class="flex items-center justify-between">
							<label for="similarity-slider" class="font-mono font-bold text-slate-200 text-xs"
								>&gt; COSINE_THRESHOLD</label
							>
							<span
								class="rounded border border-amber-500/40 bg-amber-950/80 px-2 py-0.5 font-mono text-xs font-black text-amber-300"
							>
								{Number(similarityThreshold).toFixed(2)}
							</span>
						</div>
						<input
							id="similarity-slider"
							type="range"
							min="0.40"
							max="0.95"
							step="0.01"
							bind:value={similarityThreshold}
							class="h-1.5 w-full cursor-pointer rounded-lg bg-slate-800 accent-amber-400"
						/>
						<div class="flex justify-between font-mono text-[9px] text-slate-500">
							<span>0.40 (ผ่อนปรน)</span>
							<span class="text-amber-400">แนะนำ 0.65</span>
							<span>0.95 (เข้มงวด)</span>
						</div>
						<p class="text-[10px] text-slate-400 pt-0.5 font-sans">
							ความเหมือนขั้นต่ำในการตัดสินว่าเป็นบุคคลที่ลงทะเบียนไว้
						</p>
					</div>

					<!-- MTCNN Detection Min Confidence -->
					<div class="space-y-1.5 rounded-xl border border-[#283348] bg-[#05080e] p-3.5">
						<div class="flex items-center justify-between">
							<label for="detection-slider" class="font-mono font-bold text-slate-200 text-xs"
								>&gt; MTCNN_DETECTION_CONFIDENCE</label
							>
							<span
								class="rounded border border-emerald-500/40 bg-emerald-950/80 px-2 py-0.5 font-mono text-xs font-black text-emerald-400"
							>
								{Number(detectionThreshold).toFixed(2)}
							</span>
						</div>
						<input
							id="detection-slider"
							type="range"
							min="0.30"
							max="0.90"
							step="0.05"
							bind:value={detectionThreshold}
							class="h-1.5 w-full cursor-pointer rounded-lg bg-slate-800 accent-emerald-400"
						/>
						<div class="flex justify-between font-mono text-[9px] text-slate-500">
							<span>0.30</span>
							<span class="text-emerald-400">แนะนำ 0.50</span>
							<span>0.90</span>
						</div>
						<p class="text-[10px] text-slate-400 pt-0.5 font-sans">
							ความมั่นใจขั้นต่ำของโครงข่าย MTCNN ในการจับใบหน้า
						</p>
					</div>
				</div>

				<!-- Clarity & Alignment Booster Toggle -->
				<div
					class="flex items-center justify-between rounded-xl border border-[#283348] bg-[#05080e] p-3.5 text-xs"
				>
					<div class="space-y-0.5">
						<span class="font-mono font-bold text-slate-200">&gt; FACE_ALIGNMENT_&_CLARITY_BOOSTER</span>
						<p class="text-[10px] text-slate-400 font-sans">
							ปรับระนาบดวงตาและปรับแสงอัตโนมัติก่อนส่งเข้า MobileFaceNet เพิ่มความแม่นยำ
						</p>
					</div>
					<input
						type="checkbox"
						bind:checked={enableClarity}
						class="h-4 w-4 cursor-pointer rounded accent-amber-400"
					/>
				</div>

				<!-- Face Anti-Spoof Section -->
				<div class="pt-1">
					<h3
						class="mb-2 flex items-center gap-1.5 text-[11px] font-mono font-bold tracking-wider text-amber-300 uppercase"
					>
						&gt;&gt; LIVENESS_&_ANTI_SPOOFING_UNIT
					</h3>

					<!-- Anti-Spoof Card -->
					<div
						class="rounded-xl border bg-[#05080e] p-3.5 {antiSpoofEnabled
							? 'border-emerald-500/50 shadow-[0_0_15px_rgba(16,185,129,0.15)]'
							: 'border-[#283348]'} transition-all duration-300"
					>
						<div class="flex items-start justify-between gap-3">
							<div class="flex items-start gap-3">
								<div
									class="h-8 w-8 rounded-lg {antiSpoofEnabled
										? 'border-emerald-500/50 bg-emerald-500/20 text-emerald-300'
										: 'border-slate-700 bg-slate-800 text-slate-400'} flex shrink-0 items-center justify-center border transition-all duration-300 font-mono text-sm"
								>
									🛡️
								</div>
								<div class="space-y-0.5 text-xs">
									<div class="text-xs font-mono font-bold text-slate-200">
										MiniFASNet V2 Liveness Defense
									</div>
									<p class="text-[10px] leading-tight text-slate-400 font-sans">
										ตรวจจับ Liveness ป้องกันรูปถ่าย หน้าจอมือถือ หรือหน้ากากปลอม
									</p>
									<div class="flex flex-wrap items-center gap-1.5 pt-0.5">
										<span
											class="rounded border px-1.5 py-0.5 font-mono text-[9px] {antiSpoofEnabled
												? 'border-emerald-500/40 bg-emerald-950/80 text-emerald-400'
												: 'border-slate-700 bg-slate-800/50 text-slate-500'}"
										>
											{antiSpoofEnabled ? '● ACTIVE' : '○ DISABLED'}
										</span>
										{#if !antiSpoofAvailable}
											<span
												class="rounded border border-amber-500/40 bg-amber-950/80 px-1.5 py-0.5 font-mono text-[9px] text-amber-400"
											>
												⚠️ REQUIRES anti_spoof.py
											</span>
										{:else}
											<span
												class="rounded border border-[#283348] px-1.5 py-0.5 font-mono text-[9px] text-slate-400"
											>
												Dual-Scale Zero-Gap
											</span>
										{/if}
									</div>
								</div>
							</div>

							<!-- Toggle Switch -->
							<button
								onclick={toggleAntiSpoof}
								disabled={!antiSpoofAvailable || antiSpoofToggling}
								title={antiSpoofAvailable ? 'คลิกเปิด/ปิด Anti-Spoof' : 'ไม่พบไฟล์โมเดล'}
								class="relative mt-0.5 h-5 w-10 shrink-0 cursor-pointer rounded-full transition-all duration-300 focus:outline-none disabled:cursor-not-allowed disabled:opacity-40 {antiSpoofEnabled
									? 'bg-emerald-500 shadow-[0_0_10px_rgba(16,185,129,0.4)]'
									: 'bg-slate-700'}"
							>
								<span
									class="absolute top-0.5 left-0.5 h-4 w-4 rounded-full bg-white shadow transition-transform duration-300 {antiSpoofEnabled
										? 'translate-x-5'
										: 'translate-x-0'}"
								>
									{#if antiSpoofToggling}
										<svg
											class="h-full w-full animate-spin p-0.5 text-slate-400"
											fill="none"
											viewBox="0 0 24 24"
										>
											<circle
												class="opacity-25"
												cx="12"
												cy="12"
												r="10"
												stroke="currentColor"
												stroke-width="4"
											></circle>
											<path
												class="opacity-75"
												fill="currentColor"
												d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z"
											></path>
										</svg>
									{/if}
								</span>
							</button>
						</div>
					</div>
				</div>

				<!-- Hardware & Camera Selection Section -->
				<div class="pt-1">
					<div class="flex items-center justify-between mb-2">
						<h3 class="flex items-center gap-1.5 text-[11px] font-mono font-bold tracking-wider text-amber-300 uppercase">
							&gt;&gt; HARDWARE_VIDEO_CAPTURE_SENSOR (CAMERA_SELECTION)
						</h3>
						<button
							onclick={fetchCameras}
							disabled={isScanningCameras}
							class="text-amber-400 hover:text-amber-300 font-mono text-[10px] flex items-center gap-1 underline cursor-pointer disabled:opacity-50"
						>
							<svg class="w-3 h-3 {isScanningCameras ? 'animate-spin' : ''}" fill="none" stroke="currentColor" viewBox="0 0 24 24">
								<path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
							</svg>
							<span>{isScanningCameras ? 'SCANNING...' : 'SCAN_CAMERAS'}</span>
						</button>
					</div>

					<div class="rounded-xl border border-[#283348] bg-[#05080e] p-3.5 space-y-3">
						<p class="text-[10px] text-slate-400 font-sans">
							เลือกอุปกรณ์กล้องที่ต้องการใช้ (รองรับทั้ง กล้องในตัวโน้ตบุ๊ก, กล้อง USB Webcam, และ Raspberry Pi Camera V4L2)
						</p>

						<div class="grid grid-cols-1 sm:grid-cols-2 gap-2">
							{#each availableCameras as cam}
								<button
									type="button"
									onclick={() => switchCamera(cam.id)}
									disabled={isSwitchingCam}
									class="p-3 rounded-xl border text-left flex items-center justify-between gap-2.5 transition-all cursor-pointer {cam.id === activeCameraId ? 'border-amber-500/70 bg-amber-950/30 text-white shadow-[0_0_15px_rgba(245,158,11,0.2)]' : 'border-[#1f2838] bg-[#090d16] text-slate-400 hover:border-slate-600 hover:text-slate-200'}"
								>
									<div class="flex items-center gap-2.5 min-w-0">
										<div class="w-8 h-8 rounded-lg flex items-center justify-center font-mono text-sm shrink-0 {cam.id === activeCameraId ? 'bg-amber-500/20 text-amber-300 border border-amber-500/50' : 'bg-slate-800 text-slate-500 border border-slate-700'}">
											📷
										</div>
										<div class="min-w-0">
											<div class="text-xs font-mono font-bold truncate {cam.id === activeCameraId ? 'text-amber-300' : 'text-slate-200'}">
												{cam.name}
											</div>
											<div class="text-[9.5px] font-mono text-slate-500">
												DEV_ID: {cam.id} | {cam.resolution}
											</div>
										</div>
									</div>

									{#if cam.id === activeCameraId}
										<span class="rounded bg-emerald-950/90 border border-emerald-500/50 px-2 py-0.5 font-mono text-[9px] font-black text-emerald-400 shrink-0 shadow-sm flex items-center gap-1">
											<span class="w-1.5 h-1.5 rounded-full bg-emerald-400 animate-pulse"></span>
											ACTIVE
										</span>
									{:else}
										<span class="rounded bg-[#161c28] border border-[#2c374c] px-2 py-0.5 font-mono text-[9px] text-slate-400 shrink-0">
											SELECT
										</span>
									{/if}
								</button>
							{/each}
						</div>
					</div>
				</div>
			</div>

		{:else if activeTab === 'Audio & Alerts'}
			<!-- ── Tab 2: Audio & Alerts ──────────────────────────────────────────── -->
			<div class="space-y-3.5 text-xs relative z-10 font-mono">
				<h3 class="text-[11px] font-bold tracking-wider text-amber-300 uppercase">
					&gt;&gt; AUDIO_TELEMETRY_&_SOUND_SYNTHESIZER
				</h3>

				<!-- Main Sound Toggle Switch -->
				<div
					class="flex items-center justify-between rounded-xl border border-[#283348] bg-[#05080e] p-3.5"
				>
					<div class="space-y-0.5">
						<span class="font-bold text-slate-200">&gt; RECOGNITION_CHIME_ENABLE</span>
						<p class="text-[10px] text-slate-400 font-sans">
							เล่นเสียงสังเคราะห์แจ้งเตือนทันทีเมื่อตรวจพบบุคคลที่ลงทะเบียนไว้ (Known Face)
						</p>
					</div>
					<input
						type="checkbox"
						bind:checked={audioNotification}
						class="h-4 w-4 cursor-pointer rounded accent-amber-400"
					/>
				</div>

				<!-- Sound Profiles Selection -->
				<div class="space-y-2 pt-1">
					<span class="font-bold text-slate-300 text-xs block">&gt; SOUND_PROFILE_SELECTION:</span>
					<div class="grid grid-cols-1 gap-2.5 sm:grid-cols-3">
						{#each SOUND_OPTIONS as sound}
							<!-- svelte-ignore a11y_no_noninteractive_element_interactions -->
							<div
								role="button"
								tabindex="0"
								onclick={() => (selectedSound = sound.id)}
								onkeydown={(e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); selectedSound = sound.id; } }}
								class="flex flex-col justify-between rounded-xl border p-3 cursor-pointer transition-all duration-200 {selectedSound === sound.id
									? 'border-amber-500/60 bg-amber-950/40 shadow-[0_0_15px_rgba(245,158,11,0.2)] text-amber-200'
									: 'border-[#283348] bg-[#05080e] hover:border-slate-600'}"
							>
								<div>
									<div class="flex items-center justify-between mb-1.5">
										<span class="text-base">{sound.icon}</span>
										<input
											type="radio"
											name="sound_type"
											value={sound.id}
											checked={selectedSound === sound.id}
											class="accent-amber-400 cursor-pointer"
										/>
									</div>
									<div class="font-bold text-slate-200 text-xs font-sans">{sound.name}</div>
									<p class="text-[10px] text-slate-400 mt-1 leading-tight font-sans">{sound.desc}</p>
								</div>

								<!-- Test Sound Button -->
								<button
									type="button"
									onclick={(e) => {
										e.stopPropagation();
										selectedSound = sound.id;
										testAudio(sound.id);
									}}
									class="mt-3 flex items-center justify-center gap-1.5 rounded-lg border border-amber-500/40 bg-amber-500/15 py-1.5 px-2 text-[10px] font-bold text-amber-300 hover:bg-amber-500/25 active:scale-95 transition-all uppercase"
								>
									<svg class="h-3 w-3 text-amber-400" fill="currentColor" viewBox="0 0 24 24">
										<path d="M8 5v14l11-7z" />
									</svg>
									<span>TEST_CHIME</span>
								</button>
							</div>
						{/each}
					</div>
				</div>

				<!-- Volume Slider -->
				<div class="space-y-1.5 rounded-xl border border-[#283348] bg-[#05080e] p-3.5 pt-2">
					<div class="flex items-center justify-between">
						<label for="volume-slider" class="font-bold text-slate-200 text-xs"
							>&gt; MASTER_VOLUME_GAIN</label
						>
						<span class="font-mono font-bold text-amber-400 text-xs">{alertVolume}%</span>
					</div>
					<input
						id="volume-slider"
						type="range"
						min="0"
						max="100"
						bind:value={alertVolume}
						class="h-1.5 w-full cursor-pointer rounded-lg bg-slate-800 accent-amber-400"
					/>
				</div>
			</div>

		{:else if activeTab === 'Database'}
			<!-- ── Tab 3: Database ────────────────────────────────────────────────── -->
			<div class="space-y-4 text-xs relative z-10 font-mono">
				<h3 class="text-[11px] font-bold tracking-wider text-amber-300 uppercase">
					&gt;&gt; SQLITE_PERSISTENCE_&_DATA_RETENTION
				</h3>

				<div class="grid grid-cols-1 gap-3 md:grid-cols-2">
					<div class="space-y-1.5 rounded-xl border border-[#283348] bg-[#05080e] p-3.5">
						<span class="font-bold text-slate-200">&gt; DB_STORAGE_PATH:</span>
						<div
							class="rounded-lg border border-[#283348] bg-black/60 p-2 font-mono text-[10px] text-amber-300"
						>
							Antigravity/face_recognition.db
						</div>
					</div>
					<div
						class="flex items-center justify-between rounded-xl border border-[#283348] bg-[#05080e] p-3.5"
					>
						<div class="space-y-0.5">
							<span class="font-bold text-slate-200">&gt; AUTO_BACKUP_SQLITE</span>
							<p class="text-[10px] text-slate-400 font-sans">
								สำรองไฟล์ฐานข้อมูลทุกครั้งที่มีการลงทะเบียนใบหน้าใหม่
							</p>
						</div>
						<input
							type="checkbox"
							bind:checked={autoBackup}
							class="h-4 w-4 cursor-pointer rounded accent-amber-400"
						/>
					</div>
				</div>

				<!-- Danger Zone: Clear Database Section -->
				<div class="hazard-stripes rounded-xl border-2 border-rose-500/50 p-4 space-y-3 shadow-[0_0_20px_rgba(244,63,94,0.15)] bg-[#100609]">
					<div class="flex flex-col sm:flex-row sm:items-center justify-between gap-3">
						<div class="space-y-1">
							<div class="flex items-center gap-2">
								<span class="px-2 py-0.5 rounded bg-rose-500/20 border border-rose-500/50 text-rose-400 text-[10px] font-black font-mono">DANGER_ZONE</span>
								<h4 class="font-bold text-slate-100 text-xs sm:text-sm font-sans">ล้างฐานข้อมูลทั้งหมด (Clear & Reset Database)</h4>
							</div>
							<p class="text-[11px] text-slate-400 leading-relaxed font-sans">
								ลบข้อมูลบุคคลทั้งหมด ชุดเวกเตอร์เอกลักษณ์ใบหน้า (512-D) และประวัติการสแกนทั้งหมดอย่างถาวร (จะมีการขอพิมพ์ยืนยัน <strong class="text-rose-400 font-mono">DELETE</strong> ก่อนลบ)
							</p>
						</div>

						<button
							type="button"
							onclick={openClearDbModal}
							class="px-4 py-2.5 rounded-xl text-xs font-black text-white bg-rose-600 hover:bg-rose-500 border border-rose-400 shadow-[0_0_15px_rgba(244,63,94,0.35)] hover:scale-105 active:scale-95 transition-all flex items-center justify-center gap-2 shrink-0 cursor-pointer uppercase font-mono"
						>
							<svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
								<path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
							</svg>
							<span>PURGE_DATABASE</span>
						</button>
					</div>
				</div>
			</div>
		{/if}
	</div>
</div>

<!-- ══════════════════════════════════════════════════════════════════════════
     CONFIRMATION MODAL: TYPE "DELETE" TO CLEAR DATABASE
══════════════════════════════════════════════════════════════════════════ -->
{#if isClearDbModalOpen}
	<div class="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/85 backdrop-blur-md animate-fade-in font-mono">
		<div class="relative w-full max-w-md chassis-panel border-2 border-rose-500/70 rounded-2xl p-5 shadow-[0_0_50px_rgba(244,63,94,0.3)] space-y-4">
			<div class="absolute top-1.5 left-2 screw-marker">+</div>
			<div class="absolute top-1.5 right-2 screw-marker">+</div>

			<!-- Header -->
			<div class="flex items-start justify-between gap-3">
				<div class="flex items-center gap-3">
					<div class="w-9 h-9 rounded-xl bg-rose-500/20 border border-rose-500/50 flex items-center justify-center text-rose-400 shrink-0 shadow-[0_0_15px_rgba(244,63,94,0.25)]">
						<svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
							<path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
						</svg>
					</div>
					<div>
						<h3 class="text-xs sm:text-sm font-black text-rose-300 uppercase tracking-wide">&gt;&gt; PERMANENT_DATABASE_PURGE</h3>
						<p class="text-[10px] text-rose-400 font-bold">
							IRREVERSIBLE DATA LOSS WARNING
						</p>
					</div>
				</div>

				<button
					type="button"
					onclick={closeClearDbModal}
					aria-label="ปิดหน้าต่างยืนยัน"
					class="text-slate-400 hover:text-white p-1 rounded-lg bg-slate-900/60 border border-[#283348] hover:border-slate-500 transition cursor-pointer"
				>
					<svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
						<path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
					</svg>
				</button>
			</div>

			<!-- Warning Details -->
			<div class="text-xs bg-rose-950/40 p-3 rounded-xl border border-rose-500/40 space-y-1 leading-relaxed">
				<p class="font-bold text-rose-300">
					⚠️ ระบบจะทำการลบข้อมูลต่อไปนี้ทั้งหมดอย่างถาวร:
				</p>
				<ul class="list-disc list-inside text-slate-400 text-[11px] font-sans">
					<li>รายชื่อสมาชิกและข้อมูลบุคคลทั้งหมด (Persons)</li>
					<li>ชุดเวกเตอร์ใบหน้าทั้งหมด (Face Embeddings)</li>
					<li>ประวัติการสแกนและบันทึกเหตุการณ์ทั้งหมด (Recognition Logs)</li>
				</ul>
			</div>

			<form onsubmit={handleClearDatabase} class="space-y-3 pt-1">
				<div>
					<label for="delete-confirm-input" class="block text-[11px] font-bold text-slate-300 mb-1">
						พิมพ์คำว่า <span class="font-mono text-rose-400 font-black bg-rose-500/10 px-1.5 py-0.5 rounded border border-rose-500/30">DELETE</span> เพื่อยืนยัน:
					</label>
					<input
						id="delete-confirm-input"
						type="text"
						bind:value={deleteConfirmationText}
						placeholder="พิมพ์ DELETE ที่นี่..."
						class="w-full terminal-input rounded-xl px-3.5 py-2 text-xs focus:outline-none focus:ring-1 pr-10 font-mono transition"
						autocomplete="off"
					/>

					{#if clearDbError}
						<p class="text-[11px] text-rose-400 mt-1.5 flex items-center gap-1 font-semibold">
							<svg class="w-3.5 h-3.5 shrink-0" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 8v4m0 4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" /></svg>
							<span>{clearDbError}</span>
						</p>
					{/if}
				</div>

				<div class="flex items-center justify-end gap-2.5 pt-2">
					<button
						type="button"
						onclick={closeClearDbModal}
						class="px-4 py-2 rounded-xl text-xs font-bold text-slate-300 bg-slate-900 border border-[#283348] hover:border-slate-600 hover:text-white transition cursor-pointer uppercase font-mono"
					>
						CANCEL
					</button>
					<button
						type="submit"
						disabled={deleteConfirmationText.trim() !== 'DELETE' || isClearingDb}
						class="px-5 py-2 rounded-xl text-xs font-black text-white bg-gradient-to-r from-rose-600 to-red-600 hover:from-rose-500 hover:to-red-500 active:scale-95 transition shadow-[0_0_15px_rgba(244,63,94,0.35)] disabled:opacity-40 disabled:cursor-not-allowed flex items-center gap-1.5 cursor-pointer uppercase font-mono"
					>
						{#if isClearingDb}
							<div class="w-3.5 h-3.5 border-2 border-white border-t-transparent rounded-full animate-spin"></div>
							<span>PURGING...</span>
						{:else}
							<span>EXECUTE_PURGE</span>
						{/if}
					</button>
				</div>
			</form>
		</div>
	</div>
{/if}

<!-- ══════════════════════════════════════════════════════════════════════════
     POPUP MODAL: RESULT NOTIFICATION
══════════════════════════════════════════════════════════════════════════ -->
{#if clearDbResult && clearDbResult.isOpen}
	<div class="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/85 backdrop-blur-md animate-fade-in font-mono">
		<div class="relative w-full max-w-md chassis-panel border-2 {clearDbResult.isEmpty ? 'border-amber-500/60 shadow-[0_0_50px_rgba(245,158,11,0.25)]' : 'border-emerald-500/60 shadow-[0_0_50px_rgba(16,185,129,0.25)]'} rounded-2xl p-5 space-y-4">
			<div class="absolute top-1.5 left-2 screw-marker">+</div>
			<div class="absolute top-1.5 right-2 screw-marker">+</div>

			<div class="flex items-center gap-3">
				<div class="w-10 h-10 rounded-xl {clearDbResult.isEmpty ? 'bg-amber-500/20 border-amber-500/50 text-amber-400' : 'bg-emerald-500/20 border-emerald-500/50 text-emerald-400'} border flex items-center justify-center shrink-0 shadow-lg">
					{#if clearDbResult.isEmpty}
						<svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
							<path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
						</svg>
					{:else}
						<svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
							<path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 13l4 4L19 7" />
						</svg>
					{/if}
				</div>
				<div>
					<h3 class="text-xs sm:text-sm font-black text-white tracking-wide">{clearDbResult.title}</h3>
					<p class="text-[10px] {clearDbResult.isEmpty ? 'text-amber-300' : 'text-emerald-300'} font-mono">
						{clearDbResult.isEmpty ? 'STATUS: EMPTY_DATABASE' : 'STATUS: SUCCESS_PURGED'}
					</p>
				</div>
			</div>

			<div class="text-xs text-slate-200 bg-[#05080e] p-3 rounded-xl border border-[#283348] space-y-2 leading-relaxed">
				<p class="font-medium text-slate-200 font-sans">
					{clearDbResult.message}
				</p>
				{#if clearDbResult.details}
					<p class="text-[10.5px] text-slate-400 font-mono bg-black/60 p-2 rounded-lg border border-[#283348]">
						{clearDbResult.details}
					</p>
				{/if}
			</div>

			<div class="flex items-center justify-end pt-1">
				<button
					type="button"
					onclick={closeResultModal}
					class="w-full sm:w-auto px-6 py-2 rounded-xl text-xs font-black text-slate-950 {clearDbResult.isEmpty ? 'bg-amber-400 hover:bg-amber-300' : 'bg-emerald-400 hover:bg-emerald-300'} transition shadow-md active:scale-95 cursor-pointer uppercase font-mono"
				>
					ACKNOWLEDGE (OK)
				</button>
			</div>
		</div>
	</div>
{/if}
