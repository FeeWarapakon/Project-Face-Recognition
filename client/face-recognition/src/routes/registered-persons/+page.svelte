<script lang="ts">
  import { onMount } from 'svelte';
  import { playMenuClickSound, playButtonPressSound, playWarningBeep } from '$lib/audio';

  interface Person {
    person_id: number;
    name: string;
    student_id: string;
    role: string;
    embedding_count: number;
    updated_at: string;
  }

  interface EditLog {
    id?: number;
    person_id?: number;
    action: string;
    details?: string;
    timestamp: string;
  }

  let searchQuery = $state('');
  let persons = $state<Person[]>([]);
  let selectedPerson = $state<Person | null>(null);
  let isLoading = $state(true);
  let errorMsg = $state('');

  // Edit History State
  let editHistoryList = $state<EditLog[]>([]);
  let isHistoryLoading = $state(false);

  // Edit Modal State
  let isEditModalOpen = $state(false);
  let editPersonId = $state<number | null>(null);
  let editName = $state('');
  let editStudentId = $state('');
  let editRole = $state('');
  let isSaving = $state(false);
  let modalError = $state('');

  // Toast Notification State
  let toastMessage = $state('');
  let toastType = $state<'success' | 'error'>('success');
  let toastTimer: any = null;

  // Interactive Vacuum Tubes State (User can toggle ON/OFF with sounds)
  let tubesActive = $state<boolean[]>([true, true, true, true]);

  function toggleTube(idx: number) {
    tubesActive[idx] = !tubesActive[idx];
    if (tubesActive[idx]) {
      playButtonPressSound();
    } else {
      playMenuClickSound();
    }
  }

  function showToast(msg: string, type: 'success' | 'error' = 'success') {
    toastMessage = msg;
    toastType = type;
    if (toastTimer) clearTimeout(toastTimer);
    toastTimer = setTimeout(() => {
      toastMessage = '';
    }, 3000);
  }

  async function fetchEditHistory(personId: number) {
    isHistoryLoading = true;
    try {
      const res = await fetch(`/api/persons/${personId}/history`);
      if (res.ok) {
        const data = await res.json();
        if (Array.isArray(data) && data.length > 0) {
          editHistoryList = data.slice(0, 5);
          isHistoryLoading = false;
          return;
        }
      }
    } catch (e) {
      // ignore
    }

    // Fallback to local stored history
    if (typeof window !== 'undefined') {
      const localHistoryKey = `person_history_${personId}`;
      const localRaw = localStorage.getItem(localHistoryKey);
      if (localRaw) {
        try {
          editHistoryList = JSON.parse(localRaw).slice(0, 5);
          isHistoryLoading = false;
          return;
        } catch {}
      }
    }

    // Synthetic fallback from person updated_at
    const currentP = persons.find(x => x.person_id === personId);
    const now = new Date().toLocaleString('th-TH', { hour12: false });
    editHistoryList = [
      {
        action: 'แก้ไขข้อมูลล่าสุด',
        details: `อัปเดตข้อมูลบุคคล (${currentP?.role || 'นักศึกษา'})`,
        timestamp: currentP?.updated_at || now
      },
      {
        action: 'ลงทะเบียนในระบบ',
        details: `บันทึกเวกเตอร์ใบหน้า ${currentP?.embedding_count || 1} ชุด (512-D)`,
        timestamp: currentP?.updated_at || now
      }
    ];
    isHistoryLoading = false;
  }

  async function loadPersons() {
    isLoading = true;
    try {
      const res = await fetch('/api/persons');
      if (res.ok) {
        const data = await res.json();
        persons = data || [];
        if (persons.length > 0) {
          if (!selectedPerson) {
            selectedPerson = persons[0];
            fetchEditHistory(persons[0].person_id);
          } else {
            const current = persons.find(p => p.person_id === selectedPerson!.person_id) || persons[0];
            selectedPerson = current;
            fetchEditHistory(current.person_id);
          }
        }
      } else {
        errorMsg = 'Failed to load persons from database.';
      }
    } catch (e) {
      errorMsg = 'Cannot connect to server.';
    } finally {
      isLoading = false;
    }
  }

  onMount(() => {
    loadPersons();
  });

  let filteredPersons = $derived(
    persons.filter(p =>
      p.name.toLowerCase().includes(searchQuery.toLowerCase()) ||
      (p.student_id && p.student_id.toLowerCase().includes(searchQuery.toLowerCase())) ||
      (p.role && p.role.toLowerCase().includes(searchQuery.toLowerCase()))
    )
  );

  function selectPerson(person: Person) {
    playMenuClickSound();
    selectedPerson = person;
    fetchEditHistory(person.person_id);
  }

  function openEditModal(person: Person) {
    playButtonPressSound();
    editPersonId = person.person_id;
    editName = person.name;
    editStudentId = person.student_id || '';
    editRole = person.role || 'นักศึกษา';
    modalError = '';
    isEditModalOpen = true;
  }

  function closeEditModal() {
    playButtonPressSound();
    isEditModalOpen = false;
    modalError = '';
  }

  async function handleSaveEdit() {
    if (!editName.trim()) {
      playWarningBeep();
      modalError = 'กรุณากรอกชื่อ-นามสกุล';
      return;
    }

    playButtonPressSound();
    isSaving = true;
    modalError = '';

    try {
      const res = await fetch(`/api/persons/${editPersonId}`, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          name: editName.trim(),
          student_id: editStudentId.trim(),
          role: editRole
        })
      });

      const result = await res.json();

      if (res.ok && result.success) {
        showToast('อัปเดตข้อมูลบุคคลสำเร็จ', 'success');

        // Log to local history
        if (typeof window !== 'undefined' && editPersonId) {
          const localHistoryKey = `person_history_${editPersonId}`;
          const nowStr = new Date().toLocaleString('th-TH', { hour12: false });
          const newLog: EditLog = {
            action: 'อัปเดตข้อมูลบุคคล',
            details: `ชื่อ: ${editName} | แผนก: ${editRole}`,
            timestamp: nowStr
          };
          let existing: EditLog[] = [];
          try {
            const raw = localStorage.getItem(localHistoryKey);
            if (raw) existing = JSON.parse(raw);
          } catch {}
          localStorage.setItem(localHistoryKey, JSON.stringify([newLog, ...existing].slice(0, 5)));
        }

        closeEditModal();
        await loadPersons();
        if (selectedPerson) {
          fetchEditHistory(selectedPerson.person_id);
        }
      } else {
        modalError = result.error || 'ไม่สามารถบันทึกข้อมูลได้';
      }
    } catch (err: any) {
      modalError = 'เกิดข้อผิดพลาดในการเชื่อมต่อ Server';
    } finally {
      isSaving = false;
    }
  }

  async function handleDelete(personId: number, name: string) {
    playWarningBeep();
    if (!confirm(`คุณต้องการลบข้อมูลและเวกเตอร์ของ '${name}' ออกจากระบบหรือไม่?`)) {
      return;
    }
    try {
      const res = await fetch(`/api/persons/${personId}`, { method: 'DELETE' });
      if (res.ok) {
        showToast(`ลบข้อมูล '${name}' สำเร็จ`, 'success');
        if (typeof window !== 'undefined') {
          localStorage.removeItem(`person_history_${personId}`);
        }
        await loadPersons();
      } else {
        showToast('เกิดข้อผิดพลาดในการลบข้อมูล', 'error');
      }
    } catch (e) {
      showToast('ไม่สามารถเชื่อมต่อ Server ได้', 'error');
    }
  }
