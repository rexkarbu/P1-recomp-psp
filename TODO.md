# Shin Megami Tensei: Persona (PSP) — PC Port Task Tracker (TODO)

Dokumen ini melacak seluruh progres pengerjaan proyek _static recompilation_ game **Shin Megami Tensei: Persona (USA, ULUS10432)** menjadi native PC executable (`persona_pc.exe`).

---

## ✅ 1. Tugas yang Sudah Selesai (Completed)

- [x] **Memory Map & CPU Initialization**:
  - Emulasi memori PSP (32 MB Main RAM, 2 MB VRAM, 16 KB Scratchpad) dengan translasi pointer host.
  - Mapping memori modul relocatable ELF (`EBOOT_RELOC.ELF`) segmen code & data/bss.
  - Setup ABI register loader ($gp, $sp, $k0/TCB, $ra).
- [x] **Static Recompilation Engine**:
  - Recompilasi seluruh instruksi MIPS Allegrex R4000 menjadi C11 native.
  - Dispatch table function pointer untuk indirect jumps dan exception handling.
  - Cooperative host threading (`PSPRECOMP_REAL_THREADS`) untuk eksekusi multi-thread game (`user_main`, `sndp`, `SceWaveMain`).
- [x] **Virtual File System & Asset Extraction**:
  - Akses file virtual `disc0:/` langsung ke isi ISO UMD.
  - Pemuatan aset game utama `datapack.bin` dan file BGM audio.
- [x] **HLE Firmware Subsystems**:
  - `sceSysmem`: Partisi memori dan alokasi heap dinamis.
  - `sceThreadMan`: Siklus hidup thread, event flags, vtime, delay thread, yield.
  - `sceIoFileMgr`: Operasi read, seek, close pada file disc & ISO.
  - `sceUmdUser`: Status UMD (drive inserted, readable, activated).
  - `sceImpose`: Bahasa dan volume default.
  - `sceUtilitySavedata`: Siklus autoload save data dengan kode resmi `0x80110307` (`NO_DATA`), memungkinkan game lolos boot check.
  - `sceAtrac3plus`: 14 NID firmware audio decoding dan chunk streaming dari UMD.
- [x] **Hardware-Accelerated Presentation Window (OpenGL WGL)**:
  - Konteks hardware OpenGL WGL double-buffered (`SwapBuffers`), bebas tearing.
  - Tekstur RGBA 480×272 langsung dari framebuffer PSP tanpa overhead byte-swapping CPU.
  - Letterboxing otomatis menjaga rasio asli 480:272 pada ukuran window berapa pun.
  - Software fallback otomatis ke GDI jika GPU tidak tersedia.
- [x] **Dual Keyboard & Gamepad Input**:
  - Dukungan Arrow Keys dan WASD.
  - Dual action buttons (Z/X/C/V tangan kiri dan J/K/U/I tangan kanan).
  - Space (Confirm / Cross) dan Escape (Cancel / Circle).
  - Controller XInput auto-detect port 0..3 dengan pencerminan analog stick ke D-Pad.
  - CLI flags: `--scale <n>`, `--headless`, `--frames <n>`.
- [x] **Milestone Title Screen**:
  - Game berhasil booting hingga Title Screen penuh dengan 9.512 warna unik tanpa crash.

---

## 📋 2. Masalah & Bug Terkini (High Priority Bug Fixes)

- [x] **Perbaikan Tampilan Layar Bergaris-garis (_Screen Artifacts / Scanline Striping_)**:
  - Investigasi penyebab garis-garis pada layar: akar masalah ditemukan pada evaluasi bobot barycentric di `tools/psprecomp/src/render.c` (`sw_tri()`) yang menghasilkan bobot negatif di tepi segitiga saat clockwise winding.
  - Memperbaiki formula edge function di titik tengah piksel `(fx, fy) = (x + 0.5f, y + 0.5f)` dan normalisasi bobot.
  - Hasil terverifikasi 100%: 0 glitch pixel pada `frame_1800_fixed.png`!
