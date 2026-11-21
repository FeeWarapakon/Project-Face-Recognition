<script lang="ts">
  import { playSnapshotSound, playButtonPressSound, playToggleSound } from '$lib/audio';

  // Form states
  let personName = $state('');
  let studentId = $state('');
  let role = $state('นักศึกษา');
  let isSubmitting = $state(false);

  // Floating Toast Notification (Zero Layout Shift, Auto-dismisses in 2.5s)
  let toast = $state<{ show: boolean; text: string; type: 'success' | 'error' | 'info' }>({
    show: false,
    text: '',
    type: 'info'
  });
  let toastTimer: any = null;

  function showToast(text: string, type: 'success' | 'error' | 'info' = 'info', duration = 2500) {
    if (toastTimer) clearTimeout(toastTimer);
    toast = { show: true, text, type };
    toastTimer = setTimeout(() => {
      toast.show = false;
    }, duration);
  }

  // Camera Management States (2 Clean Working Options)
  let selectedCameraOption = $state<'__raw_backend__' | '__ai_backend__'>('__raw_backend__');
  let videoFeedElement: HTMLImageElement | null = $state(null);
  let cameraLoadError = $state(false);
  let cameraStreamSrc = $state('/api/raw_video_feed');
  let captureFlash = $state(false);

  // 3 Image Slots
  interface ImageSlot {
    id: number;
    title: string;
    subtitle: string;
    dataUrl: string | null;
    sourceType: 'camera' | 'upload' | null;
  }

  let slots = $state<ImageSlot[]>([
    { id: 1, title: 'รูปที่ 1', subtitle: 'หน้าตรง (Front Angle)', dataUrl: null, sourceType: null },
    { id: 2, title: 'รูปที่ 2', subtitle: 'เอียงข้าง (Slight Turn)', dataUrl: null, sourceType: null },
    { id: 3, title: 'รูปที่ 3', subtitle: 'ยิ้ม / หน้าตรงเสริม', dataUrl: null, sourceType: null }
  ]);

  let activeSlotIndex = $state(0);

  // Switch camera stream mode (2 options)
  function handleCameraChange(newOption: string) {
    playToggleSound();
    selectedCameraOption = newOption as '__raw_backend__' | '__ai_backend__';
    cameraLoadError = false;
    if (newOption === '__raw_backend__') {
      cameraStreamSrc = `/api/raw_video_feed?t=${Date.now()}`;
    } else {
      cameraStreamSrc = `/api/video_feed?t=${Date.now()}`;
    }
  }

  // Capture frame from live camera to fill the next available slot
  async function captureFromCamera() {
    playSnapshotSound();
    captureFlash = true;
    setTimeout(() => captureFlash = false, 250);

    // 1. Direct high-quality capture from backend API (Original Clean Frame)
    try {
      const res = await fetch('/api/capture_frame');
      if (res.ok) {
        const data = await res.json();
        if (data.success && data.image_base64) {
          fillSlot(data.image_base64, 'camera');
          return;
        }
      }
    } catch (e) {}

    // 2. Fallback: capture from video feed element in DOM
    const canvas = document.createElement('canvas');
    canvas.width = 640;
    canvas.height = 480;
    const ctx = canvas.getContext('2d');

    if (ctx && videoFeedElement && videoFeedElement.complete && videoFeedElement.naturalWidth > 0) {
      try {
        ctx.drawImage(videoFeedElement, 0, 0, canvas.width, canvas.height);
        const capturedBase64 = canvas.toDataURL('image/jpeg', 0.92);
        fillSlot(capturedBase64, 'camera');
        return;
      } catch (e) {
        console.warn('Canvas drawImage error:', e);
      }
    }

    showToast('ไม่สามารถบันทึกภาพจากกล้องได้ กรุณาตรวจสอบการเชื่อมต่อกล้อง', 'error');
  }

  // Auto fill next available slot or active slot
  function fillSlot(dataUrl: string, type: 'camera' | 'upload') {
    const emptyIndex = slots.findIndex(s => !s.dataUrl);
    const targetIndex = emptyIndex !== -1 ? emptyIndex : activeSlotIndex;

    slots[targetIndex].dataUrl = dataUrl;
    slots[targetIndex].sourceType = type;

    // Move active pointer to next slot
    activeSlotIndex = (targetIndex + 1) % slots.length;
    showToast(`บันทึกภาพลงใน "${slots[targetIndex].title}" เรียบร้อย`, 'success', 2200);
  }

  // Handle single slot file upload
  function handleSlotFileUpload(event: Event, slotId: number) {
    const target = event.target as HTMLInputElement;
    const file = target.files?.[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = (e) => {
      const b64 = e.target?.result as string;
      const index = slots.findIndex(s => s.id === slotId);
      if (index !== -1) {
        slots[index].dataUrl = b64;
        slots[index].sourceType = 'upload';
        showToast(`อัปโหลดไฟล์ลงใน "${slots[index].title}" เรียบร้อย`, 'info', 2200);
      }
    };
    reader.readAsDataURL(file);
  }

  // Clear single slot
  function clearSlot(slotId: number) {
    playButtonPressSound();
    const index = slots.findIndex(s => s.id === slotId);
    if (index !== -1) {
      slots[index].dataUrl = null;
      slots[index].sourceType = null;
    }
  }

  // Reset all fields & clear all slots ready for next person
  function resetForm() {
    playButtonPressSound();
    personName = '';
    studentId = '';
    role = 'นักศึกษา';
    slots.forEach(s => {
      s.dataUrl = null;
      s.sourceType = null;
    });
    activeSlotIndex = 0;
    showToast('ล้างข้อมูลเรียบร้อย พร้อมเพิ่มคนใหม่', 'info', 1800);
  }

  // Save Data Button
  async function saveData() {
    playButtonPressSound();
    if (!personName.trim()) {
      showToast('กรุณากรอกชื่อ-นามสกุลก่อนบันทึกข้อมูล', 'error', 3000);
      return;
    }

    const filledSlots = slots.filter(s => s.dataUrl !== null);
    if (filledSlots.length === 0) {
      showToast('กรุณาถ่ายภาพจากกล้องหรืออัปโหลดรูปภาพอย่างน้อย 1 ภาพ', 'error', 3000);
      return;
    }

    const imagesBase64 = filledSlots.map(s => s.dataUrl as string);

    isSubmitting = true;
    showToast(`กำลังสกัดเวกเตอร์ใบหน้า ${filledSlots.length} รูป และบันทึกลง SQLite...`, 'info', 3500);

    try {
      const res = await fetch('/api/register_face', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          name: personName.trim(),
          student_id: studentId.trim(),
          role: role,
          images: imagesBase64,
          image_base64: imagesBase64[0]
        })
      });

      const data = await res.json();
      if (res.ok && data.success) {
        showToast(data.message || `บันทึกข้อมูล '${personName}' และรูปภาพลงใน known_faces เรียบร้อย!`, 'success', 3500);

        // Clear values ready for next person
        setTimeout(() => {
          resetForm();
        }, 1500);
      } else {
        showToast(data.error || 'เกิดข้อผิดพลาดในการลงทะเบียน', 'error', 3500);
      }
    } catch (err) {
      showToast('ไม่สามารถเชื่อมต่อ Server Backend ได้ กรุณาตรวจสอบว่ารัน py 3_realtime_recognition.py อยู่', 'error', 4000);
    } finally {
      isSubmitting = false;
    }
  }