</script>

<svelte:head>
  <title>Personnel Matrix // ข้อมูลผู้ลงทะเบียน</title>
</svelte:head>

<div class="h-full flex flex-col space-y-3 min-h-0 select-none w-full max-w-[1920px] mx-auto overflow-hidden">
  <!-- Toast Notification Popup -->
  {#if toastMessage}
    <div class="fixed top-5 right-5 z-50 animate-bounce transition-all duration-300">
      <div class="flex items-center gap-2.5 px-4 py-2.5 rounded-xl shadow-2xl border backdrop-blur-xl text-xs sm:text-sm font-mono font-bold {toastType === 'success' ? 'bg-[#0a1812]/95 border-emerald-500/70 text-emerald-300 shadow-[0_0_25px_rgba(16,185,129,0.3)]' : 'bg-[#1c0a0e]/95 border-rose-500/70 text-rose-300 shadow-[0_0_25px_rgba(244,63,94,0.3)]'}">
        {#if toastType === 'success'}
          <svg class="w-5 h-5 text-emerald-400 shrink-0" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2.5" d="M9 12l2 2 4-4m6 2a9 9 0 11-18 0 9 9 0 0118 0z" />
          </svg>
        {:else}
          <svg class="w-5 h-5 text-rose-400 shrink-0" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2.5" d="M12 8v4m0 4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
          </svg>
        {/if}
        <span>{toastMessage}</span>
      </div>
    </div>
  {/if}

  <!-- Header & Search Bar (Industrial Gunmetal Chassis) -->
  <div class="relative chassis-panel rounded-2xl p-3 sm:p-3.5 shrink-0 overflow-hidden">
    <!-- Corner Rivet Screws -->
    <div class="absolute top-1.5 left-2 screw-marker">+</div>
    <div class="absolute top-1.5 right-2 screw-marker">+</div>

    <div class="flex flex-col sm:flex-row sm:items-center justify-between gap-3 relative z-10">
      <div class="flex items-center gap-3">
        <div class="w-10 h-10 rounded-xl bg-[#161d2a] border border-[#2d3a50] flex items-center justify-center text-amber-400 shadow-sm shrink-0">
          <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M17 20h5v-2a3 3 0 00-5.356-1.857M17 20H7m10 0v-2c0-.656-.126-1.283-.356-1.857M7 20H2v-2a3 3 0 015.356-1.857M7 20v-2c0-.656.126-1.283.356-1.857m0 0a5.002 5.002 0 019.288 0M15 7a3 3 0 11-6 0 3 3 0 016 0zm6 3a2 2 0 11-4 0 2 2 0 014 0zM7 10a2 2 0 11-4 0 2 2 0 014 0z" />
          </svg>
        </div>
        <div class="min-w-0">
          <div class="flex items-center gap-2">
            <span class="text-[10px] font-mono font-bold text-slate-400 tracking-widest uppercase">&gt;&gt; PERSONNEL_REGISTRY</span>
            <span class="text-[9px] px-2 py-0.5 rounded bg-emerald-950/80 text-emerald-400 border border-emerald-500/40 font-mono font-bold">
              ● SYNC_ACTIVE
            </span>
          </div>
          <h1 class="text-base sm:text-lg font-black text-white tracking-wide flex items-center gap-2 truncate leading-tight mt-0.5">
            ฐานข้อมูลผู้ลงทะเบียน (Database Members)
            <span class="text-xs px-2.5 py-0.5 rounded bg-[#161d2a] text-amber-300 border border-[#2d3a50] font-mono font-bold">
              {persons.length} คน
            </span>
          </h1>
        </div>
      </div>

      <!-- Search & Register Button -->
      <div class="flex items-center gap-2.5 min-w-0">
        <div class="relative flex-1 sm:w-80">
          <span class="absolute left-3 top-1/2 -translate-y-1/2 font-mono text-slate-400 text-xs font-bold pointer-events-none">ID &gt;</span>
          <input
            type="text"
            bind:value={searchQuery}
            placeholder="ค้นหาชื่อ, รหัสนักศึกษา หรือตำแหน่ง..."
            class="w-full bg-[#03060a] border border-[#2a364a] focus:border-amber-400/80 text-slate-100 pl-11 pr-3 py-2 rounded-xl text-xs sm:text-sm placeholder-slate-500 font-mono transition outline-none shadow-inner"
          />
        </div>

        <a
          href="/register-face"
          onclick={() => playButtonPressSound()}
          class="bg-[#1e293b] hover:bg-[#2b3952] border border-amber-500/40 hover:border-amber-400 text-amber-300 hover:text-white px-3.5 py-2 rounded-xl text-xs sm:text-sm font-bold shadow-md flex items-center gap-1.5 transition whitespace-nowrap shrink-0 active:scale-95 cursor-pointer uppercase tracking-wider font-mono"
        >
          <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2.5" d="M12 4v16m8-8H4" />
          </svg>
          <span>+ ENROLL_NEW</span>
        </a>
      </div>
    </div>
  </div>

  <!-- Main Responsive Grid Layout (Auto-adapts on compact windows with smooth overflow) -->
  <div class="grid grid-cols-1 lg:grid-cols-12 gap-3 flex-1 min-h-0 overflow-y-auto lg:overflow-hidden">

    <!-- LEFT COLUMN: Table of Registered Persons (7 cols) -->
    <div class="lg:col-span-7 chassis-panel rounded-2xl flex flex-col min-h-[300px] lg:min-h-0 overflow-hidden relative">
      <!-- Corner Rivet Screws -->
      <div class="absolute top-1.5 left-2 screw-marker">+</div>
      <div class="absolute top-1.5 right-2 screw-marker">+</div>

      <!-- Table Header Bar -->
      <div class="px-3.5 py-2 border-b border-[#283348] flex items-center justify-between shrink-0 bg-[#0a0d14]">
        <div class="flex items-center gap-2">
          <span class="w-2 h-2 rounded-full bg-amber-400 animate-pulse shadow-[0_0_6px_#f59e0b]"></span>
          <h2 class="text-xs font-mono font-bold uppercase tracking-wider text-amber-300/90">&gt;&gt; PERSONNEL_MATRIX_INDEX</h2>
        </div>
        <span class="text-[11px] text-slate-400 font-mono">REC: <span class="text-amber-300 font-bold">{filteredPersons.length}</span> / {persons.length}</span>
      </div>

      <!-- Responsive Auto-Fitting Table Container -->
      <div class="overflow-x-auto overflow-y-auto flex-1 min-h-0 terminal-screen custom-scrollbar">
        <table class="w-full text-left table-auto">
          <thead class="bg-[#060910] text-slate-400 uppercase font-mono text-[10px] sm:text-[11px] border-b border-[#1f2838] sticky top-0 z-10">
            <tr>
              <th class="py-2 px-2 sm:px-3 w-12 sm:w-14 font-black whitespace-nowrap text-amber-400/90"># ID</th>
              <th class="py-2 px-2 sm:px-3 font-bold text-slate-300">ชื่อ - นามสกุล</th>
              <th class="py-2 px-2 sm:px-2.5 font-bold text-slate-300">รหัสประจำตัว</th>
              <th class="py-2 px-2 sm:px-2.5 font-bold text-slate-300 hidden sm:table-cell">แผนก / ตำแหน่ง</th>
              <th class="py-2 px-1.5 sm:px-2 text-center font-bold text-emerald-400">เวกเตอร์</th>
              <th class="py-2 px-2 text-center w-20 sm:w-24 font-bold text-slate-400">จัดการ</th>
            </tr>
          </thead>
          <tbody class="divide-y divide-[#182030] font-mono text-xs">
            {#if isLoading}
              <tr>
                <td colspan="6" class="py-14 text-center text-slate-400 font-mono text-xs">
                  <div class="flex flex-col items-center justify-center gap-2.5">
                    <div class="w-7 h-7 border-2 border-amber-400/80 border-t-transparent rounded-full animate-spin"></div>
                    <span class="text-slate-400">&gt;&gt; READING SQLITE EMBEDDINGS VECTORS...</span>
                  </div>
                </td>
              </tr>
            {:else if filteredPersons.length === 0}
              <tr>
                <td colspan="6" class="py-14 text-center text-slate-500 font-mono text-xs">
                  <div class="flex flex-col items-center justify-center gap-2">
                    <svg class="w-9 h-9 text-slate-600" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.5" d="M9.172 16.172a4 4 0 015.656 0M9 10h.01M15 10h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
                    </svg>
                    <span>[ 0 RECORDS MATCHED FILTER QUERY ]</span>
                  </div>
                </td>
              </tr>
            {:else}
              {#each filteredPersons as p}
                {@const isSelected = selectedPerson && selectedPerson.person_id === p.person_id}
                <tr
                  onclick={() => selectPerson(p)}
                  class="cursor-pointer transition-colors {isSelected ? 'bg-[#1b1c20] border-l-4 border-l-amber-400' : 'hover:bg-[#0c1018]'}"
                >
                  <td class="py-2 px-2 sm:px-3 font-mono font-black text-xs whitespace-nowrap {isSelected ? 'text-amber-300' : 'text-slate-400'}">
                    #{p.person_id.toString().padStart(3, '0')}
                  </td>
                  <td class="py-2 px-2 sm:px-3 min-w-0">
                    <div class="font-bold text-xs sm:text-sm {isSelected ? 'text-white' : 'text-slate-200'} font-sans truncate">{p.name}</div>
                    <div class="text-[10px] text-slate-500 font-mono truncate">MOD: {p.updated_at || 'RECENT'}</div>
                  </td>
                  <td class="py-2 px-2 sm:px-2.5">
                    <span class="font-mono text-[10px] sm:text-[11px] font-bold px-1.5 sm:px-2 py-0.5 rounded border whitespace-nowrap inline-block {isSelected ? 'text-amber-200 bg-[#222834] border-amber-500/40' : 'text-slate-300 bg-[#080d14] border-[#222c3d]'}">
                      {p.student_id || '-'}
                    </span>
                  </td>
                  <td class="py-2 px-2 sm:px-2.5 hidden sm:table-cell">
                    <span class="inline-block px-2 py-0.5 rounded text-[10px] sm:text-[11px] font-medium bg-[#111622] text-slate-300 border border-[#222c3d] truncate max-w-[140px] xl:max-w-[180px] font-sans">
                      {p.role || 'นักศึกษา'}
                    </span>
                  </td>
                  <td class="py-2 px-1.5 sm:px-2 text-center whitespace-nowrap">
                    <span class="font-mono text-[10px] sm:text-xs font-bold text-emerald-400 bg-emerald-950/60 px-1.5 sm:px-2 py-0.5 rounded border border-emerald-500/40 inline-block">
                      {p.embedding_count} SETS
                    </span>
                  </td>
                  <td class="py-2 px-2 text-center whitespace-nowrap">
                    <div class="flex items-center justify-center gap-1.5 whitespace-nowrap">
                      <!-- Edit Button -->
                      <button
                        onclick={(e) => { e.stopPropagation(); openEditModal(p); }}
                        class="p-1 sm:p-1.5 bg-[#172030] hover:bg-[#233149] border border-[#2b3c58] hover:border-amber-400/60 rounded-lg text-slate-300 hover:text-amber-300 transition cursor-pointer"
                        title="แก้ไขข้อมูล"
                        aria-label="Edit {p.name}"
                      >
                        <svg class="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M11 5H6a2 2 0 00-2 2v11a2 2 0 002 2h11a2 2 0 002-2v-5m-1.414-9.414a2 2 0 112.828 2.828L11.828 15H9v-2.828l8.586-8.586z" />
                        </svg>
                      </button>

                      <!-- Delete Button -->
                      <button
                        onclick={(e) => { e.stopPropagation(); handleDelete(p.person_id, p.name); }}
                        class="p-1 sm:p-1.5 bg-[#251016] hover:bg-rose-950/80 border border-rose-500/40 rounded-lg text-rose-400 hover:text-white transition cursor-pointer"
                        title="ลบข้อมูล"
                        aria-label="Delete {p.name}"
                      >
                        <svg class="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
                        </svg>
                      </button>
                    </div>
                  </td>
                </tr>
              {/each}
            {/if}
          </tbody>
        </table>
      </div>
    </div>

    <!-- RIGHT COLUMN: Large Person Details + Unified Info + Recent Edit History (5 cols) -->
    <div class="lg:col-span-5 chassis-panel p-2.5 sm:p-3.5 rounded-2xl flex flex-col min-h-0 overflow-y-auto relative custom-scrollbar">
      <!-- Corner Rivet Screws -->
      <div class="absolute top-1.5 left-2 screw-marker">+</div>
      <div class="absolute top-1.5 right-2 screw-marker">+</div>

      {#if selectedPerson}
        <div class="flex-1 min-h-0 flex flex-col justify-between gap-2 relative z-10">
          
          <!-- Top Section (Header + Profile Card) -->
          <div class="flex flex-col gap-1.5 sm:gap-2 shrink-0">
            <!-- Profile Card Header -->
            <div class="flex items-center justify-between border-b border-[#283348] pb-1 gap-2 shrink-0">
              <div class="flex items-center gap-2">
                <span class="w-2 h-2 rounded-full bg-amber-400 animate-pulse shadow-[0_0_8px_#f59e0b]"></span>
                <h3 class="text-xs sm:text-sm font-mono font-black uppercase tracking-wider text-amber-300">
                  &gt;&gt; IDENTITY_TELEMETRY_PROFILE
                </h3>
              </div>
              <span class="text-[9.5px] sm:text-[10.5px] font-mono font-bold text-emerald-400 bg-emerald-950/80 border border-emerald-500/40 px-2 py-0.5 rounded flex items-center gap-1.5 shadow-sm">
                <span class="w-1.5 h-1.5 rounded-full bg-emerald-400 animate-pulse"></span>
                ONLINE_VERIFIED
              </span>
            </div>

            <!-- Upper Profile Section: Snug at Top -->
            <div class="terminal-screen p-2.5 sm:p-3 rounded-xl border border-[#283348] flex flex-col justify-between shadow-inner shrink-0 relative overflow-hidden">
              <div class="crt-scanlines absolute inset-0 pointer-events-none opacity-30"></div>

              <div class="flex flex-row items-center gap-3 relative z-10">
                <!-- Avatar / Photo Box -->
                <div class="relative w-14 h-14 sm:w-16 sm:h-16 md:w-18 md:h-18 rounded-xl overflow-hidden border-2 border-amber-400/60 shadow-[0_0_12px_rgba(245,158,11,0.2)] bg-[#03060a] flex flex-col items-center justify-center shrink-0">
                  <!-- Viewfinder Corner Crosshairs -->
                  <div class="absolute top-1 left-1 text-[8px] font-mono text-amber-400/80 leading-none">◤</div>
                  <div class="absolute top-1 right-1 text-[8px] font-mono text-amber-400/80 leading-none">◥</div>
                  <div class="absolute bottom-3 left-1 text-[8px] font-mono text-amber-400/80 leading-none">◣</div>
                  <div class="absolute bottom-3 right-1 text-[8px] font-mono text-amber-400/80 leading-none">◢</div>

                  <svg class="w-8 h-8 sm:w-9 sm:h-9 text-slate-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.5" d="M16 7a4 4 0 11-8 0 4 4 0 018 0zM12 14a7 7 0 00-7 7h14a7 7 0 00-7-7z" />
                  </svg>

                  <div class="absolute bottom-0.5 bg-black/90 px-1 py-0.5 rounded text-[7px] font-mono text-amber-300/90 font-bold border border-amber-500/30 tracking-wider">
                    PHOTO // ID
                  </div>
                </div>

                <!-- Name & Role Badges -->
                <div class="min-w-0 flex-1 space-y-1 text-left">
                  <div class="text-[9.5px] font-mono text-slate-400 font-bold uppercase tracking-wider">&gt; SUBJECT_NAME:</div>
                  <h2 class="text-base sm:text-lg md:text-xl font-black text-white tracking-wide truncate leading-tight font-sans" title={selectedPerson.name}>
                    {selectedPerson.name}
                  </h2>
                  <div class="flex flex-wrap items-center gap-1.5 pt-0.5">
                    <span class="font-mono text-[11px] sm:text-xs font-bold text-amber-300 bg-[#1d1f2a] px-2 py-0.5 rounded-md border border-[#2d3a50] whitespace-nowrap inline-block shadow-sm">
                      ID: {selectedPerson.student_id || '#' + selectedPerson.person_id}
                    </span>
                    <span class="text-[11px] sm:text-xs font-bold text-slate-200 px-2 py-0.5 rounded-md bg-[#161d2b] border border-[#283348] whitespace-nowrap inline-block font-sans shadow-sm">
                      {selectedPerson.role || 'นักศึกษา'}
                    </span>
                  </div>
                </div>
              </div>

              <!-- Unified Specs Grid -->
              <div class="grid grid-cols-2 gap-2 pt-2 mt-2 border-t border-[#1f2838] relative z-10">
                <div class="bg-[#05080e] px-2.5 py-1 sm:py-1.5 rounded-lg border border-[#1f2838] flex flex-col justify-center shadow-inner">
                  <div class="text-[9px] text-slate-400 font-mono leading-none whitespace-nowrap uppercase">&gt; EMBEDDINGS (512-D)</div>
                  <div class="text-xs sm:text-sm font-black font-mono text-emerald-400 mt-1 leading-none whitespace-nowrap flex items-center gap-1">
                    <span>{selectedPerson.embedding_count} ชุด</span>
                    <span class="text-[9px] text-emerald-500/70 font-semibold">(NORMALIZED)</span>
                  </div>
                </div>
                <div class="bg-[#05080e] px-2.5 py-1 sm:py-1.5 rounded-lg border border-[#1f2838] flex flex-col justify-center shadow-inner">
                  <div class="text-[9px] text-slate-400 font-mono leading-none whitespace-nowrap uppercase">&gt; DB_RECORD_KEY</div>
                  <div class="text-xs sm:text-sm font-black font-mono text-slate-200 mt-1 leading-none whitespace-nowrap">
                    PRIMARY_KEY #{selectedPerson.person_id}
                  </div>
                </div>
              </div>
            </div>
          </div>

          <!-- Middle Section: Stretched Logs Card with Embedded Tubes (Logs hug the top snugly) -->
          <div class="bg-[#05080e] p-2 sm:p-2.5 rounded-xl border border-[#283348] shadow-inner flex-1 min-h-[200px] flex flex-col justify-between overflow-hidden">
            
            <!-- Header (Pinned top) -->
            <div class="flex items-center justify-between pb-1 border-b border-[#1f2838] gap-2 shrink-0">
              <div class="flex items-center gap-1.5 min-w-0">
                <span class="w-1.5 h-1.5 rounded-full bg-amber-400"></span>
                <span class="text-[11px] sm:text-xs font-mono font-bold text-slate-200 uppercase tracking-wider whitespace-nowrap truncate">
                  &gt;&gt; EVENT_AUDIT_STREAM // 5 LOGS ล่าสุด
                </span>
              </div>
              <span class="text-[9px] sm:text-[9.5px] font-mono font-bold text-slate-300 bg-[#161d2b] px-1.5 py-0.5 rounded border border-[#283348] whitespace-nowrap shrink-0">
                {editHistoryList.length}/5 SLOTS
              </span>
            </div>

            <!-- Snug 5 History Rows (ชิดบนแนบสนิทกับเส้นหัวกล่อง ไม่มีช่องว่างกว้าง) -->
            <div class="space-y-1 pt-1 font-mono text-xs shrink-0 flex flex-col justify-start">
              {#if isHistoryLoading}
                <div class="flex items-center justify-center py-3 text-slate-400 text-xs font-mono gap-2">
                  <div class="w-3.5 h-3.5 border-2 border-amber-400/70 border-t-transparent rounded-full animate-spin"></div>
                  <span>READING AUDIT TRAILS...</span>
                </div>
              {:else}
                {#each Array(5) as _, idx}
                  {@const log = editHistoryList[idx]}
                  {#if log}
                    <div class="bg-black/60 px-2 py-0.5 sm:py-1 rounded-md border border-[#1f2838] flex items-center justify-between gap-1.5 hover:border-[#2f3d54] transition-all text-xs">
                      <div class="flex items-center gap-1.5 min-w-0 flex-1">
                        <span class="text-[9.5px] font-mono text-amber-400 font-bold shrink-0">[{idx+1}]</span>
                        <div class="min-w-0 flex-1 truncate">
                          <span class="font-bold text-slate-200 text-[11px] sm:text-xs font-sans whitespace-nowrap">{log.action}</span>
                          {#if log.details}
                            <span class="text-slate-400 text-[10px] ml-1 truncate font-sans">&bull; {log.details}</span>
                          {/if}
                        </div>
                      </div>
                      <span class="text-[9px] sm:text-[9.5px] font-mono text-slate-400 whitespace-nowrap shrink-0">
                        {log.timestamp}
                      </span>
                    </div>
                  {:else}
                    <div class="bg-black/20 px-2 py-0.5 rounded-md border border-dashed border-[#182030] flex items-center justify-between gap-1.5 text-slate-600 text-xs">
                      <div class="flex items-center gap-1.5 min-w-0">
                        <span class="text-[9px] font-mono text-slate-600">[{idx+1}]</span>
                        <span class="text-[9.5px] font-mono text-slate-600 whitespace-nowrap">SLOT_{idx + 1} // VACANT</span>
                      </div>
                      <span class="text-[8.5px] font-mono text-slate-700 shrink-0">--:--:--</span>
                    </div>
                  {/if}
                {/each}
              {/if}
            </div>

            <!-- Flexible Space to push vacuum tubes to bottom on large screens -->
            <div class="tube-rack-spacer flex-1 min-h-[4px]"></div>

            <!-- 4 Big Interactive Glowing Vacuum Tubes (Inside Logs Card at Bottom, Hidden on Compact Screens) -->
            <div class="tube-rack pt-1 mt-1 border-t border-[#182030] items-center justify-around shrink-0">
              {#each [0, 1, 2, 3] as tIdx}
                {@const isOn = tubesActive[tIdx]}
                <button
                  type="button"
                  onclick={() => toggleTube(tIdx)}
                  class="relative flex flex-col items-center cursor-pointer group transition-transform hover:scale-105 active:scale-95 px-1.5"
                  title="คลิกเพื่อสลับเปิด-ปิดไฟหลอดสุญญากาศ V{tIdx + 1}"
                >
                  <!-- Glass Tube -->
                  <div class="relative w-5 sm:w-6 h-7.5 sm:h-9 rounded-t-full bg-gradient-to-b {isOn ? 'from-white/30 via-amber-500/15 to-transparent shadow-[0_0_15px_rgba(245,158,11,0.8)] border-amber-400/60' : 'from-white/10 via-transparent to-transparent shadow-none border-slate-700'} border flex items-center justify-center overflow-hidden transition-all duration-300">
                    <div class="absolute top-0.5 left-0.5 w-0.5 h-2 rounded-full bg-white/40 blur-[0.5px]"></div>
                    <!-- Tungsten Filament -->
                    <div class="relative w-1 h-3 sm:h-3.5 flex flex-col items-center justify-between">
                      <div class="w-1 h-1 rounded-full {isOn ? 'bg-[#ffedd5] shadow-[0_0_6px_2px_#f59e0b] scale-125' : 'bg-slate-700'} transition-all"></div>
                      <div class="w-[1px] h-full {isOn ? 'bg-[#fef08a] shadow-[0_0_4px_#f97316]' : 'bg-slate-800'} transition-all"></div>
                      <div class="w-1 h-1 rounded-full {isOn ? 'bg-[#ffedd5] shadow-[0_0_6px_2px_#f59e0b] scale-125' : 'bg-slate-700'} transition-all"></div>
                    </div>
                  </div>
                  <!-- Metal Base Socket -->
                  <div class="w-5.5 sm:w-6.5 h-1.5 bg-gradient-to-r from-[#475569] via-[#94a3b8] to-[#334155] rounded-b-sm border-t border-black shadow-md flex items-center justify-center">
                    <span class="text-[6px] {isOn ? 'text-amber-950 font-black' : 'text-slate-900 font-bold'}">V{tIdx + 1}</span>
                  </div>
                </button>
              {/each}
            </div>

          </div>

          <!-- Bottom Action Buttons: Pinned Bottom -->
          <div class="grid grid-cols-2 gap-2 pt-1 border-t border-[#283348] shrink-0 relative z-10">
            <button
              onclick={() => openEditModal(selectedPerson!)}
              class="w-full bg-[#1e293b] hover:bg-[#2b3952] border border-amber-500/40 hover:border-amber-400 text-amber-300 hover:text-white py-1.5 sm:py-2 rounded-xl text-xs font-bold transition flex items-center justify-center gap-1.5 cursor-pointer active:scale-95 uppercase tracking-wider font-mono shadow-sm"
            >
              <svg class="w-3.5 h-3.5 shrink-0" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M11 5H6a2 2 0 00-2 2v11a2 2 0 002 2h11a2 2 0 002-2v-5m-1.414-9.414a2 2 0 112.828 2.828L11.828 15H9v-2.828l8.586-8.586z" />
              </svg>
              <span>EDIT_RECORD</span>
            </button>

            <button
              onclick={() => handleDelete(selectedPerson!.person_id, selectedPerson!.name)}
              class="w-full bg-rose-950/40 hover:bg-rose-900/80 border border-rose-500/40 text-rose-300 hover:text-white py-1.5 sm:py-2 rounded-xl text-xs font-bold transition flex items-center justify-center gap-1.5 cursor-pointer active:scale-95 uppercase tracking-wider font-mono shadow-sm"
            >
              <svg class="w-3.5 h-3.5 shrink-0" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
              </svg>
              <span>PURGE_RECORD</span>
            </button>
          </div>

        </div>
      {:else}
        <div class="flex flex-col items-center justify-center h-full text-slate-500 text-xs font-mono py-16">
          <svg class="w-10 h-10 text-slate-700 mb-2.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.5" d="M15 15l-2 5L9 9l11 4-5 2zm0 0l5 5M7.188 2.239l.777 2.897M5.136 7.965l-2.898-.777M13.95 4.05l-2.122 2.122m-5.657 5.656l-2.12 2.122" />
          </svg>
          <span>&gt;&gt; SELECT PERSON FROM INDEX MATRIX</span>
        </div>
      {/if}
    </div>
  </div>

  <!-- Edit Person Modal Dialog -->
  {#if isEditModalOpen}
    <div class="fixed inset-0 z-50 bg-black/85 backdrop-blur-md flex items-center justify-center p-4">
      <div class="chassis-panel rounded-2xl w-full max-w-md shadow-2xl overflow-hidden border border-amber-500/50 animate-in fade-in zoom-in duration-200 relative">
        <div class="absolute top-1.5 left-2 screw-marker">+</div>
        <div class="absolute top-1.5 right-2 screw-marker">+</div>

        <!-- Modal Header -->
        <div class="bg-[#0c1018] px-5 py-3.5 border-b border-[#283348] flex items-center justify-between">
          <div class="flex items-center gap-2.5">
            <div class="w-8 h-8 rounded-lg bg-amber-500/20 border border-amber-500/40 flex items-center justify-center text-amber-400">
              <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M11 5H6a2 2 0 00-2 2v11a2 2 0 002 2h11a2 2 0 002-2v-5m-1.414-9.414a2 2 0 112.828 2.828L11.828 15H9v-2.828l8.586-8.586z" />
              </svg>
            </div>
            <div>
              <h3 class="text-xs sm:text-sm font-black text-amber-300 font-mono">&gt;&gt; MODIFY_OPERATOR_DATA</h3>
              <p class="text-[10px] text-slate-400 font-mono">PERSON_ID: #{editPersonId}</p>
            </div>
          </div>
          <button
            onclick={closeEditModal}
            class="p-1.5 rounded-lg text-slate-400 hover:text-white hover:bg-slate-800 transition cursor-pointer"
            title="ปิดหน้าต่าง"
            aria-label="Close modal"
          >
            <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
            </svg>
          </button>
        </div>

        <!-- Modal Form Body -->
        <form onsubmit={(e) => { e.preventDefault(); handleSaveEdit(); }} class="p-5 space-y-3.5 text-xs">
          {#if modalError}
            <div class="p-2.5 bg-rose-950/80 border border-rose-500/60 rounded-lg text-rose-300 text-xs flex items-center gap-2 font-mono">
              <svg class="w-4 h-4 shrink-0" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 8v4m0 4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
              </svg>
              <span>{modalError}</span>
            </div>
          {/if}

          <!-- Full Name Input -->
          <div class="space-y-1">
            <label for="edit-name" class="block font-bold text-amber-400 font-mono text-[11px] uppercase">
              FULL_NAME &gt; <span class="text-rose-400">*</span>
            </label>
            <input
              id="edit-name"
              type="text"
              bind:value={editName}
              placeholder="เช่น นาย สมชาย ใจดี"
              required
              class="w-full terminal-input px-3 py-2 rounded-xl text-xs font-sans placeholder-amber-700/60 transition"
            />
          </div>

          <!-- Student / Staff ID Input -->
          <div class="space-y-1">
            <label for="edit-studentid" class="block font-bold text-amber-400 font-mono text-[11px] uppercase">
              STUDENT_ID &gt;
            </label>
            <input
              id="edit-studentid"
              type="text"
              bind:value={editStudentId}
              placeholder="เช่น 68543206118-0 หรือ EMP-001"
              class="w-full terminal-input px-3 py-2 rounded-xl text-xs font-mono placeholder-amber-700/60 transition"
            />
          </div>

          <!-- Role / Department Selection / Input -->
          <div class="space-y-1">
            <label for="edit-role" class="block font-bold text-amber-400 font-mono text-[11px] uppercase">
              ROLE_DEPARTMENT &gt;
            </label>
            <select
              id="edit-role"
              bind:value={editRole}
              class="w-full terminal-input px-3 py-2 rounded-xl text-xs font-sans transition bg-[#0a0d14]"
            >
              <option value="นักศึกษา">นักศึกษา</option>
              <option value="อาจารย์/บุคลากร">อาจารย์/บุคลากร</option>
              <option value="นักพัฒนาระบบ">นักพัฒนาระบบ</option>
              <option value="พนักงานทดลองระบบ">พนักงานทดลองระบบ</option>
              <option value="ผู้ดูแลระบบ">ผู้ดูแลระบบ</option>
            </select>
          </div>

          <!-- Modal Actions -->
          <div class="flex items-center justify-end gap-2.5 pt-3 border-t border-[#283348]">
            <button
              type="button"
              onclick={closeEditModal}
              disabled={isSaving}
              class="px-4 py-2 rounded-xl border border-slate-700 hover:bg-slate-800 text-slate-300 text-xs font-mono font-bold transition cursor-pointer"
            >
              CANCEL
            </button>
            <button
              type="submit"
              disabled={isSaving}
              class="bg-gradient-to-r from-amber-500 via-amber-400 to-yellow-500 hover:from-amber-400 hover:to-yellow-400 px-5 py-2 rounded-xl text-xs font-black text-slate-950 shadow-[0_0_15px_rgba(245,158,11,0.35)] border border-amber-300/60 transition flex items-center gap-1.5 cursor-pointer active:scale-95 font-mono uppercase"
            >
              {#if isSaving}
                <div class="w-3.5 h-3.5 border-2 border-slate-950 border-t-transparent rounded-full animate-spin"></div>
                <span>COMMITTING...</span>
              {:else}
                <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 13l4 4L19 7" />
                </svg>
                <span>COMMIT_CHANGES</span>
              {/if}
            </button>
          </div>
        </form>
      </div>
    </div>
  {/if}
</div>

<style>
  /* Base display for vacuum tube rack and spacer */
  .tube-rack {
    display: flex;
  }

  /* Automatically hide tubes & spacer when window height is short or window is shrunk */
  @media (max-height: 760px) {
    .tube-rack,
    .tube-rack-spacer {
      display: none !important;
    }
  }

  @media (max-width: 1200px) {
    .tube-rack,
    .tube-rack-spacer {
      display: none !important;
    }
  }
</style>