- [x] **Implementasi Output Suara Nyata (Host PC Audio Backend)**:
  - Mengimplementasikan audio streamer Win32 `waveOut` berlatensi rendah multi-buffer (8 `WAVEHDR` per channel) di `tools/psprecomp/src/hle/misc.c`.
  - Memisahkan handler panned blocking (`hle_OutputPannedBlocking`) dan direct blocking (`hle_OutputBlocking`) untuk membaca argument volume dan buffer PCM secara presisi.
  - Mengimplementasikan NID `0xE175EF66` (`__sceSasGetOutputmode`) di `tools/psprecomp/src/hle/sascore.c`.
  - Menyediakan kontrol volume master global (`psp_audio_set_master_volume`) yang siap diintegrasikan ke menu pengaturan Audio.
  - Buffer audio PCM 16-bit stereo 44.1 kHz dari game kini dialirkan secara mulus dan jernih langsung ke speaker/headphone host PC!

---

## ⚙️ 3. Fitur Menu Pengaturan In-Game (In-Game Overlay Menu - Tombol F1)

Menu overlay interaktif yang dapat dibuka/ditutup kapan saja selama game berjalan (`tools/psprecomp/src/overlay.c`).

### A. Controls (Pengaturan Kontrol)

- [x] **1. Free Camera Movements**:
  - Fitur rotasi pandangan kamera menggunakan analog kanan (R3: `sThumbRX`/`sThumbRY`) pada controller.
  - Opsi toggle ON / OFF langsung di tab Controls.
- [x] **2. Standard Controls**:
  - Konfigurasi kontrol seimbang antara Gamepad (XInput port 0..3) dan Keyboard (WASD + Z/X/C/V dan Arrow Keys + J/K/U/I).
- [x] **3. Akses Menu & Navigasi**:
  - Buka/tutup menu pengaturan via: `F1`, `Esc`, Tombol `Guide / Home / Xbox` pada gamepad, atau kombinasi `Back + Start`.
  - Shortcut Fullscreen: `F11` / `Alt + Enter`.

### B. Video (Pengaturan Grafis & Display)

- [x] **1. Display**:
  - **Aspect Ratio**: Opsi `16:9 (Native PSP)`, `4:3 (Classic CRT)`, dan `16:9 (Stretched)`.
  - **Fullscreen**: Toggle Windowed / Fullscreen langsung dari menu (sinkron dengan `F11`).
  - **Target FPS**: Pilihan limit frame rate (`30 FPS`, `60 FPS`, `90 FPS`, `120 FPS`, `240 FPS`).
  - **Show FPS**: Toggle OSD FPS counter ON / OFF (shortcut keyboard `F4`).
- [x] **2. Rendering**:
  - **Internal Resolution Multiplier**: Pilihan `1x` (480×272), `2x` (960×544), `3x` (1440×816), `4x` (1920×1088).
  - **Upscaler Filter**:
    - _Nearest (Pixel Art)_
    - _Bilinear (Smooth)_
    - _AMD FSR 1.0 (Edge Sharpening)_
  - **Anti-Aliasing**: Toggle ON / OFF (FXAA smoothing).

### C. Audio (Pengaturan Suara)

- [x] **Master Volume Slider**:
  - Pengatur volume suara dari 0% hingga 100% (step 5%) dengan penyesuaian instan ke Win32 `waveOut` audio streamer.
  - Opsi mute instan.

### D. Cheats (Dukungan CWCheat / PPSSPP Format - ULUS10432)

- [x] Integrasi memory write cheat real-time berbasis CWCheat PSP (ULUS10432):
  - **Infinite Battle HP & SP** (Party HP & 1P/2P/3P HP & SP)
  - **Max Yen (99,999,999)** (`0x004C6200` = `0x05F5E0FF`)
  - **Max Casino Coins (99,999,999)** (`0x004C6204` = `0x05F5E0FF`)
  - **No Random Encounters** (`0x0005CED0` = `0x00000000`)

---

## 🕹️ 4. Hotkeys & Fitur Kenyamanan (Quality of Life)

- [x] **F2 — Instant Screenshot**:
  - Menangkap tampilan layar saat ini dan menyimpannya otomatis ke folder `./screenshots/persona_screenshot_YYYYMMDD_HHMMSS.bmp` (24-bit TrueColor BMP).
- [x] **F4 — Toggle FPS Counter**:
  - Menampilkan/menyembunyikan real-time FPS counter di pojok kanan atas layar secara instan.
