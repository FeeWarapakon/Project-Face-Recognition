/**
 * Web Audio API Synthesizer for Face Recognition System
 * Zero latency, crystal-clear synthetic chimes without external mp3 dependencies.
 */

let audioCtx: AudioContext | null = null;

export function getAudioContext(): AudioContext | null {
	if (typeof window === 'undefined') return null;
	if (!audioCtx) {
		const AudioContextClass =
			window.AudioContext ||
			(window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
		if (AudioContextClass) {
			audioCtx = new AudioContextClass();
		}
	}
	if (audioCtx && audioCtx.state === 'suspended') {
		audioCtx.resume().catch(() => {});
	}
	return audioCtx;
}

// Auto-unlock Web Audio on first user interaction anywhere on the screen
if (typeof window !== 'undefined') {
	const unlockHandler = () => {
		const ctx = getAudioContext();
		if (ctx && ctx.state === 'suspended') {
			ctx.resume().catch(() => {});
		}
	};
	['click', 'touchstart', 'pointerdown', 'keydown', 'mousedown'].forEach((ev) => {
		window.addEventListener(ev, unlockHandler, { passive: true, once: false });
	});
}

export type SoundType = 'cyber_ping' | 'welcome_bell' | 'access_granted';

export const SOUND_OPTIONS: { id: SoundType; name: string; desc: string; icon: string }[] = [
	{
		id: 'cyber_ping',
		name: 'Cyber Ping (Sci-Fi Beep)',
		desc: 'เสียงบี๊บสังเคราะห์สไตล์ Cyberpunk ความถี่สูง คมชัด',
		icon: '⚡'
	},
	{
		id: 'welcome_bell',
		name: 'Welcome Bell (Success Chime)',
		desc: 'เสียงกระดิ่งคอร์ดเมโลดี้คู่เสียง ละมุนและเป็นมิตร',
		icon: '🔔'
	},
	{
		id: 'access_granted',
		name: 'Access Granted (Tech Tone)',
		desc: 'เสียง Tri-tone ยืนยันสิทธิ์อนุญาตผ่านประตูไฮเทค',
		icon: '🛡️'
	}
];

function _executeSynth(ctx: AudioContext, soundType: SoundType, volumePercent: number) {
	const masterGain = ctx.createGain();
	// Punchy, clear volume scaling (up to 0.9)
	const vol = Math.max(0.05, Math.min(1.0, (volumePercent / 100) * 0.85));
	masterGain.gain.setValueAtTime(vol, ctx.currentTime);
	masterGain.connect(ctx.destination);

	const now = ctx.currentTime;

	if (soundType === 'cyber_ping') {
		// Fast sci-fi sine chirp + crisp high overtone
		const osc = ctx.createOscillator();
		const gain = ctx.createGain();
		osc.type = 'sine';
		osc.frequency.setValueAtTime(900, now);
		osc.frequency.exponentialRampToValueAtTime(1800, now + 0.09);

		gain.gain.setValueAtTime(1, now);
		gain.gain.exponentialRampToValueAtTime(0.001, now + 0.28);

		osc.connect(gain);
		gain.connect(masterGain);

		osc.start(now);
		osc.stop(now + 0.3);

	} else if (soundType === 'welcome_bell') {
		// Dual harmonic bell chime (E5 659Hz -> B5 987Hz)
		const notes = [659.25, 987.77];
		notes.forEach((freq, i) => {
			const osc = ctx.createOscillator();
			const gain = ctx.createGain();
			osc.type = 'triangle';
			osc.frequency.setValueAtTime(freq, now + i * 0.09);

			gain.gain.setValueAtTime(0, now);
			gain.gain.setValueAtTime(0.9, now + i * 0.09);
			gain.gain.exponentialRampToValueAtTime(0.001, now + i * 0.09 + 0.48);

			osc.connect(gain);
			gain.connect(masterGain);

			osc.start(now + i * 0.09);
			osc.stop(now + i * 0.09 + 0.5);
		});

	} else if (soundType === 'access_granted') {
		// Tech ascending triad (C5 523Hz -> E5 659Hz -> C6 1046Hz)
		const triad = [523.25, 659.25, 1046.5];
		triad.forEach((freq, idx) => {
			const osc = ctx.createOscillator();
			const gain = ctx.createGain();
			const tStart = now + idx * 0.07;
			osc.type = 'sine';
			osc.frequency.setValueAtTime(freq, tStart);

			gain.gain.setValueAtTime(0.85, tStart);
			gain.gain.exponentialRampToValueAtTime(0.001, tStart + 0.25);

			osc.connect(gain);
			gain.connect(masterGain);

			osc.start(tStart);
			osc.stop(tStart + 0.26);
		});
	}
}

export function playSound(soundType: SoundType = 'cyber_ping', volumePercent: number = 80) {
	try {
		const ctx = getAudioContext();
		if (!ctx) return;

		if (ctx.state === 'suspended') {
			ctx.resume()
				.then(() => {
					_executeSynth(ctx, soundType, volumePercent);
				})
				.catch(() => {
					_executeSynth(ctx, soundType, volumePercent);
				});
		} else {
			_executeSynth(ctx, soundType, volumePercent);
		}
	} catch (e) {
		console.warn('Audio playback error:', e);
	}
}

/**
 * High-tech menu click sound (Crisp optic pulse)
 */
export function playMenuClickSound() {
	try {
		const ctx = getAudioContext();
		if (!ctx) return;
		const now = ctx.currentTime;
		
		const osc = ctx.createOscillator();
		const gain = ctx.createGain();
		osc.type = 'sine';
		osc.frequency.setValueAtTime(1400, now);
		osc.frequency.exponentialRampToValueAtTime(800, now + 0.04);
		
		gain.gain.setValueAtTime(0.08, now);
		gain.gain.exponentialRampToValueAtTime(0.001, now + 0.05);
		
		osc.connect(gain);
		gain.connect(ctx.destination);
		osc.start(now);
		osc.stop(now + 0.06);
	} catch (e) {}
}

/**
 * Tactile Button Press Sound (Satisfying micro-switch actuation)
 */
export function playButtonPressSound() {
	try {
		const ctx = getAudioContext();
		if (!ctx) return;
		const now = ctx.currentTime;
		
		const osc = ctx.createOscillator();
		const gain = ctx.createGain();
		osc.type = 'triangle';
		osc.frequency.setValueAtTime(450, now);
		osc.frequency.exponentialRampToValueAtTime(120, now + 0.06);
		
		gain.gain.setValueAtTime(0.12, now);
		gain.gain.exponentialRampToValueAtTime(0.001, now + 0.07);
		
		osc.connect(gain);
		gain.connect(ctx.destination);
		osc.start(now);
		osc.stop(now + 0.08);
	} catch (e) {}
}

/**
 * Toggle Switch Sound (Dual tone click)
 */
export function playToggleSound(nextState?: boolean) {
	try {
		const ctx = getAudioContext();
		if (!ctx) return;
		const now = ctx.currentTime;
		
		const osc = ctx.createOscillator();
		const gain = ctx.createGain();
		osc.type = 'sine';
		const startFreq = nextState === false ? 600 : 350;
		const endFreq = nextState === false ? 300 : 850;
		
		osc.frequency.setValueAtTime(startFreq, now);
		osc.frequency.exponentialRampToValueAtTime(endFreq, now + 0.07);
		
		gain.gain.setValueAtTime(0.09, now);
		gain.gain.exponentialRampToValueAtTime(0.001, now + 0.08);
		
		osc.connect(gain);
		gain.connect(ctx.destination);
		osc.start(now);
		osc.stop(now + 0.09);
	} catch (e) {}
}

/**
 * Camera Shutter / Snapshot capture sound
 */
export function playSnapshotSound() {
	try {
		const ctx = getAudioContext();
		if (!ctx) return;
		const now = ctx.currentTime;
		
		// 1. Shutter Click
		const osc = ctx.createOscillator();
		const gain = ctx.createGain();
		osc.type = 'triangle';
		osc.frequency.setValueAtTime(180, now);
		osc.frequency.exponentialRampToValueAtTime(40, now + 0.08);
		gain.gain.setValueAtTime(0.2, now);
		gain.gain.exponentialRampToValueAtTime(0.001, now + 0.09);
		osc.connect(gain);
		gain.connect(ctx.destination);
		osc.start(now);
		osc.stop(now + 0.1);
		
		// 2. High flash beep
		const osc2 = ctx.createOscillator();
		const gain2 = ctx.createGain();
		osc2.type = 'sine';
		osc2.frequency.setValueAtTime(1760, now + 0.04);
		gain2.gain.setValueAtTime(0.07, now + 0.04);
		gain2.gain.exponentialRampToValueAtTime(0.001, now + 0.18);
		osc2.connect(gain2);
		gain2.connect(ctx.destination);
		osc2.start(now + 0.04);
		osc2.stop(now + 0.19);
	} catch (e) {}
}

/**
 * Amber Warning / Spoof alert tone
 */
export function playWarningBeep() {
	try {
		const ctx = getAudioContext();
		if (!ctx) return;
		const now = ctx.currentTime;
		
		const osc = ctx.createOscillator();
		const gain = ctx.createGain();
		osc.type = 'sawtooth';
		osc.frequency.setValueAtTime(320, now);
		osc.frequency.setValueAtTime(440, now + 0.08);
		
		gain.gain.setValueAtTime(0.12, now);
		gain.gain.exponentialRampToValueAtTime(0.001, now + 0.22);
		
		osc.connect(gain);
		gain.connect(ctx.destination);
		osc.start(now);
		osc.stop(now + 0.24);
	} catch (e) {}
}