</script>

<svelte:head>
  <title>Register Face | ลงทะเบียนใบหน้า</title>
</svelte:head>

<!-- Floating Toast Popup Notification -->
{#if toast.show}
  <div class="fixed top-5 left-1/2 -translate-x-1/2 z-50 pointer-events-none transition-all duration-300 transform scale-100 opacity-100 font-mono">
    <div class="px-4 py-2 rounded-xl border-2 shadow-2xl backdrop-blur-md flex items-center gap-2.5 text-xs font-bold {
      toast.type === 'success' ? 'bg-emerald-950/95 border-emerald-500 text-emerald-200 shadow-[0_0_20px_rgba(16,185,129,0.3)]' :
      toast.type === 'error' ? 'bg-rose-950/95 border-rose-500 text-rose-200 shadow-[0_0_20px_rgba(244,63,94,0.3)]' :
      'bg-amber-950/95 border-amber-500 text-amber-200 shadow-[0_0_20px_rgba(245,158,11,0.3)]'
    }">
      {#if toast.type === 'success'}
        <span class="text-emerald-400 font-black">✓</span>
      {:else if toast.type === 'error'}
        <span class="text-rose-400 font-black">✕</span>
      {:else}
        <span class="text-amber-400 font-black">ℹ</span>
      {/if}
      <span class="tracking-wide font-sans">{toast.text}</span>
    </div>
  </div>
{/if}

<div class="h-full flex flex-col select-none w-full gap-2.5 overflow-y-auto custom-scrollbar pr-0.5">
  
  <!-- Header Title & Quick Actions Bar (Heavy Chassis Panel) -->
  <div class="chassis-panel px-3.5 py-2 rounded-xl flex items-center justify-between gap-3 shrink-0 relative">
    <div class="absolute top-1.5 left-2 screw-marker">+</div>
    <div class="absolute top-1.5 right-2 screw-marker">+</div>

    <div class="flex items-center gap-2.5 min-w-0 pl-3">
      <div class="w-8 h-8 rounded-lg bg-amber-500/20 border border-amber-500/40 text-amber-400 flex items-center justify-center shadow-[0_0_10px_rgba(245,158,11,0.25)] shrink-0">
        <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M18 9v3m0 0v3m0-3h3m-3 0h-3m-2-5a4 4 0 11-8 0 4 4 0 018 0zM3 20a6 6 0 0112 0v1H3v-1z" />
        </svg>
      </div>
      <div class="min-w-0">
        <h2 class="text-xs sm:text-sm font-black text-amber-300 tracking-wide font-mono flex items-center gap-2 truncate">
          <span>ENROLLMENT_DECK // ลงทะเบียนใบหน้า</span>
          <span class="text-[9px] font-mono text-amber-400 bg-amber-950/80 px-1.5 py-0.2 rounded border border-amber-500/40">
            3-SLOT MULTI-ANGLE
          </span>
        </h2>
        <p class="text-[10px] text-slate-400 leading-tight truncate">
          ถ่ายภาพ 3 มุมมอง สกัดเวกเตอร์ 512-D MobileFaceNet บันทึกลง SQLite
        </p>
      </div>
    </div>

    <!-- Clear / Reset Form Button -->
    <button
      onclick={resetForm}
      class="px-2.5 py-1 rounded-lg bg-[#020406] hover:bg-[#161c28] border border-amber-500/30 hover:border-amber-400 text-amber-300 text-xs font-mono font-bold flex items-center gap-1.5 transition shadow cursor-pointer active:scale-95 whitespace-nowrap shrink-0 pr-3"
    >
      <span>[ RESET / ล้างฟอร์ม ]</span>
    </button>
  </div>

  <!-- Main 2-Column Responsive Grid -->
  <div class="grid grid-cols-1 lg:grid-cols-12 gap-2.5 flex-1 min-h-0 items-stretch">
    
    <!-- Left Column: Camera Viewfinder & Capture Action (Col Span 5) -->
    <div class="lg:col-span-5 chassis-panel p-2.5 sm:p-3 rounded-2xl flex flex-col justify-between min-h-[300px] lg:h-full space-y-2 relative">
      <div class="absolute top-1.5 left-2 screw-marker">+</div>
      <div class="absolute top-1.5 right-2 screw-marker">+</div>

      <div class="flex-1 flex flex-col min-h-0">
        <!-- Top Status Bar & Camera Switcher Dropdown -->
        <div class="flex items-center justify-between mb-1.5 shrink-0 gap-2 font-mono">
          <span class="text-[11px] font-bold tracking-wider text-amber-300 flex items-center gap-1.5 shrink-0">
            <span class="w-2 h-2 rounded-full bg-emerald-400 animate-pulse shadow-[0_0_6px_#10b981]"></span>
            <span>VIEWFINDER // V4L2</span>
          </span>

          <!-- Clean Camera Mode Indicator -->
          <span class="text-[10px] font-mono text-emerald-400 bg-emerald-950/80 px-2 py-0.5 rounded border border-emerald-500/40 flex items-center gap-1 shrink-0">
            📷 RAW_CAMERA (ภาพถ่ายต้นฉบับ ไม่มีกรอบ)
          </span>
        </div>

        <!-- Camera Viewport (Clean, Unobstructed Camera View) -->
        <div class="relative flex-1 min-h-[200px] sm:min-h-[240px] terminal-screen rounded-xl flex items-center justify-center border-2 border-[#283348] overflow-hidden group">
          <!-- Corner Viewfinder Targets -->
          <div class="absolute top-2.5 left-2.5 w-3.5 h-3.5 border-t-2 border-l-2 border-amber-400 z-20 pointer-events-none"></div>
          <div class="absolute top-2.5 right-2.5 w-3.5 h-3.5 border-t-2 border-r-2 border-amber-400 z-20 pointer-events-none"></div>
          <div class="absolute bottom-2.5 left-2.5 w-3.5 h-3.5 border-b-2 border-l-2 border-amber-400 z-20 pointer-events-none"></div>
          <div class="absolute bottom-2.5 right-2.5 w-3.5 h-3.5 border-b-2 border-r-2 border-amber-400 z-20 pointer-events-none"></div>

          {#if captureFlash}
            <div class="absolute inset-0 bg-white z-30 animate-fade-out pointer-events-none"></div>
          {/if}

          <!-- Live Camera Stream -->
          <img
            bind:this={videoFeedElement}
            src={cameraStreamSrc}
            alt="Registration Camera Feed"
            class="w-full h-full object-cover object-center relative z-10"
            onerror={() => {
              if (cameraStreamSrc.includes('raw_video_feed')) {
                cameraStreamSrc = '/api/video_feed';
              } else {
                cameraLoadError = true;
              }
            }}
          />

          {#if cameraLoadError}
            <div class="absolute inset-0 bg-[#020406]/95 z-20 flex flex-col items-center justify-center text-slate-500 font-mono space-y-2 p-4 text-center">
              <div class="text-amber-400 text-xs font-bold">[ NO CAMERA SIGNAL ]</div>
              <p class="text-[10px] text-slate-400">กรุณาตรวจสอบว่าเซิร์ฟเวอร์กล้องเปิดอยู่</p>
            </div>
          {/if}

          <!-- Corner Cyber Brackets -->
          <div class="absolute top-2 left-2 w-3.5 h-3.5 border-t-2 border-l-2 border-amber-400/80 z-20 pointer-events-none"></div>
          <div class="absolute top-2 right-2 w-3.5 h-3.5 border-t-2 border-r-2 border-amber-400/80 z-20 pointer-events-none"></div>
          <div class="absolute bottom-2 left-2 w-3.5 h-3.5 border-b-2 border-l-2 border-amber-400/80 z-20 pointer-events-none"></div>
          <div class="absolute bottom-2 right-2 w-3.5 h-3.5 border-b-2 border-r-2 border-amber-400/80 z-20 pointer-events-none"></div>
        </div>
      </div>

      <!-- Large Capture Button (Golden Amber Shutter Button) -->
      <div class="shrink-0 pt-0.5">
        <button
          onclick={captureFromCamera}
          class="w-full py-2.5 px-4 rounded-xl bg-gradient-to-r from-amber-500 via-amber-400 to-yellow-500 hover:from-amber-400 hover:to-yellow-400 text-slate-950 font-black text-xs sm:text-sm tracking-wider uppercase flex items-center justify-center gap-2 shadow-[0_0_20px_rgba(245,158,11,0.35)] active:scale-[0.98] transition-all cursor-pointer font-mono"
        >
          <svg class="w-4 h-4 text-slate-950 shrink-0" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2.2" d="M3 9a2 2 0 012-2h.93a2 2 0 001.664-.89l.812-1.22A2 2 0 0110.07 4h3.86a2 2 0 011.664.89l.812 1.22A2 2 0 0018.07 7H19a2 2 0 012 2v9a2 2 0 01-2 2H5a2 2 0 01-2-2V9z" />
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2.2" d="M15 13a3 3 0 11-6 0 3 3 0 016 0z" />
          </svg>
          <span>CAPTURE_FRAME // ถ่ายภาพลงช่อง</span>
        </button>
      </div>
    </div>

    <!-- Right Column: 3 Image Slots & Form Input Fields (Col Span 7) -->
    <div class="lg:col-span-7 flex flex-col justify-between gap-2.5 min-h-0">
      
      <!-- 1. 3 Image Slots Cartridge Array -->
      <div class="chassis-panel p-2.5 rounded-2xl flex-1 min-h-[120px] flex flex-col justify-between space-y-1.5 relative">
        <div class="absolute top-1.5 left-2 screw-marker">+</div>
        <div class="absolute top-1.5 right-2 screw-marker">+</div>

        <div class="flex items-center justify-between shrink-0 pl-3 font-mono">
          <h3 class="text-[11px] font-bold uppercase tracking-wider text-amber-300 flex items-center gap-1.5">
            <span>&gt;&gt; BIOMETRIC_CARTRIDGES // 3 ช่องภาพ</span>
          </h3>
          <span class="text-[10px] font-bold text-amber-400 bg-amber-950/80 px-2 py-0.2 rounded border border-amber-500/30">
            {slots.filter(s => s.dataUrl).length} / 3 SLOTS
          </span>
        </div>

        <!-- 3 Slots Grid -->
        <div class="grid grid-cols-3 gap-2 flex-1 min-h-[80px]">
          {#each slots as slot (slot.id)}
            <div class="relative bg-[#020406] p-1.5 rounded-xl border {slot.dataUrl ? 'border-amber-400/80 shadow-[0_0_12px_rgba(245,158,11,0.2)]' : 'border-[#1c2433]'} flex flex-col justify-between items-center text-center space-y-1 group transition-all duration-200 h-full min-h-0">
              
              <!-- Slot Header Badge -->
              <div class="w-full flex items-center justify-between text-[10px] font-mono shrink-0 leading-none">
                <span class="font-bold text-amber-300 flex items-center gap-1 truncate">
                  <span class="w-1.5 h-1.5 rounded-full {slot.dataUrl ? 'bg-amber-400' : 'bg-slate-700'} shrink-0"></span>
                  SLOT_{slot.id}
                </span>
                {#if slot.dataUrl}
                  <span class="px-1 py-0.2 rounded text-[8px] font-mono font-bold bg-amber-500/20 text-amber-300 border border-amber-500/40 shrink-0">
                    OK
                  </span>
                {:else}
                  <span class="text-slate-600 text-[8.5px] font-mono shrink-0">EMPTY</span>
                {/if}
              </div>

              <!-- Slot Preview Box / Upload Trigger -->
              <div class="relative w-full flex-1 min-h-[45px] rounded-lg bg-[#05080f] border border-dashed {slot.dataUrl ? 'border-amber-500/40' : 'border-slate-800 hover:border-amber-400/80'} overflow-hidden flex items-center justify-center transition-all">
                {#if slot.dataUrl}
                  <img src={slot.dataUrl} alt={slot.title} class="absolute inset-0 w-full h-full object-cover" />
                  
                  <!-- Delete button -->
                  <button
                    onclick={() => clearSlot(slot.id)}
                    class="absolute top-1 right-1 z-10 w-4.5 h-4.5 rounded bg-rose-600/90 hover:bg-rose-500 text-white flex items-center justify-center text-[10px] font-mono font-black shadow transition active:scale-90"
                    title="ลบภาพนี้"
                  >
                    ✕
                  </button>
                {:else}
                  <label class="w-full h-full flex flex-col items-center justify-center cursor-pointer p-1 hover:bg-white/[0.02] transition group/label">
                    <span class="text-[9px] text-slate-500 font-mono group-hover/label:text-amber-300">+ UPLOAD</span>
                    <input
                      type="file"
                      accept="image/*"
                      class="hidden"
                      onchange={(e) => handleSlotFileUpload(e, slot.id)}
                    />
                  </label>
                {/if}
              </div>

              <!-- Slot Subtitle Hint -->
              <span class="text-[8px] text-slate-500 truncate w-full font-mono shrink-0 leading-none">{slot.subtitle}</span>
            </div>
          {/each}
        </div>
      </div>

      <!-- 2. Person Information Form (Heavy Industrial Panel) -->
      <div class="chassis-panel p-2.5 sm:p-3 rounded-2xl flex-1 flex flex-col justify-between space-y-2 relative">
        <div class="absolute top-1.5 left-2 screw-marker">+</div>
        <div class="absolute top-1.5 right-2 screw-marker">+</div>

        <div class="space-y-1.5 pl-3">
          <div class="flex items-center justify-between font-mono">
            <h3 class="text-xs font-bold uppercase tracking-wider text-amber-300 flex items-center gap-1.5 truncate">
              <span>&gt;&gt; IDENTITY_METADATA // ข้อมูลบุคคล</span>
            </h3>
            <span class="text-[9px] font-mono font-bold text-emerald-400 bg-emerald-950/80 px-2 py-0.2 rounded border border-emerald-500/30">
              ● SQLITE_READY
            </span>
          </div>

          <!-- 3 Stacked Input Fields (Terminal Inputs) -->
          <div class="space-y-1.5">
            <!-- 1. Full Name -->
            <div class="space-y-0.5">
              <label for="person-name" class="block font-mono font-bold text-amber-300/90 text-[10.5px]">
                FULL_NAME &gt; <span class="text-rose-400">*</span>
              </label>
              <input
                id="person-name"
                type="text"
                bind:value={personName}
                placeholder="กรอกชื่อ-นามสกุล..."
                class="w-full px-3 py-1.5 rounded-lg terminal-input text-xs transition"
              />
            </div>

            <!-- 2. Student / Employee ID -->
            <div class="space-y-0.5">
              <label for="student-id" class="block font-mono font-bold text-amber-300/90 text-[10.5px]">
                STUDENT_ID &gt;
              </label>
              <input
                id="student-id"
                type="text"
                bind:value={studentId}
                placeholder="ระบุรหัสประจำตัว (ถ้ามี)..."
                class="w-full px-3 py-1.5 rounded-lg terminal-input text-xs transition"
              />
            </div>

            <!-- 3. Role / Department -->
            <div class="space-y-0.5">
              <label for="person-role" class="block font-mono font-bold text-amber-300/90 text-[10.5px]">
                ROLE_DEPARTMENT &gt;
              </label>
              <select
                id="person-role"
                bind:value={role}
                class="w-full px-3 py-1.5 rounded-lg terminal-input text-xs transition cursor-pointer"
              >
                <option value="นักศึกษา">นักศึกษา</option>
                <option value="อาจารย์/บุคลากร">อาจารย์/บุคลากร</option>
                <option value="นักพัฒนาระบบ">นักพัฒนาระบบ</option>
                <option value="พนักงานทดลองระบบ">พนักงานทดลองระบบ</option>
                <option value="เทพซ่า007">เทพซ่า007</option>
              </select>
            </div>
          </div>
        </div>

        <!-- Large EXECUTE ENROLLMENT Action Button -->
        <div class="pt-0.5 shrink-0">
          <button
            onclick={saveData}
            disabled={isSubmitting}
            class="w-full py-2.5 px-4 rounded-xl bg-gradient-to-r from-emerald-500 via-emerald-400 to-teal-400 hover:from-emerald-400 hover:to-teal-300 text-slate-950 font-black text-xs sm:text-sm tracking-wider uppercase flex items-center justify-center gap-2 shadow-[0_0_20px_rgba(16,185,129,0.35)] active:scale-[0.98] transition-all cursor-pointer disabled:opacity-50 font-mono"
          >
            {#if isSubmitting}
              <span>ENROLLING_TENSORS // กำลังสกัดเวกเตอร์...</span>
            {:else}
              <span>EXECUTE_ENROLLMENT // บันทึกข้อมูลใบหน้า</span>
            {/if}
          </button>
        </div>
      </div>
    </div>
  </div>
</div>