- [x] **F5 — Save State**:
  - Menyimpan snapshot instan seluruh state RAM (32 MB), VRAM (2 MB), Scratchpad (16 KB), PRX module, dan CPU register ke `./states/slot1.state`.
- [x] **F6 — Load State**:
  - Membaca dan mengembalikan game secara instan ke titik save state tanpa harus reboot.

---

## 💾 5. Sistem Penyimpanan Game Asli (Native Save/Load)

- [x] **Memetakan `sceUtilitySavedata` operasi Save dan Load ke file fisik di `./savedata/` pada harddisk PC**:
  - Implementasi lengkap handler HLE untuk `sceUtilitySavedataInitStart`, `sceUtilitySavedataGetStatus`, dan `sceUtilitySavedataShutdownStart`.
  - Mendukung operasi `AUTOSAVE`, `SAVE`, `LISTSAVE`, `AUTOLOAD`, `LOAD`, `LISTLOAD`, `DELETE`, dan `SIZES`.
  - Generator biner `PARAM.SFO` standar Sony PSP (`\0PSF`, format 1.1) otomatis mencatat `TITLE`, `SAVEDATA_TITLE`, dan `SAVEDATA_DETAIL`.
  - Dukungan ekspor dan impor ikon save data `ICON0.PNG` ke folder slot.
- [x] **Kompatibilitas dengan save data asli PSP & PPSSPP**:
  - Folder penyimpanan terstruktur sesuai format asli PSP: `./savedata/<gameName><saveName>/` (misal `./savedata/ULUS10432SYSI/` dan `./savedata/ULUS104320000/`).
  - Mekanisme cascading fallback mendeteksi nama file save asli: membaca `PERSONA.SAV`, `DATA.BIN`, atau `SECURE.BIN`.
  - Save file dari konsol PSP fisik atau emulator PPSSPP lama dapat langsung disalin ke folder `./savedata/` dan dimainkan tanpa konversi.
  - Unit test `test_savedata.exe` lulus pengujian 100% (Autosave, Autoload, Fallback, Error Handling, Delete).

---

## 📦 6. Standalone Game Launcher & Legal Distribution Pipeline (BYOG — Bring Your Own Game)

Tujuan utama: Menyiapkan model distribusi rilis GitHub yang 100% legal dan bersih dari pelanggaran hak cipta (_clean legal distribution_, serupa dengan proyek _Zelda 64 Recomp_, _Jak & Daxter OpenGOAL_, dan _Sonic CD decompilation_). Yang didistribusikan ke publik hanyalah binary launcher dan mesin rekompilasi tanpa menyertakan aset game komersial Atlus. Pemain membawa file ISO game mereka sendiri (_Bring Your Own Game_).

### A. Tampilan & Antarmuka Launcher (`persona_launcher.exe`)

- [x] **1. GUI Launcher Desktop yang Ringan & Modern**:
  - Antarmuka grafis desktop Win32 native C11 (~95 KB binary size) bertema Atlus Persona (Dark Indigo / Velvet Violet).
  - Rendering double-buffered GDI 100% bebas flickering dengan status wizard otomatis (_Not Installed_ vs _Ready to Play_).
  - Tombol **"Select ISO File..."** (_Windows File Dialog_) dan area **Drag-and-Drop** (`WM_DROPFILES`) langsung ke jendela launcher.

### B. Validasi & Integritas ISO Game

- [x] **2. Deteksi & Verifikasi ISO Persona 1 USA (`ULUS-10432`)**:
  - Membaca header file ISO virtual (ISO9660 PVD Sector 16) dan mengekstrak metadata dari `PSP_GAME/PARAM.SFO`.
  - Memvalidasi secara native bahwa ISO yang dimasukkan adalah versi resmi USA (`DISC_ID: ULUS-10432` / `Title: Shin Megami Tensei: Persona`).
  - Pengecekan checksum hash cryptographic (SHA-1: `ec190259efdac60801e25c9f7d1ce51cda8f94aa` dan MD5: `c75f75b0f1e92d7cca77d75585bebd82`) berbasis Windows CryptoAPI native (`wincrypt.h`) untuk memverifikasi kecocokan 1:1 terhadap basis data Redump resmi.
  - Deteksi dan penolakan otomatis untuk ISO salah region (misal versi Jepang `ULJM-05461`) atau ISO game lain dengan pesan dialog peringatan yang informatif.
  - Menampilkan indikator status verifikasi di GUI Launcher: `"✅ Persona 1 (USA) Detected - Ready to Recompile (Official Redump 1:1 Clean Dump Verified)"`.

