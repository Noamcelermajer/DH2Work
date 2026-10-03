# Host import surface — Dungeon Hunter 2 HD v1.0.2 (armeabi-v7a)

Exhaustive inventory of everything the guest asks a host for, derived only from bytes in
the shipped binaries. Every number below is the output of a command stated in §0 or §8.

Scope: **Dungeon Hunter 2 HD v1.0.2, 32-bit `armeabi-v7a`**, arm64-only host, Dynarmic
AArch32→AArch64 JIT.

Inputs (all read-only):

| input | path |
| --- | --- |
| pristine engine | `DH2Work-stage\compatibility\work\original\lib\armeabi-v7a\` |
| runtime bundle (extracted) | `DH2Work-stage\compatibility\work\research\ZettaBridge\build\launcher\` |
| guest libs | `…\launcher\assets\zb\guest\lib\*.so` |
| ARM32 sysroot | `…\launcher\assets\zb\sysroot\system\lib\*.so`, `…\system\bin\linker` |
| current host tables | `…\research\ZettaBridge\core\src\gen\*` |
| device evidence | `Dungeon hunter 2 Rework\_device-evidence\dh2work-r1\diag\zb-runtime-report.txt` |

Scratch (all analysis code lives here): `C:\Users\NacWorkstation\Documents\DH2Work-scratch8\`.
Nothing outside the scratch directory and this file was modified.

The engine under analysis is the expected pristine build:

```
$ Get-FileHash "…\original\lib\armeabi-v7a\libDungeonHunter2.so" -Algorithm SHA256
36498EB8180FFB74759E6305E9596DB999F18583D460F3B8534ABCB6022F5E80
```

That matches the stated SHA-256 `36498eb8180ffb74759e6305e9596db999f18583d460f3b8534abcb6022f5e80`
(15 938 284 bytes), so every number below describes the intended artefact.

The corpus is **20 objects**: 3 engine binaries, 7 in `assets/zb/guest/lib/`,
9 in `assets/zb/sysroot/system/lib/`, and `assets/zb/sysroot/system/bin/linker`.

---

## 0. How to reproduce

Toolchain actually present — **note the deviation from the brief**:

```
$ python -c "import capstone, elftools; print(capstone.__version__, elftools.__version__)"
5.0.7 0.33
```

capstone is **5.0.7**, not 5.0.9. pyelftools is 0.33 as stated. `python` is Python 3.13.

Analysis scripts (each is self-contained and prints everything quoted below):

```
cd C:\Users\NacWorkstation\Documents\DH2Work-scratch8
python s01_undef.py       # undefined dynamic symbols, every object
python s03_gl.py          # GL/EGL import lists and the deduplicated set
python s05_hostcalls.py   # hostcalls.inc parse + engine .rel.plt census
python s06_reach.py       # host-call reachability + stub SVC inventory
python s07_verify.py      # stub-SVC-index vs hostcalls.inc cross-check
python s08c_natives.py    # native-registration probe
python s10_jni.py natives # Java_* export census
python s12_jni2.py        # argument-seeded JNIEnv dataflow
python s11_slots.py       # whole-text interface-dispatch histogram
python s13_java.py        # Java signature/class strings
python s14_binding.py     # Java binding sites with resolved string arguments
python s15_syscalls.py    # syscall stub census (ARM-only, superseded)
python s17_mixed.py       # syscalls + reachability, ARM/Thumb-correct
python s20_inv.py         # consolidated inventory (writes out/inventory.json)
python s18_hostsys.py     # syscall numbers the current host implements
```

Shared modules: `fastrel.py` (raw ELF32-ARM reader), `elffino.py`… see `elfinfo.py`
(paths), `mixed.py` (ARM/Thumb mode map + register resolver), `mangle.py` (Itanium
argument parser).

### Two corrections to the brief

1. **The engine imports 92 GL functions, not 91.** §2 proves it two independent ways.
2. **The prior instruction census was ARM-only.** The engine is mixed ARM/Thumb: its
   `.symtab` carries 29 827 `$a`, 1 121 `$t`, 14 152 `$d` mapping symbols. An ARM-only
   decode counts 1 513 919 instructions; a mode-correct decode counts 1 468 826 across
   30 948 ranges. Anything derived by disassembling this corpus as pure ARM (including
   libc.so, which is 2 421 Thumb regions against 724 ARM) is unreliable. Every
   disassembly result below uses the mode map in `mixed.py`.

---

## 1. Undefined-symbol inventory, per guest binary

Method: each object's `.dynsym` entries with `st_shndx == SHN_UNDEF` are collected, then
attributed to the **first object in its own `DT_NEEDED` order whose `.dynsym` actually
defines that name**. Attribution is therefore read out of the files, not guessed from
name prefixes.

```
python s01_undef.py
python s20_inv.py
```

**Grand total: 865 unique undefined dynamic symbols across all 20 shipped objects.**
Of those, **506** are the three engine binaries' import surface.

### 1.1 The three engine binaries

| object | unique undef | libc | libGLESv2 | libm | libstdc++ | liblog | libdl | libGLESv1_CM | libEGL | libandroid | libz |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `libDungeonHunter2.so` | **361** | 221 | **92** | 37 | 9 | 2 | 0 | 0 | 0 | — | — |
| `libStormGLOFT.so` | **126** | 104 | 2 | 11 | 0 | 1 | 3 | 5 | **0** | **0** | 0 |
| `libnativeinterface.so` | **19** | 19 | — | — | — | — | — | — | — | — | — |

Symbol-type split of the engine: **354 `STT_FUNC` + 7 `STT_OBJECT`**
(`_ZSt7nothrow`, `__dso_handle`, `__sF`, `__stack_chk_guard`, `_ctype_`,
`_tolower_tab_`, `_toupper_tab_`).

`DT_NEEDED` lists (from `s02_char.py`):

```
libDungeonHunter2.so  libc.so, libGLESv2.so, libstdc++.so, libm.so, libGLESv1_CM.so, libdl.so, liblog.so
libStormGLOFT.so      liblog.so, libz.so, libm.so, libdl.so, libEGL.so, libGLESv1_CM.so,
                      libGLESv2.so, libandroid.so, libc.so, libstdc++.so