### C. Otomatisasi Pipeline Ekstraksi, Dekripsi, & Rekompilasi

- [x] **3. Ekstraksi Aset Mandiri (Self-Contained Extraction)**:
  - Ekstraksi pohon direktori `PSP_GAME/USRDIR/` (`datapack.bin`, folder audio, movie) secara 100% native dari ISO9660 tanpa memerlukan tool luar (7-Zip/WinRAR).
  - Ekstraksi aset pendukung: `ICON0.PNG`, `PIC1.PNG`, dan metadata `PARAM.SFO`.
- [x] **4. Dekripsi Otomatis Executable PSP**:
  - Mengekstrak `PSP_GAME/SYSDIR/EBOOT.BIN` ke `extracted/EBOOT.BIN`.
  - Integrasi otomatis engine dekripsi Kirk melalui modul dekriptor PSP menghasilkan `extracted/EBOOT.ELF` yang valid (header `\x7F ELF`).
- [x] **5. One-Click Recompilation & Asset Linking**:
  - Engine relokasi segmen PRX internal merelokasi segmen `EBOOT.ELF` menjadi executable relocatable `extracted/EBOOT_RELOC.ELF`.
  - Background worker thread dengan visual Progress Bar interaktif (0%..100%) dan live status logging.
  - Persistensi status instalasi melalui berkas `install_manifest.txt` sehingga setup hanya perlu dilakukan sekali saat rilis pertama.
  - Pengujian `test_full_installation_pipeline.exe` berhasil 100% mengekstrak, mendekripsi, dan memverifikasi integritas file permainan.

### D. Game Management & Peluncuran

- [x] **6. Tombol "Play / Launch Game" & Pengaturan Awal**:
  - Tombol **"PLAY PERSONA 1"** langsung meluncurkan `persona_pc.exe` secara seamless dengan argumen konfigurasi yang dipilih dan otomatis meminimalkan launcher ke taskbar.
  - **Quick Pre-Launch Settings & Konfigurasi Interaktif**:
    - **Window Scale (Resolusi Internal)**: Pilihan tombol pills responsif `1x (480x272)`, `2x HD (960x544)`, `3x QHD (1440x816)`, dan `4x Full HD (1920x1088)` dengan penanda visual status aktif.
    - **Input Device Selector**: Pilihan pills instan `Auto Detect`, `Xbox Gamepad (XInput)`, dan `Keyboard Only`.
    - **Mode Layar**: Toggle Checkbox interaktif `[x] Start in Fullscreen Mode`.
    - **Persistensi Konfigurasi (`launcher_config.ini`)**: Otomatis menyimpan preferensi pemain menggunakan Windows API native (`GetPrivateProfileIntA` & `WritePrivateProfileStringA`) sehingga preferensi tetap tersimpan saat launcher dibuka kembali.
  - **Tombol Utilitas & Pengujian File**:
    - Tombol **"Verify Files"**: Memvalidasi integritas 4 komponen utama game (`datapack.bin`, `EBOOT_RELOC.ELF`, `persona_pc.exe`, dan `./savedata/`) dengan dialog pop-up konfirmasi lengkap.
    - Tombol **"Change ISO / Re-install"**: Mengembalikan status ke pemilih ISO jika pengguna ingin mengganti file ISO atau memperbarui instalasi.
    - Tombol **"Save Data"** dan **"Screenshots"**: Akses instan langsung membuka folder fisik Windows Explorer (`ShellExecuteA`).
  - Unit test `test_launcher_mgmt.exe` lulus 100% (Default Settings, Custom Settings, Value Clamping, Integrity Verification, Command Line Construction).

---

_Catatan: Dokumen ini disusun sebagai panduan urutan kerja dan tidak langsung dieksekusi sebelum ada instruksi lebih lanjut._