libnativeinterface.so libc.so, libstdc++.so, libm.so, libdl.so
```

**Two `DT_NEEDED` entries are load-only requirements with zero symbolic content.**
The engine names `libGLESv1_CM.so` and `libdl.so` but imports nothing from either; the
GL library names `libEGL.so`, `libandroid.so`, `libz.so` and `libstdc++.so` and imports
nothing from any of them. A host must still be able to *open* those objects for
`DT_NEEDED` resolution, but it must marshal **no** symbol through them for this game.

### 1.2 Guest libraries in `assets/zb/guest/lib/`

These are the objects the current host ships *into* the guest. All five API stubs are
**relocation-free, `DT_NEEDED`-free pure export tables**:

| object | `DT_NEEDED` | relocations | `.rel.plt` | undefined symbols | exported | SVC sites |
| --- | --- | --- | --- | --- | --- | --- |
| `libEGL.so` | ∅ | 0 | 0 | **0** | 46 | 46 (all `0x5A0000\|idx`) |
| `libGLESv1_CM.so` | ∅ | 0 | 0 | **0** | 145 | 145 (all `0x5A0000\|idx`) |
| `libGLESv2.so` | ∅ | 0 | 0 | **0** | 259 | 259 (all `0x5A0000\|idx`) |
| `libandroid.so` | ∅ | 0 | 0 | **0** | 184 | 184 (all `0x5A0000\|idx`) |
| `libjnigraphics.so` | ∅ | 0 | 0 | **0** | 3 | 3 (all `0x5A0000\|idx`) |
| `libzbcompat.so` | `libdl.so`, `libc.so` | 5 | 2 | **2** (`libc.so`: `__cxa_atexit`, `__cxa_finalize`) | 1 | 0 |
| `libzbjni.so` | `libdl.so`, `libc.so` | 254 | 11 | **12** (`libc.so`: `__cxa_atexit`, `__cxa_finalize`, `__sF`, `abort`, `calloc`, `free`, `fwrite`, `malloc`, `memset`, `snprintf`, `strdup`, `strlen`) | 0 | 59 (all `0x5AFC00\|idx`) |

The five API stubs issue **zero syscalls** and import **zero symbols**. Their entire body
is one `svc` per exported function (ARM mode — `gen_jni.py`/`gen_stubs.py` emit `.arm`),
carrying host-call index in the low 16 bits. `libzbjni.so` is the same mechanism on a
separate table (`0x5AFC00`). See §3 and §4.

### 1.3 Sysroot libraries and the linker

| object | unique undef | provider groups |
| --- | --- | --- |
| `sysroot/libc.so` | 13 | `ld-android.so` 4, `libdl.so` 8, unresolved 1 (`__scudo_default_options`) |
| `sysroot/libc++.so` | 174 | `libc.so` 174 |
| `sysroot/libm.so` | 6 | `libc.so` 6 (`__cxa_finalize`, `__stack_chk_fail`, `__stack_chk_guard`, `isnanf`, `ldexp`, `memset`) |
| `sysroot/liblog.so` | 88 | `libc++.so` 13, `libc.so` 75 |
| `sysroot/libstdc++.so` | 19 | `libc.so` 19 |
| `sysroot/libz.so` | 21 | `libc.so` 21 |
| `sysroot/libdl.so` | 13 | `ld-android.so` 13 (`__loader_*`) |
| `sysroot/libdl_android.so` | 9 | `ld-android.so` 9 (`__loader_*`) |
| `sysroot/ld-android.so` | **0** | — |
| `sysroot/linker` | 2 | unresolved 2 (`__gcov_dump`, `__gcov_flush`) |

`ld-android.so` is a 2 868-byte **stub**: it defines the 26 `__loader_*` entry points
`libdl.so` and `libdl_android.so` need, and imports nothing. Its entire code section
decodes to a single instruction. A purpose-built host can synthesise those 26 symbols
directly instead of shipping the file.

Full `__loader_*` set the loader shim must provide (union of the two columns above):

```
__loader_add_thread_local_dtor          __loader_dl_iterate_phdr
__loader_android_create_namespace       __loader_dl_unwind_find_exidx
__loader_android_dlopen_ext             __loader_dladdr
__loader_android_dlwarning              __loader_dlclose
__loader_android_get_LD_LIBRARY_PATH    __loader_dlerror
__loader_android_get_application_target_sdk_version
__loader_android_get_exported_namespace __loader_dlopen
__loader_android_handle_signal          __loader_dlsym
__loader_android_init_anonymous_namespace __loader_dlvsym
__loader_android_link_namespaces        __loader_remove_thread_local_dtor
__loader_android_set_16kb_appcompat_mode __loader_shared_globals
__loader_android_set_application_target_sdk_version
__loader_android_update_LD_LIBRARY_PATH
__loader_cfi_fail
```

Plus the four `libc.so` → `ld-android.so` symbols
(`__loader_add_thread_local_dtor`, `__loader_android_get_exported_namespace`,
`__loader_remove_thread_local_dtor`, `__loader_shared_globals`).

### 1.4 Full symbol lists for the groups that matter

#### `libDungeonHunter2.so` → `libc.so` (221 symbols)

These 221 are the *entire* libc surface the engine needs — note it is a single libc, not
libc/libm/libdl/libpthread/libstdc++ split; bionic folds them together.

```
__aeabi_atexit __aeabi_d2f __aeabi_d2iz __aeabi_d2uiz __aeabi_dadd __aeabi_dcmpeq
__aeabi_dcmpge __aeabi_dcmpgt __aeabi_dcmple __aeabi_dcmplt __aeabi_dcmpun __aeabi_ddiv
__aeabi_dmul __aeabi_dsub __aeabi_f2d __aeabi_f2iz __aeabi_fadd __aeabi_fcmpeq
__aeabi_fcmpge __aeabi_fcmpgt __aeabi_fcmple __aeabi_fcmplt __aeabi_fdiv __aeabi_fmul
__aeabi_fsub __aeabi_i2d __aeabi_i2f __aeabi_idiv __aeabi_idivmod __aeabi_ldivmod
__aeabi_lmul __aeabi_ui2d __aeabi_ui2f __aeabi_uidiv __aeabi_uidivmod __aeabi_ul2d
__aeabi_uldivmod __aeabi_unwind_cpp_pr0 __aeabi_unwind_cpp_pr1 __assert2 __dso_handle
__errno __isfinitef __sF __stack_chk_fail __stack_chk_guard _ctype_ _tolower_tab_
_toupper_tab_ abort accept atoi atol bind calloc chdir clock close closedir connect exit
fclose fcntl feof fflush fgetpos fgets fopen fprintf fputc fputs fread free fseek fsetpos
fstat ftell fwrite getc getenv gethostbyaddr gethostbyname gethostname getpeername
getsockname getsockopt gettimeofday gmtime inet_addr inet_ntoa ioctl isalnum isalpha
iscntrl islower ispunct isspace isupper iswalpha iswcntrl iswdigit iswlower iswprint
iswpunct iswspace iswupper iswxdigit isxdigit ldexp listen localtime localtime_r longjmp
lrand48 lseek malloc memchr memcmp memcpy memmove memset mktime mmap munmap nanosleep open
opendir printf pthread_attr_destroy pthread_attr_init pthread_attr_setdetachstate
pthread_create pthread_exit pthread_getschedparam pthread_getspecific pthread_join
pthread_key_create pthread_kill pthread_mutex_destroy pthread_mutex_init
pthread_mutex_lock pthread_mutex_trylock pthread_mutex_unlock pthread_mutexattr_init
pthread_mutexattr_settype pthread_self pthread_setschedparam pthread_setspecific putc
putchar puts qsort read readdir realloc recv recvfrom rename sched_get_priority_max
sched_get_priority_min sched_yield select send sendto setjmp setlocale setsockopt setvbuf
sigaction snprintf socket sprintf srand48 sscanf strcasecmp strcat strchr strcmp strcoll
strcpy strcspn strdup strerror_r strftime strlcpy strlen strncasecmp strncat strncmp
strncpy strpbrk strrchr strstr strtod strtok strtol strtoul swprintf sysconf time toupper
towlower towupper uname ungetc unlink usleep vprintf vsnprintf vsprintf wcscat wcscmp
wcscpy wcslen wcsncmp wcsncpy wmemcmp wmemcpy wmemmove wmemset write
```

Grouped for the host author:

* **ARM EABI runtime helpers (39)** — `__aeabi_*` plus `__aeabi_unwind_cpp_pr0/pr1`.
  These are *not* libc API surface; they are compiler-emitted helpers. On a Dynarmic host
  they are pure guest code and need a real implementation only if the guest calls them.
* **pthreads (18)** — the engine is genuinely multithreaded.
* **sockets (14)** — `accept bind connect getpeername getsockname getsockopt gethostbyaddr
  gethostbyname gethostname inet_addr inet_ntoa listen recv recvfrom select send sendto
  setsockopt socket`. Networking is a **gameplay-phase** requirement, not a boot one.
* **stdio (33)**, **string (30)**, **wchar (16)**, **ctype (21)** — ordinary libc.
* **`mmap` / `munmap`** — memory-shaped, see §5.
* **missing entirely**: no `dlopen`/`dlsym`/`dlclose` in the engine, no `EGL*`, no
  `ANativeWindow_*`, no `AAsset*`, no `__android_log_*` beyond two calls.

#### `libDungeonHunter2.so` → `libm.so` (37)

```
__aeabi_d2lz acos acosf asin asinf atan atan2 atan2f atanf ceil ceilf cos cosf cosh exp
expf floor floorf fmod fmodf frexp ldexpf log log10 logf modf modff pow powf sin sinf
sinh sqrt sqrtf tan tanf tanh
```

#### `libDungeonHunter2.so` → `libstdc++.so` (9) and `liblog.so` (2)

```
_ZSt7nothrow _ZdaPv _ZdlPv _Znaj _ZnajRKSt9nothrow_t _Znwj __cxa_guard_acquire
__cxa_guard_release __cxa_pure_virtual
__android_log_print __android_log_write
```

The engine is built against **GNU libstdc++ (bionic)**, not libc++. Only 9 C++ runtime
symbols. `libc++.so` is present in the sysroot but **the engine never touches it**;
`libc++.so` is pulled in only by `liblog.so`.

#### `libnativeinterface.so` → `libc.so` (19)

```
__aeabi_uidivmod __aeabi_unwind_cpp_pr0 __aeabi_unwind_cpp_pr1 __stack_chk_fail
__stack_chk_guard fclose fopen fread free fwrite malloc memcmp memset sprintf strcat
strlen strncpy tolower wcslen
```

This is the Samsung "zirconia" licence-check shim. It is tiny and self-contained.

#### `libStormGLOFT.so` → `libc.so` (104), `libm.so` (11), `libdl.so` (3)

```
libm:   ceilf cos exp exp2 floor floorf log pow sin sqrtf truncf
libdl:  dlclose dlopen dlsym
liblog: __android_log_print
```

`libdl.so` imports matter: **the GL library resolves three symbols dynamically.** That is
the only dynamic-lookup surface in the corpus (§7).

---

## 2. GL/EGL surface, exactly

### 2.1 The engine really does import 92 GL and 0 EGL

Verified two independent ways, both stated as commands.

**Method A — undefined dynamic symbols.**

```
python s03_gl.py
```
→ `libDungeonHunter2.so`: total unique undefined = 361, of which
`libGLESv2.so 92`, `libEGL.so` **absent**, `libGLESv1_CM.so` **absent**.
All 92 are `STT_FUNC`; 0 `gl*`/`egl*` symbol is non-`FUNC`.

**Method B — PLT relocations.**

```
python s05_hostcalls.py
```
→ engine `.rel.plt`: **351** relocations, all `R_ARM_JUMP_SLOT`, **351 distinct symbols**.
`gl*` in `.rel.plt` = **92**; `egl*` in `.rel.plt` = **0**.
`gl*` in `DYNSYM-UNDEF` = **92**; set difference in both directions = ∅.

`.plt` is `0x1088` = 4232 bytes = 20-byte header + 351 × 12, so the veneer↔symbol mapping
is a bijection over all 351 veneers — the "resolved all 351 PLT veneers" claim reproduces
exactly.

**So the correct figure is 92 / 0. The brief's "91" is off by one.** No GL symbol is
reachable only through a non-PLT relocation, and no GL import is duplicated.

**Method C — string content (independent corroboration).** Scanning `.rodata` for every
printable run matching `(egl|gl|GL_|EGL_)[A-Za-z0-9_]*`:

```
python s19_glcount.py
```
→ `libDungeonHunter2.so`: the complete set of `gl*` strings is **exactly the 92 imported
names**; there is **no `egl*` string anywhere in the engine**, and no GL name outside the
92. This rules out `dlsym`-style GL extension use by the engine.

### 2.2 What the shipped guest stubs need

The five API stubs (`libGLESv2.so`, `libGLESv1_CM.so`, `libEGL.so`, `libandroid.so`,
`libjnigraphics.so`) import **zero** symbols and have **zero** relocations — their need is
entirely determined by what other objects import *from* them. The only objects that import
from a GL/EGL stub are:

| importer | from `libGLESv2.so` | from `libGLESv1_CM.so` | from `libEGL.so` |
| --- | --- | --- | --- |
| `libDungeonHunter2.so` | 92 | 0 | 0 |
| `libStormGLOFT.so` | 2 (`glGetShaderiv`, `glShaderSource`) | 5 (`glGetError`, `glGetString`, `glScissor`, `glTexImage2D`, `glViewport`) | 0 |

All 7 of `libStormGLOFT.so`'s GL names are already members of the engine's 92, so the
union is 92. Nothing in `assets/zb/guest/lib/` or the sysroot imports a GL/EGL symbol.

### 2.3 The deduplicated entry-point set a host must marshal for this game

**92 entry points, 0 of them EGL.**

```
glActiveTexture glAttachShader glBindBuffer glBindFramebuffer glBindRenderbuffer
glBindTexture glBlendColor glBlendEquation glBlendFunc glBufferData glBufferSubData
glCheckFramebufferStatus glClear glClearColor glClearDepthf glClearStencil glColorMask
glCompileShader glCompressedTexImage2D glCompressedTexSubImage2D glCopyTexSubImage2D
glCreateProgram glCreateShader glCullFace glDeleteBuffers glDeleteFramebuffers
glDeleteProgram glDeleteRenderbuffers glDeleteShader glDeleteTextures glDepthFunc
glDepthMask glDepthRangef glDisable glDisableVertexAttribArray glDrawArrays glDrawElements
glEnable glEnableVertexAttribArray glFlush glFramebufferRenderbuffer
glFramebufferTexture2D glFrontFace glGenBuffers glGenFramebuffers glGenRenderbuffers
glGenTextures glGenerateMipmap glGetActiveAttrib glGetActiveUniform glGetAttribLocation
glGetError glGetFloatv glGetIntegerv glGetProgramInfoLog glGetProgramiv glGetShaderInfoLog
glGetShaderiv glGetString glGetUniformLocation glHint glLineWidth glLinkProgram
glPixelStorei glPolygonOffset glReadPixels glRenderbufferStorage glSampleCoverage glScissor
glShaderSource glStencilFunc glStencilMask glStencilOp glTexImage2D glTexParameterf
glTexParameteri glTexSubImage2D glUniform1f glUniform1fv glUniform1i glUniform1iv
glUniform2fv glUniform2iv glUniform3fv glUniform3iv glUniform4fv glUniform4iv
glUniformMatrix4fv glUseProgram glVertexAttrib4f glVertexAttribPointer glViewport
```

Full GLES2 core (no ES 3.x, no extension entry points, no vertex-array-object OES path, no
map-buffer, no `glDiscardFramebuffer`, no `glGetStringi`).

### 2.4 Reduction against the current host

The current host's generated entry points, counted from its own tables
(`python s19_glcount.py`, `Get-Content hostcalls.inc | …`):

| table | count | source of the count |
| --- | --- | --- |
| `hostcalls.inc` rows for `libGLESv2.so` | **259** | 579 rows parsed, grouped by library |
| `hostcalls.inc` rows for `libGLESv1_CM.so` | **87** | ibid. |
| `hostcalls.inc` rows for `libEGL.so` | **46** | ibid. |
| **GL + EGL subtotal** | **392** | 259 + 87 + 46 |
| `hostcalls.inc` rows for `libandroid.so` | 184 | ibid. |
| `hostcalls.inc` rows for `libjnigraphics.so` | 3 | ibid. |
| **all host-call rows** | **579** | `hostcalls.inc` line count 580 incl. comment |
| `gl_dispatch.inc` `case ZB_GL_HC_*` labels | 258 | regex count |
| `egl_dispatch.inc` `case ZB_EGL_HC_*` labels | 44 | regex count |
| `gl_unsupported_extensions.inc` extension names | 149 | regex count |

(The brief's "~490" is close to 392 GL+EGL+ …; if the `libandroid` rows are included the
figure is 576. The **canonical, exactly reproducible GL/EGL figure is 392**.)

**Reduction:**

| measure | value |
| --- | --- |
| GL/EGL entry points the host ships | 392 |
| GL/EGL entry points this game needs | **92** |
| droppable | **300** |
| reduction | **76.5 %** |
| EGL entry points the host ships | 46 |
| EGL entry points this game needs | **0** |
| reduction | **100 %** |
| `libGLESv1_CM` entry points needed | **0** (the 5 names used survive in the GLES2 index space) |
| `libandroid` entry points ships → needed | 184 → **0** |
| `libjnigraphics` ships → needed | 3 → **0** |

The `libandroid` and `libjnigraphics` zeros are the largest single scoping win in this
document: `libStormGLOFT.so` lists `libandroid.so` in `DT_NEEDED` but imports **no** symbol
from it, and no other object does either.

**One thing that must not be deleted:** `libEGL.so`, `libandroid.so` and `libGLESv1_CM.so`
must still exist as *loadable* objects, because `DT_NEEDED` names them (§1.1). They need to
export only the symbols actually looked up, and nothing looks any up — an empty stub with
the right `SONAME` suffices.

---

## 3. Guest → host call surface

### 3.1 The mechanism, confirmed from the host source

```
core/src/process.cpp:69   // svc #(0x5A0000 | index) from the generated stub libraries (tools/gen_stubs.py).
core/src/process.cpp:70   constexpr std::uint32_t kHostCallBase = 0x5A0000;
core/src/process.cpp:523  if ((stop.swi & 0xFF0000u) == kHostCallBase) {
core/src/process.cpp:524      const std::uint32_t index = stop.swi & 0xFFFFu;
```

So a guest→host call is an ARM `svc #(0x5A0000 | index)` and `hostcalls.inc` is the
`index → (library, symbol)` table. A second table lives at `0x5AFC00 | index`
(`jni_hostcalls.inc`, 59 rows) — see §4.

### 3.2 Method

1. Parse `hostcalls.inc` → `index → (library, name)` (579 rows, 579 distinct pairs, no
   duplicate index).
2. For every shipped object, take its `DYNSYM` undefined names and attribute them to
   `DT_NEEDED` providers (§1), keeping only the five host-call libraries.
3. Translate each surviving `(library, name)` to its host-call index using the table — and
   if the named library does not carry that symbol, fall back to the library that does
   (this is how the GLES1/GLES2 shared names resolve).
4. **Independently prove the index assignment** by disassembling every exported function of
   every stub library and reading the `svc` immediate out of its body, then comparing to
   `hostcalls.inc` (`python s06_reach.py`, `python s07_verify.py`).

Step 4 is the load-bearing step: it reads the index the *guest* will actually trap with,
out of the shipped bytes, instead of trusting the table.

### 3.3 Result of the stub-side proof

```
python s07_verify.py
→ functions whose SVC index disagrees with hostcalls.inc: 0
→ table=579  union_of_stub_indices=579  missing=0
```

Every stub function's `svc` immediate matches its `hostcalls.inc` index, and the union of
all stub indices is exactly `{0 … 578}` — the table is complete and consistent with the
shipped stubs. Per-library index ranges:

| stub | exported functions | SVC indices carried |
| --- | --- | --- |
| `libGLESv2.so` | 259 | `0–141`, `216`, `228–343` |
| `libGLESv1_CM.so` | 145 | `0,3,6,…,141` (shared GLES2 indices) + `492–578` |
| `libandroid.so` | 184 | `142–167`, `212–213`, `220–227`, `344–491` |
| `libEGL.so` | 46 | `168–211`, `214–215` |
| `libjnigraphics.so` | 3 | `217–219` |

### 3.4 Reachable subset

```
python s06_reach.py
```

Objects importing from a host-call library:

```
engine/libDungeonHunter2.so   libGLESv2.so=92
engine/libStormGLOFT.so       libGLESv1_CM.so=5, libGLESv2.so=2
```

Nothing else — not `libnativeinterface.so`, not `libzbcompat.so`, not `libzbjni.so`, not
any sysroot library, and not the engine's `libGLESv1_CM.so`/`libdl.so` `DT_NEEDED` entries.

**Result: 92 of the 579 host-call indices are reachable. All 92 are `libGLESv2.so`
indices. 487 are unreachable.**

| library | shipped | reachable | unreachable |
| --- | --- | --- | --- |
| `libGLESv2.so` | 259 | **92** | 167 |
| `libGLESv1_CM.so` | 87 | **0** | 87 |
| `libEGL.so` | 46 | **0** | 46 |
| `libandroid.so` | 184 | **0** | 184 |
| `libjnigraphics.so` | 3 | **0** | 3 |
| **total** | **579** | **92** | **487** |

Reduction: **84.1 %**.

The 92 indices, with their names, are exactly the 92 GL symbols of §2.3, in
`hostcalls.inc` order. Format: `hostcalls.inc index  symbol`.

```
  0  glActiveTexture
  1  glAttachShader
  3  glBindBuffer
  4  glBindFramebuffer
  5  glBindRenderbuffer
  6  glBindTexture
  7  glBlendColor
  8  glBlendEquation
 10  glBlendFunc
 12  glBufferData
 13  glBufferSubData
 14  glCheckFramebufferStatus
 15  glClear
 16  glClearColor
 17  glClearDepthf
 18  glClearStencil
 19  glColorMask
 20  glCompileShader
 21  glCompressedTexImage2D
 22  glCompressedTexSubImage2D
 24  glCopyTexSubImage2D
 25  glCreateProgram
 26  glCreateShader
 27  glCullFace
 28  glDeleteBuffers
 29  glDeleteFramebuffers
 30  glDeleteProgram
 31  glDeleteRenderbuffers
 32  glDeleteShader
 33  glDeleteTextures
 34  glDepthFunc
 35  glDepthMask
 36  glDepthRangef
 38  glDisable
 39  glDisableVertexAttribArray
 40  glDrawArrays
 41  glDrawElements
 42  glEnable
 43  glEnableVertexAttribArray
 45  glFlush
 46  glFramebufferRenderbuffer
 47  glFramebufferTexture2D
 48  glFrontFace
 49  glGenBuffers
 50  glGenFramebuffers
 51  glGenRenderbuffers
 52  glGenTextures
 53  glGenerateMipmap
 54  glGetActiveAttrib
 55  glGetActiveUniform
 57  glGetAttribLocation
 60  glGetError
 61  glGetFloatv
 63  glGetIntegerv
 64  glGetProgramInfoLog
 65  glGetProgramiv
 67  glGetShaderInfoLog
 70  glGetShaderiv
 71  glGetString
 74  glGetUniformLocation
 80  glHint
 88  glLineWidth
 89  glLinkProgram
 90  glPixelStorei
 91  glPolygonOffset
 92  glReadPixels
 94  glRenderbufferStorage
 95  glSampleCoverage
 96  glScissor
 98  glShaderSource
 99  glStencilFunc
101  glStencilMask
103  glStencilOp
105  glTexImage2D
106  glTexParameterf
108  glTexParameteri
110  glTexSubImage2D
111  glUniform1f
112  glUniform1fv
113  glUniform1i
114  glUniform1iv
116  glUniform2fv
118  glUniform2iv
120  glUniform3fv
122  glUniform3iv
124  glUniform4fv
126  glUniform4iv
129  glUniformMatrix4fv
130  glUseProgram
138  glVertexAttrib4f
140  glVertexAttribPointer
141  glViewport
```

Cross-check against the device run: `zb-runtime-report.txt` line 12-13 record
`unimplemented-host-calls: 0` / `unimplemented-distinct: 0`, i.e. the run that reached the
rendering path never asked for a host call outside this set. (It also records
`gl-calls: 0` — the run crashed before the first GL call, so this is a *permitted* set
confirmed by absence of failures, not an exercised set.)

---

## 4. JNI / ART surface

### 4.1 Registered natives: 32 + 4 = 36 — exactly the device figure

```
python s10_jni.py natives
```
→ `libDungeonHunter2.so` `.dynsym`: **32** defined `Java_*` symbols.
→ `libnativeinterface.so` `.dynsym`: **4** defined `Java_*` symbols.
→ `libStormGLOFT.so`: **0** (it exports `JNI_OnLoad` at `0x3713c` but no natives).

`zb-runtime-report.txt` line 11: `registered-natives: 36`. **32 + 4 = 36.** The host
registers natives by scanning the library's exported `Java_*` symbols, so the count is a
direct read of `.dynsym`.

`JNI_OnLoad`: engine `0x53224c`, `libStormGLOFT.so` `0x3713c` → 2, matching
`jni-onload-calls: 2` and the two `jni-onload:` lines (JNI 1.4 and JNI 1.6).

The 32 engine natives form 7 Java classes:

| Java class | natives |
| --- | --- |
| `com/gameloft/android/GAND/GloftD2SS/DungeonHunter2` | `nativeAccelerometer`, `nativeCanInterrupt`, `nativeGetGameMusicVolume`, `nativeGetInfo`, `nativeInit`, `nativeKeyDown`, `nativeKeyUp`, `nativeOpenIGM`, `nativePause`, `nativeResume`, `nativeSetOrientation`, `nativeSetPhone`, `nativegetState`, `nativeonTrackballEvent` (14) |
| `…/GameRenderer` | `nativeConfig`, `nativeDone`, `nativeGameRenderer`, `nativeGetJNIEnv`, `nativeInit`, `nativeOnDrawFrame`, `nativeOnSurfaceChanged`, `nativeRender`, `nativeResize` (9) |
| `…/GLMediaPlayer` | `nativeGetTotalSounds`, `nativeGetTotalSoundsOfSameInstance`, `nativeInit`, `nativeSetStopOnMusic` (4) |
| `…/Musicplayer` | `nativeDisplayMusicTitle`, `nativeInitplayer` (2) |
| `…/GLUtils_Device` | `nativeInit` (1) |
| `…/GLResLoader` | `nativeInit` (1) |
| `…/GameGLSurfaceView` | `nativeOnTouch` (1) |

`libnativeinterface.so` provides `com/samsung/zirconia/NativeInterface`:
`checkLicenseFile`, `checkLicenseFile2`, `doPassphraseTest`, `storeLicenseKey` (4).

`nativeGetJNIEnv` is a notable one: the engine asks Java for a `JNIEnv` back into Java.

### 4.2 Java methods the guest asks for

Method: locate every interface dispatch through slot 6 (`FindClass`), 94 (`GetFieldID`),
113 (`GetStaticMethodID`), 144 (`GetStaticFieldID`) and every `bl` to the out-of-line
`_JNIEnv::GetMethodID` wrapper at `0x88ed78`; then resolve the argument registers
backwards to string literals. The engine is `-fPIC`, so a string is materialised as
`ldr rX,[pc,#off]` + `add rX, pc, rX`; a resolver that treats the literal as the pointer
finds nothing (this was verified — the naive version returned 0 of 6). `s14_binding.py`
implements the two-instruction PIC form.

```
python s14_binding.py
```
→ **19 binding sites**, all resolved:

| slot | function | name | signature |
| --- | --- | --- | --- |
| 6 | `FindClass` | — | `android/content/Context` |
| 33 | `GetMethodID` | `<init>` | `(IIIIII)V` |
| 33 | `GetMethodID` | `play` | `()V` |
| 33 | `GetMethodID` | `pause` | `()V` |
| 33 | `GetMethodID` | `stop` | `()V` |
| 33 | `GetMethodID` | `release` | `()V` |
| 33 | `GetMethodID` | `write` | `([BII)I` |
| 113 | `GetStaticMethodID` | `a` | `()[B` |
| 113 | `GetStaticMethodID` | `b` | `()[B` |
| 113 | `GetStaticMethodID` | `c` | `()[B` |
| 113 | `GetStaticMethodID` | `d` | `()[B` |
| 113 | `GetStaticMethodID` | `IsWifiEnable` | `()Z` |
| 113 | `GetStaticMethodID` | `getHostName` | `()[B` |
| 113 | `GetStaticMethodID` | `e` | `(I)V` |
| 113 | `GetStaticMethodID` | `f` | `()[B` |
| 113 | `GetStaticMethodID` | `d` | `()V` |
| 113 | `GetStaticMethodID` | `db` | `()[B` |
| 113 | `GetStaticMethodID` | `dc` | `()[B` |
| 113 | `GetStaticMethodID` | `da` | `()Landroid/app/Activity;` |

The six `GetMethodID` calls are an `android/media/AudioTrack` binding —
constructor `(IIIIII)V`, `play`, `pause`, `stop`, `release`, `write([BII)I` — which matches
the `android/media/AudioTrack` class descriptor found in `.rodata`. The obfuscated
one-letter statics (`a`–`f`, `db`/`dc`/`da`) belong to the ProGuard-renamed game classes
whose descriptors also appear in `.rodata` (`b/k`, `d/2`, `d/X`, `d/c`, `h/D`, `h/E`,
`h/I`, `h/S`, `h/f`, `l/c`, `l/x`, `p/9`, `p/R`, `t/2`, `t/L`, `x/V`, `x/b`).

String-level Java surface (`python s13_java.py`): **24 JNI signature strings** and
**22 class descriptors** in the engine. The full signature set:

```
()I ()Landroid/app/Activity; ()Ljava/lang/String; ()V ()Y ()Z ()[B (I)I (I)V (I)[B
(IFI)V (IFII)V (II)I (II)V (IIF)V (III)I (IIIIII)V (Ljava/lang/String;)I
(Ljava/lang/String;)Ljava/lang/Object; (Ljava/lang/String;)V (Ljava/lang/String;)[B
(Ljava/lang/String;I)I (Ljava/lang/String;II)[B ([BII)I
```

Non-obfuscated class descriptors: `android/content/Context`, `android/media/AudioTrack`,
`java/lang/String`, `data/scripts/test`, `level/combat_formulas`, `level/death_scripts`.

Note these are **Java→native** callbacks, not host-call indices: input
(`nativeOnTouch`, `nativeKeyDown`, `nativeKeyUp`, `nativeonTrackballEvent`), lifecycle and
sensors (`nativeAccelerometer`, `nativeSetOrientation`), audio
(`nativeInitplayer`, `nativeDisplayMusicTitle`), and the render loop
(`nativeOnDrawFrame`, `nativeRender`, `nativeOnDrawFrame`).

### 4.3 `JNIEnv` / `JavaVM` function-table slots touched

Two static methods, unioned.

**(a) Out-of-line C++ wrappers.** The engine is a C++ NDK build, so it calls JNI through
jni.h's `_JNIEnv` wrapper. Each wrapper that the compiler emits out-of-line is one tail
call through the interface table:

```
python s10_jni.py slots
```

| wrapper | slot | jni.h `V`-variant |
| --- | --- | --- |
| `_JNIEnv::NewObject` | **29** | `NewObjectV` |
| `_JNIEnv::GetMethodID` | **33** | — |
| `_JNIEnv::CallObjectMethod` | **35** | `CallObjectMethodV` |
| `_JNIEnv::CallNonvirtualIntMethod` | **80** | `CallNonvirtualIntMethodV` |
| `_JNIEnv::CallNonvirtualVoidMethod` | **92** | `CallNonvirtualVoidMethodV` |
| `_JNIEnv::CallStaticObjectMethod` | **115** | `CallStaticObjectMethodV` |
| `_JNIEnv::CallStaticBooleanMethod` | **118** | `CallStaticBooleanMethodV` |
| `_JNIEnv::CallStaticIntMethod` | **130** | `CallStaticIntMethodV` |
| `_JNIEnv::CallStaticVoidMethod` | **142** | `CallStaticVoidMethodV` |

The decode is `ldr ip,[r0]` + `ldr pc,[ip,#imm]`, slot = `imm/4`. `GetMethodID` → `0x84/4 =
33` and `CallNonvirtualVoidMethod` → `0x170/4 = 92` both match the standard
`JNINativeInterface` order, which validates the slot numbering. Variadic wrappers route to
the `V` slot; that is jni.h's own behaviour, not an inference.

**(b) Argument-seeded dataflow.** For every function whose *mangled name* says it takes a
`_JNIEnv*`/`_JavaVM*` argument (the Itanium argument list is parsed, so the argument index
is read from the symbol), seed that argument register and follow the table-dispatch idiom:

```
python s12_jni2.py
```
→ slots **6 `FindClass`**, **21 `NewGlobalRef`**, **113 `GetStaticMethodID`**,
**169 `GetStringUTFChars`**.

**Union: 13 JNIEnv slots.**

```
6 FindClass            21 NewGlobalRef         29 NewObjectV
33 GetMethodID         35 CallObjectMethodV    80 CallNonvirtualIntMethodV
92 CallNonvirtualVoidMethodV                   113 GetStaticMethodID
115 CallStaticObjectMethodV                    118 CallStaticBooleanMethodV
130 CallStaticIntMethodV                       142 CallStaticVoidMethodV
169 GetStringUTFChars
```

`JavaVM` slots: **0 found** by either method. See §7.

**Independent corroboration from the device.**
`zb-runtime-report.txt` line 111 `crash-recent-jni-calls` is the host's ring buffer of the
JNI host calls the guest made before crashing:

```
Register GetStringUTFLength GetStringLength GetStringUTFRegion   (×3 repeats)
NewGlobalRef GetMethodID ×9
```

Every one of those is explained by this slot set:
* `GetStringUTFLength` + `GetStringLength` + `GetStringUTFRegion` is exactly what the guest
  JNI shim (`guest/zbjni/zbjni.c` → `zbjni_hc_GetStringUTFLength`, `…GetStringLength`,
  `…GetStringUTFRegion`) must issue to service **slot 169 `GetStringUTFChars`**. The static
  scan found 169 and *not* 171 (`GetArrayLength`)/200 (`GetByteArrayRegion`) alone — the
  runtime agrees.
* `NewGlobalRef` = slot 21 ✔ (found statically).
* `GetMethodID` = slot 33 ✔ (found statically, as an out-of-line wrapper).
* `Register` is the host-protocol call `0xFC00` (`ZB_JNI_HC_Register`), **not** a JNIEnv
  slot — it registers the guest's interface table with the host. It is not in the 233.

Two independent artefacts — a static scan of the binary and a runtime ring buffer from a
different device run — agree on the *name* set. That is as far as corroboration goes; only
`NewGlobalRef` and `GetMethodID` are confirmed exercised, the rest are confirmed *permitted*.

The host's JNI protocol tables (`jni_hostcalls.inc`, base `0x5AFC00`, 59 rows) are a
different thing again: they are the *shim→host* calls (`Register`, `GetMethodID`,
`GetStringUTFRegion`, …), not the guest's slots. `libzbjni.so`'s 59 functions
`zbjni_hc_<Name>` each carry exactly one of them and nothing else (§1.2, `python s07_verify.py`).

---

## 5. Syscall surface

### 5.1 Method and the ARM/Thumb correction

ARM EABI: syscall number in `r7`, trap with `svc #0`. The first attempt decoded every
object as ARM and produced a plausible-but-wrong call graph (libc.so "2700 `bl`
instructions" of which 2320 had impossible targets). The cause: **every object in this
corpus is mixed ARM/Thumb**. `python s17_mixed.py` builds the mode map from the `$a`/`$t`
mapping symbols (skipping `$d`) and falls back to the `STT_FUNC` Thumb bit when an object
has no mapping symbols:

| object | mode ranges | ARM | Thumb |
| --- | --- | --- | --- |
| `libDungeonHunter2.so` | 30 948 | 29 827 | 1 121 |
| `libStormGLOFT.so` | 1 498 | 170 | 1 328 |
| `sysroot/libc.so` | 3 145 | 724 | 2 421 |
| `sysroot/linker` | 4 146 | 250 | 3 896 |
| `sysroot/libc++.so` | 1 370 | 0 | 1 370 |

### 5.2 Census: which syscall stubs physically exist

```
python s17_mixed.py
```

| object | `svc` sites | sites with resolvable number | distinct syscall numbers | non-syscall SVCs |
| --- | --- | --- | --- | --- |
| `sysroot/libc.so` | 240 | 231 | **230** | 3 unresolved |
| `sysroot/linker` | 273 | 231 | **229** | 44 other |
| `sysroot/libc++.so` | 40 | 0 | 0 | all other |
| `sysroot/libm.so` | 2 | 0 | 0 | all other |
| `sysroot/liblog.so` | 2 | 0 | 0 | all other |
| `sysroot/libz.so`, `libdl.so`, `libdl_android.so`, `libstdc++.so`, `ld-android.so` | 0 | 0 | 0 | — |
| `engine/libDungeonHunter2.so` | **0** | 0 | 0 | — |
| `engine/libnativeinterface.so` | **0** | 0 | 0 | — |
| `engine/libStormGLOFT.so` | 46 | 0 | 0 | 5 unresolved, rest data |
| `guest/libGLESv2.so` | 259 | 0 | 0 | all `0x5A0000\|idx` |
| `guest/libGLESv1_CM.so` | 145 | 0 | 0 | all `0x5A0000\|idx` |
| `guest/libandroid.so` | 184 | 0 | 0 | all `0x5A0000\|idx` |
| `guest/libEGL.so` | 46 | 0 | 0 | all `0x5A0000\|idx` |
| `guest/libjnigraphics.so` | 3 | 0 | 0 | all `0x5A0000\|idx` |
| `guest/libzbjni.so` | 59 | 0 | 0 | all `0x5AFC00\|idx` |
| `guest/libzbcompat.so` | 0 | 0 | 0 | — |

Three facts worth stating plainly:

1. **Neither `libDungeonHunter2.so` nor `libnativeinterface.so` contains a single `svc`
   instruction.** They reach the kernel only through `libc.so`.
2. **The five GL/EGL/android/jnigraphics stubs issue no syscalls at all** — every trap they
   contain is a host call. A host that implements the 92-entry GL surface plus `libc.so`
   covers them completely.
3. **libc.so carries one stub per ARM syscall (230 distinct numbers); the linker carries
   229.** That is a bionic build artefact, not a requirement. Counting stubs is the
   over-approximation the brief warns against.

### 5.3 Reachability: the required subset

Method: build libc.so's intra-object call graph and BFS from the libc functions the guest
actually imports.

* Nodes: 2 871 `STT_FUNC` symbols in libc.so's `.symtab`.
* Edges: direct `bl`/`b` to an address inside `.text` (7 823), plus calls to the object's
  own PLT decoded to their `DT_NEEDED` symbol via the veneer/GOT-slot technique (4 580
  resolved, 36 unresolved). 280 indirect (`blx rN`) calls are **not** followed — see §7.
* Seeds: the **404** distinct libc.so functions imported by name from the engine binaries,
  the guest libraries and the other sysroot libraries.

```
python s17_mixed.py
```
→ 802 of 2 871 libc functions reachable → **61 distinct ARM syscall numbers**.

```
  1 exit                 3 read                 4 write                6 close
 12 chdir               19 lseek               54 ioctl               78 gettimeofday
 91 munmap              94 fchmod             120 clone              122 uname
125 mprotect           144 msync              146 writev             155 sched_getparam
156 sched_setscheduler 157 sched_getscheduler  158 sched_yield        159 sched_get_priority_max
160 sched_get_priority_min                    162 nanosleep          163 mremap
172 prctl              174 rt_sigaction       175 rt_sigprocmask     183 getcwd
186 sigaltstack        191 ugetrlimit         193 truncate64         194 ftruncate64
199 getuid32           200 getgid32           201 geteuid32          202 getegid32
205 getgroups32        219 mincore            220 madvise            221 fcntl64
239 sendfile64         242 sched_getaffinity  248 exit_group         256 set_tid_address
268 tgkill             282 bind               284 listen             286 getsockname
287 getpeername        292 recvfrom           294 setsockopt         295 getsockopt
322 openat             328 unlinkat           330 linkat             335 pselect6
336 ppoll              348 utimensat          363 rt_tgsigqueueinfo   382 renameat2
384 getrandom          452 fchmodat2
```

This set is a **lower bound** and it is visibly incomplete: `mmap2` (192), `brk` (45),
`futex` (240) and `gettimeofday`'s siblings are absent only because `mmap()`/`brk()` inside
libc reach their trap through one of the 280 unfollowed indirect calls. Do not scope from
this list alone.

For calibration, the current host's `syscalls.cpp` references **147 distinct ARM syscall
numbers**:

```
python s18_hostsys.py
→ NR_* identifiers referenced in syscalls.cpp: 147 (known 147, unknown 0)
→ distinct ARM syscall numbers: 147
```

and the device run that got as far as the GL surface reports `unimplemented-host-calls: 0`,
i.e. no syscall was the blocker. Treat **147 (host-proven) ∪ 61 (static lower bound) ∪ the
memory-shaped set below** as the requirement, i.e. the host's current 147 is already the
right order of magnitude and the static 61 confirms a purpose-built host cannot shrink it
much further below ~150.

### 5.4 Memory-shaped syscalls — specific attention

Guest memory is a **single 4 GiB reservation bounded by `mmap_limit = 0xFE000000`**
(3.99 GiB usable), and a guest `mmap` failure has already caused a fatal bug
(`docs/GUEST-MEMORY-INVESTIGATION.md`). The memory-shaped syscalls are therefore called
out separately, independent of the reachability result:

| syscall | number | why it is mandatory here |
| --- | --- | --- |
| `mmap2` | 192 | the only mapping primitive on ARM EABI (page-offset form); all guest `mmap`/`malloc`/`dlopen` maps funnel here. Present as a stub in libc.so, present in the host's 147. **It is absent from the static 61 only because `mmap()` reaches it indirectly — that is a limitation of the analysis, not evidence it is unused.** |
| `munmap` | 91 | present in the static 61 (2 functions) and host 147 |
| `mprotect` | 125 | present in both; W^X transitions for the JIT's translated code and for guest `dlopen` |
| `mremap` | 163 | present in both; used by guest allocators to grow mappings — the classic `mmap_limit` failure mode |
| `msync` | 144 | present in both; the engine calls `msync` directly (`libStormGLOFT.so` imports it) |
| `madvise` | 220 | present in the static 61 and host 147; `MADV_DONTNEED` reclaim is how guest allocators release pages |
| `mincore` | 219 | present in the static 61; guest code probes residency |
| `brk` | 45 | libc's legacy heap; present in host 147, absent from the static 61 (reached indirectly) |
| `memfd_create` | 385 | ashmem replacement path; host 147 |
| `mlock`/`mlock2`/`munlock`/`mlockall`/`munlockall` | 150/390/151/152/153 | libc stubs exist; JIT + translated code pinning |
| `membarrier` | 389 | host 147; needed for cross-thread JIT invalidation |

Two hard requirements that fall out of the design docs:

* **`mmap2` must fail with a correct `errno` and must never return an address at or above
  `0xFE000000`.** A guest-visible `MAP_FAILED` from an over-large request is a supported
  outcome; a silent clamp is not.
* **`mremap` must be able to grow a mapping in place up to the bound** — the investigation
  doc's fatal bug is in this shape.

Also in the "other trap" class, not in `syscall_nrs_arm.h`:

| trap | immediate | note |
| --- | --- | --- |
| `__ARM_NR_cacheflush` | `0x0F0002` (98 3042) | present in the host's 147 set; `libStormGLOFT.so` imports the `cacheflush` libc wrapper |
| `__ARM_NR_set_tls` | `0x0F0005` (98 3045) | present in host 147; required for guest TLS |
| `__ARM_NR_get_tls` | `0x0F0006` (98 3046) | present in host 147 |
| `kuser` page helpers | `0xFFFF0000` (`get_tls`, `cmpxchg`, `memory_barrier`) | the host synthesises these in `process.cpp` (`kKuserPage`, version 3), not via `svc` |

---

## 6. "Must implement" table, by phase

Union of §1–§5, ordered by what a minimal host needs to get from cold start to gameplay.
"Droppable" is relative to the current host's tables and is the scoping answer.

### Phase A — boot the engine

Everything here is required before a single GL call can happen.

| requirement | exact surface | source |
| --- | --- | --- |
| ELF loading + `DT_NEEDED` resolution | 20 objects (3 engine + 7 guest + 9 sysroot + `linker`); `SONAME`s: `libDungeonHunter2.so`, `libStormGLOFT.so`, `libnativeinterface.so`, `libEGL.so`, `libGLESv1_CM.so`, `libGLESv2.so`, `libandroid.so`, `libjnigraphics.so`, `libzbcompat.so`, `libzbjni.so`, `libc.so`, `libc++.so`, `libm.so`, `libdl.so`, `libdl_android.so`, `liblog.so`, `libstdc++.so`, `libz.so`, `ld-android.so` | §1 |
| R_ARM_RELATIVE / ABS32 / GLOB_DAT / JUMP_SLOT relocation engine | engine alone: 54 463 relocations (`.rel.dyn` 54 112 = 49 132 type 23 + 4 969 type 2 + 11 type 21; `.rel.plt` 351 type 22); all 20 objects: **64 951** | `python s05_hostcalls.py`, `python s21_verify.py` |
| ARM EABI unwind (`__aeabi_unwind_cpp_pr0/pr1`, `__gnu_Unwind_Find_exidx`) | `.ARM.exidx` present in every guest object (engine: 0x3C880 bytes) | §1.4 |
| loader shim symbols | the 26 `__loader_*` names of §1.3 | §1.3 |
| `libdl` shim | `dlopen`, `dlsym`, `dlclose`, `dlerror`, `dladdr`, `dl_unwind_find_exidx`, `android_dlopen_ext`, `android_get_application_target_sdk_version` | §1.4 |
| libc core | `malloc`/`calloc`/`realloc`/`free`, `memcpy`/`memmove`/`memset`/`memcmp`, `strlen`/`strcmp`/`strcpy`/…, `__errno`, `__sF`/stdio, `abort`, `exit`, `__stack_chk_fail`/`__stack_chk_guard`, `__cxa_atexit`/`__cxa_finalize`/`__cxa_guard_acquire`/`__cxa_guard_release`/`__cxa_pure_virtual`, `__dso_handle` | §1.4 |
| ARM EABI compiler helpers | the 39 `__aeabi_*` / unwind symbols of §1.4 | §1.4 |
| libstdc++ (bionic, GNU) | 9 symbols: `_Znwj`, `_Znaj`, `_ZdlPv`, `_ZdaPv`, `_ZnajRKSt9nothrow_t`, `_ZSt7nothrow`, `__cxa_guard_acquire`, `__cxa_guard_release`, `__cxa_pure_virtual` | §1.4 |
| pthreads | 18 symbols: `pthread_create`, `pthread_join`, `pthread_exit`, `pthread_self`, `pthread_kill`, `pthread_mutex_*` (6), `pthread_mutexattr_*` (2), `pthread_attr_*` (3), `pthread_key_create`, `pthread_getspecific`, `pthread_setspecific`, `pthread_getschedparam`, `pthread_setschedparam`; plus `pthread_cond_wait`/`_broadcast`/`_destroy`/`_signal`/`_timedwait`/`_detach`/`pthread_rwlock_*`/`pthread_once` via liblog/libc++ | §1.4, §1.3 |
| TLS | `__tls_get_addr`, `__loader_add_thread_local_dtor`, `__loader_remove_thread_local_dtor`, `__ARM_NR_set_tls`/`get_tls` | §1.3, §5.4 |
| filesystem syscalls | `openat`, `close`, `read`, `lseek`, `fstat64`, `fstatat64`, `getdents64`, `ioctl`, `fcntl64`, `readlinkat`, `faccessat`, `getcwd`, `chdir`, `statfs64`, `fstatfs64`, `ftruncate64`, `truncate64`, `unlinkat`, `mkdirat`, `renameat2`, `utimensat`, `fchmod`/`fchmodat2` | §5.3, §5.4 |
| memory syscalls | `mmap2`, `munmap`, `mprotect`, `mremap`, `msync`, `madvise`, `mincore`, `brk` — **bounded by `mmap_limit = 0xFE000000`** | §5.4 |
| thread/signal syscalls | `clone`, `set_tid_address`, `futex`/`futex_time64`, `exit`, `exit_group`, `tgkill`, `kill`, `rt_sigaction`, `rt_sigprocmask`, `sigaltstack`, `sched_yield`, `sched_getaffinity`, `sched_setaffinity`, `sched_setscheduler`/`getscheduler`, `sched_get_priority_max`/`min`, `prctl`, `prlimit64`, `ugetrlimit`, `getrandom`, `membarrier`, `__ARM_NR_cacheflush` | §5.3, §5.4 |
| time syscalls | `clock_gettime`, `gettimeofday`, `nanosleep`, `clock_nanosleep`, `times`, `getitimer`/`setitimer` | §5.3, §5.4 |
| misc syscalls | `uname`, `sysinfo`, `getpid`, `gettid`, `getuid32`, `getgid32`, `geteuid32`, `getegid32`, `getgroups32`, `personality`, `readv`/`writev`, `pread64`/`pwrite64` | §5.3 |
| JNI/ART | `JNINativeMethod` registration for **36** natives; `JNI_OnLoad` ×2 (JNI 1.4 + 1.6); JNIEnv table with at least the 13 slots of §4.3 (implement all 233 for safety); JavaVM table (5 functions); `FindClass` for `android/content/Context`; `GetMethodID` ×6 on `android/media/AudioTrack`; `GetStaticMethodID` ×13 | §4 |
| JNI shim host calls | the 59 `jni_hostcalls.inc` entries at `0x5AFC00` (`Register`, `FindClass`, `GetMethodID`, `GetStringUTFLength`, `GetStringLength`, `GetStringUTFRegion`, `NewGlobalRef`, …) | §4.3 |
| host-call dispatch | `svc #(0x5A0000 \| index)` for `index ∈ [0,578]` **and** `svc #(0x5AFC00 \| index)`; ARM mode; `index = swi & 0xFFFF` | §3.1 |

Phase A droppable: **487 of 579 host-call indices** (nothing in Phase A needs a host call;
even the GL stubs sit unused until Phase B).

### Phase B — render

| requirement | exact surface | source |
| --- | --- | --- |
| GLES2 entry points | the **92** names of §2.3, host-call indices **0…141, 216, 228…343 minus unused** — concretely the 92 indices listed in §3.4 | §2, §3.4 |
| EGL entry points | **0** | §2.1 |
| EGL/GLES1/android/jnigraphics stubs on disk | the five stub objects must exist and be loadable (`DT_NEEDED`), but export nothing that is looked up | §1.1, §2.2 |
| framebuffer / display | **no guest-visible API at all**: no `EGL*`, no `ANativeWindow_*`, no `eglSwapBuffers`, no `glReadPixels`-driven present path. Presentation is entirely host-side. | §2.1 (0 EGL strings anywhere in the engine), §1.4 |
| libm | the 37 names of §1.4 | §1.4 |
| liblog | `__android_log_print`, `__android_log_write` | §1.4 |
| texture/data path | `openat`/`read`/`lseek`/`mmap2`/`munmap` (already Phase A) | §1.4 |
| audio (Java side) | `android/media/AudioTrack` `<init>(IIIIII)V`, `play`, `pause`, `stop`, `release`, `write([BII)I` | §4.2 |

Phase B droppable: **46 EGL entry points, 87 `libGLESv1_CM` entry points, 167 unused
`libGLESv2` entry points, 184 `libandroid` entry points, 3 `libjnigraphics` entry points.**

### Phase C — gameplay

| requirement | exact surface | source |
| --- | --- | --- |
| sockets | libc: `accept`, `bind`, `connect`, `getpeername`, `getsockname`, `getsockopt`, `gethostbyaddr`, `gethostbyname`, `gethostname`, `inet_addr`, `inet_ntoa`, `listen`, `recv`, `recvfrom`, `select`, `send`, `sendto`, `setsockopt`, `socket`. syscalls: `socket`, `bind`, `connect`, `listen`, `accept4`, `getsockname`, `getpeername`, `sendto`, `recvfrom`, `setsockopt`, `getsockopt`, `sendmsg`, `recvmsg`, `socketpair`, `shutdown`, `poll`, `ppoll`, `pselect6`, `eventfd2`, `epoll_create1`, `epoll_ctl`, `epoll_wait`/`epoll_pwait`, `inotify_add_watch`/`inotify_rm_watch`/`inotify_init1` | §1.4, §5.3 |
| input | Java→native only: `nativeOnTouch`, `nativeKeyDown`, `nativeKeyUp`, `nativeonTrackballEvent`, `nativeAccelerometer`, `nativeSetOrientation`. **No host-call index, no syscall.** | §4.1 |
| Java callbacks | the 13 `GetStaticMethodID` sites of §4.2 (`IsWifiEnable`, `getHostName`, `da`→`Activity`, `a`–`f`, `db`, `dc`, `d`) | §4.2 |
| file/asset I/O | Phase A set; plus `utimensat`, `linkat`, `unlinkat`, `sendfile64` | §5.3 |
| threads | Phase A set; the engine creates threads (`pthread_create`, `pthread_join`, `pthread_kill`, sched params) | §1.4 |
| licence check | `libnativeinterface.so`: 19 libc symbols, 4 natives — self-contained, needs no host call | §1.4, §4.1 |

Phase C droppable: nothing further from the host-call table — the entire remaining
requirement is syscalls, libc and Java.

### Summary of the scoping answer

| surface | current host ships | this game needs | droppable | reduction |
| --- | --- | --- | --- | --- |
| SVC host-call indices (`0x5A0000`) | **579** | **92** | 487 | **84.1 %** |
| GL/EGL entry points | **392** (259+87+46) | **92** | 300 | **76.5 %** |
| EGL entry points | 46 | **0** | 46 | 100 % |
| `libGLESv1_CM` entry points | 87 | **0** | 87 | 100 % |
| `libandroid` entry points | 184 | **0** | 184 | 100 % |
| `libjnigraphics` entry points | 3 | **0** | 3 | 100 % |
| JNI host-call indices (`0x5AFC00`) | 59 | 59 (all reachable) | 0 | 0 % |
| JNIEnv slots | 233 | **13 proven** (implement all 233) | ~220 | — |
| JavaVM slots | 8 | **5** (implement all 8) | 3 reserved | — |
| registered natives | 36 | **36** (32 + 4) | 0 | 0 % |
| syscall numbers | 147 implemented | 147 ∪ 61 ∪ §5.4 memory set | few | ~0 % |
| undefined dynamic symbols | — | **865** total across 20 objects; **506** across the 3 engine binaries | — | — |

---

## 7. Biggest unknown

**The JNIEnv/JavaVM slot set beyond the 13 proven slots.**

Why this is the biggest: it is the only surface where the two independent methods disagree
with the runtime's *reach*, and where a missed slot is a hard failure (an unimplemented
slot is an indirect call through a null or wrong function pointer, not a graceful error).

* The argument-seeded dataflow (`s12_jni2.py`) seeds only functions whose **mangled name**
  proves a `_JNIEnv*`/`_JavaVM*` argument. Only **36** functions in the engine qualify
  (32 `Java_*` natives + `JNI_OnLoad` + 3 more:
  `_Z12VoxSetJavaVMP7_JavaVM`, `_ZN13ALicenseCheck4InitEP7_JNIEnvP7_jclass`,
  `_ZN7ADevice4InitEP7_JNIEnvP7_jclass`). The engine has **31 021** `STT_FUNC` symbols.
* The engine caches `JNIEnv` in globals and object members (`ALicenseCheck` clearly does —
  it stores an env obtained via `GetEnv`), so most of its JNI traffic dispatches through a
  table pointer loaded from a global or a `this->m_env` field. That defeats argument
  seeding and cannot be distinguished from an ordinary C++ vtable call by instruction
  shape alone: the whole-text histogram (`s11_slots.py`) found 10 422 `ldr rX,[rN]` +
  `ldr pc,[rX,#imm]` tail dispatches, dominated by C++ virtual calls, and is therefore
  useless for separating JNI.
* **`JavaVM` slots are the sharpest instance: the scan found 0.** `JNI_OnLoad` receives a
  `JavaVM*` in `r0` and must call `GetEnv` (JNIInvokeInterface offset 6 × 4 = 24) to obtain
  an env; the runtime report confirms `jni-runtime-start: ok`, so this path definitely
  executes. The static method did not observe it — the `vm` register is spilled and
  reloaded before the dispatch.

**Bound and mitigation.** The bound is hard and small: the guest interface table
(`guest/zbjni/gen/tables.inc`) is a full `JNINativeInterface` with **233 slots** (229
functions + 4 reserved) and a full `JNIInvokeInterface` with **8** (5 functions + 3
reserved). Every slot is already populated by a guest `zbjni_<Name>` C function, and every
one of those routes to the host through the flat 59-entry `0x5AFC00` protocol that the host
already implements completely. So the correct scoping decision is **not** to reduce the
JNI tables by static analysis: implement all **233 JNIEnv slots + 5 JavaVM functions**.
That costs 238 dispatch entries — less than the 487 host-call indices being deleted — and
removes the unknown entirely.

**Second unknown (lower severity): syscall reachability is a lower bound.** 280 indirect
(`blx rN`) calls inside libc.so were not followed, and calls that leave libc.so and return
are not modelled, so the static 61 omits syscalls that are certainly required — `mmap2`
(192), `brk` (45) and `futex` (240) among them. §5.4 lists the memory-shaped ones
explicitly for that reason. The current host's 147-number implementation is the practical
floor and already proved sufficient to reach the GL surface (`unimplemented-host-calls: 0`).

**Third, and benign:** the engine's `.rodata` contains no GL name outside the 92 imports
and no `egl*` string at all (`s19_glcount.py`), and nothing in the corpus imports from
`libEGL.so`/`libandroid.so`/`libjnigraphics.so`. Runtime `dlsym` extension loading of GL
entry points is therefore **excluded with evidence**, not assumed away. The only dynamic
lookup surface in the whole corpus is `libStormGLOFT.so`'s three imports
(`dlopen`, `dlsym`, `dlclose`) — worth tracing at runtime once, because a `dlsym` for a
symbol name built at runtime would not appear in any of the analysis above.

---

## 8. Reproduce

```
REM toolchain (note: capstone 5.0.7, not 5.0.9)
python -c "import capstone, elftools; print(capstone.__version__, elftools.__version__)"

cd C:\Users\NacWorkstation\Documents\DH2Work-scratch8

REM §1  undefined-symbol inventory
python s01_undef.py
python s02_char.py
python s20_inv.py

REM §2  GL/EGL surface and the reduction
python s03_gl.py
python s05_hostcalls.py
python s19_glcount.py

REM §3  host-call reachability and the stub-SVC proof
python s06_reach.py
python s07_verify.py

REM §4  JNI/ART surface
python s10_jni.py natives
python s10_jni.py slots
python s12_jni2.py
python s13_java.py
python s14_binding.py

REM §5  syscalls
python s17_mixed.py
python s18_hostsys.py

REM host tables being compared against
Get-Content "C:\Users\NacWorkstation\Documents\DH2Work-stage\compatibility\work\research\ZettaBridge\core\src\gen\hostcalls.inc"   REM 580 lines, 579 rows
Get-Content "C:\Users\NacWorkstation\Documents\DH2Work-stage\compatibility\work\research\ZettaBridge\core\src\gen\jni_hostcalls.inc" REM 59 rows
```

Machine-readable intermediates are written to
`C:\Users\NacWorkstation\Documents\DH2Work-scratch8\out\`:
`undef.json`, `undef.tsv`, `hostcalls.json`, `reachable.json`, `stub_svc.json`,
`gl.json`, `jnienv_slots_*.json`, `jni_slots*.json`, `java_surface.json`, `java_pairs.json`,
`java_binding.json`, `natives_anchored.json`, `syscalls*.json`, `inventory.json`,
`host_syscalls.json`.
