#include "dh2/syscalls.hpp"

#include <errno.h>
#include <fcntl.h>
#include <linux/futex.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "dh2/cpu.hpp"
#include "dh2/loader.hpp"
#include "dh2/random.hpp"
#include "dh2/vfs.hpp"

namespace dh2 {

namespace {

// ARM EABI syscall numbers.
enum : std::int32_t {
    kExit = 1,
    kRead = 3,
    kWrite = 4,
    kOpen = 5,
    kClose = 6,
    kLseek = 19,
    kGetpid = 20,
    kAccess = 33,
    kBrk = 45,
    kIoctl = 54,
    kUmask = 60,
    kDup = 41,
    kGettimeofday = 78,
    kReadlink = 85,
    kGetppid = 64,
    kUgetrlimit = 191,
    kPersonality = 136,
    kSchedGetparam = 155,
    kSchedSetscheduler = 156,
    kSchedGetscheduler = 157,
    kSchedYield = 158,
    kSchedGetPriorityMax = 159,
    kSchedGetPriorityMin = 160,
    kSigaltstack = 186,
    kSocket = 281,
    kRtSigpending = 176,
    kRtSigtimedwait = 177,
    kRtSigqueueinfo = 178,
    kRtSigsuspend = 179,
    kRtTgsigqueueinfo = 363,
    kMunmap = 91,
    kMsync = 144,
    kReadv = 145,
    kMremap = 163,
    kMadvise = 220,
    kMprotect = 125,
    kUname = 122,
    kWritev = 146,
    kPoll = 168,
    kPrctl = 172,
    kRtSigaction = 174,
    kRtSigprocmask = 175,
    kGetcwd = 183,
    kMmap2 = 192,
    kStat64 = 195,
    kFstat64 = 197,
    kGetuid32 = 199,
    kGetgid32 = 200,
    kGeteuid32 = 201,
    kGetegid32 = 202,
    kFcntl64 = 221,
    kGettid = 224,
    kFutex = 240,
    kSchedGetaffinity = 242,
    kExitGroup = 248,
    kSetTidAddress = 256,
    kClockGettime = 263,
    kClockGettime64 = 403,
    kOpenat = 322,
    kConnect = 283,
    kLlseek = 140,
    kFstatat64 = 327,
    kReadlinkat = 332,
    kFaccessat = 334,
    kSetRobustList = 338,
    kPrlimit64 = 369,
    kPread64 = 180,
    kGetrandom = 384,

    // ARM private range: the number lives in the SVC immediate, not in r7.
    kArmNrCacheflush = 0x0f0002,
    kArmNrSetTls = 0x0f0005,
};

constexpr std::uint32_t kStackTop = 0xFF000000u;

// ARM's struct stat64 is 96 bytes and the fields sit at these offsets. Only the ones a guest
// reads in practice are filled; the rest stays zero rather than guessed.
constexpr std::uint32_t kStat64Size = 96;

void store_u32(std::uint8_t* out, std::size_t offset, std::uint32_t value) {
    std::memcpy(out + offset, &value, 4);
}

void store_u64(std::uint8_t* out, std::size_t offset, std::uint64_t value) {
    std::memcpy(out + offset, &value, 8);
}

void fill_stat64(std::uint8_t* out, std::uint32_t mode, std::uint64_t size) {
    std::memset(out, 0, kStat64Size);
    store_u32(out, 16, mode);                                    // st_mode
    store_u32(out, 20, 1);                                       // st_nlink
    store_u64(out, 44, size);                                    // st_size
    store_u32(out, 52, 4096);                                    // st_blksize
    store_u64(out, 56, (size + 511) / 512);                      // st_blocks
    store_u64(out, 88, 1);                                       // st_ino
}

// Android's pid_max is 32768, and bionic's 32-bit pthread_mutex_t keeps the owner in a 16-bit
// field, so a guest must never be told a pid above 65535 or libc refuses to lock a mutex at
// startup. A Linux host can have a far larger pid counter -- this WSL one has -- so the guest is
// given a stable small pid derived from the host's. One guest thread means pid and tid are the
// same value, which is also what bionic's main thread expects.
std::uint32_t guest_pid() {
    static const std::uint32_t pid = [] {
        const std::uint32_t host = static_cast<std::uint32_t>(::getpid());
        return host <= 65535u ? host : 1000u + (host % 30000u);
    }();
    return pid;
}

}  // namespace

const char* syscall_name(std::int32_t number) {
    switch (number) {
        case kExit: return "exit";
        case kRead: return "read";
        case kWrite: return "write";
        case kOpen: return "open";
        case kClose: return "close";
        case kLseek: return "lseek";
        case kGetpid: return "getpid";
        case kAccess: return "access";
        case kBrk: return "brk";
        case kIoctl: return "ioctl";
        case kUmask: return "umask";
        case kDup: return "dup";
        case kGettimeofday: return "gettimeofday";
        case kReadlink: return "readlink";
        case kMunmap: return "munmap";
        case kMsync: return "msync";
        case kReadv: return "readv";
        case kMremap: return "mremap";
        case kMadvise: return "madvise";
        case kMprotect: return "mprotect";
        case kUname: return "uname";
        case kWritev: return "writev";
        case kPoll: return "poll";
        case kPrctl: return "prctl";
        case kRtSigaction: return "rt_sigaction";
        case kRtSigprocmask: return "rt_sigprocmask";
        case kGetcwd: return "getcwd";
        case kMmap2: return "mmap2";
        case kStat64: return "stat64";
        case kFstat64: return "fstat64";
        case kGetuid32: return "getuid32";
        case kGetgid32: return "getgid32";
        case kGeteuid32: return "geteuid32";
        case kGetegid32: return "getegid32";
        case kFcntl64: return "fcntl64";
        case kGettid: return "gettid";
        case kFutex: return "futex";
        case kSchedGetaffinity: return "sched_getaffinity";
        case kExitGroup: return "exit_group";
        case kSetTidAddress: return "set_tid_address";
        case kClockGettime: return "clock_gettime";
        case kClockGettime64: return "clock_gettime64";
        case kOpenat: return "openat";
        case kFstatat64: return "fstatat64";
        case kReadlinkat: return "readlinkat";
        case kFaccessat: return "faccessat";
        case kSetRobustList: return "set_robust_list";
        case kPrlimit64: return "prlimit64";
        case kConnect: return "connect";
        case kLlseek: return "_llseek";
        case kPread64: return "pread64";
        case kGetrandom: return "getrandom";
        case kGetppid: return "getppid";
        case kUgetrlimit: return "ugetrlimit";
        case kPersonality: return "personality";
        case kSchedGetparam: return "sched_getparam";
        case kSchedSetscheduler: return "sched_setscheduler";
        case kSchedGetscheduler: return "sched_getscheduler";
        case kSchedYield: return "sched_yield";
        case kSchedGetPriorityMax: return "sched_get_priority_max";
        case kSchedGetPriorityMin: return "sched_get_priority_min";
        case kSigaltstack: return "sigaltstack";
        case kSocket: return "socket";
        case kRtSigpending: return "rt_sigpending";
        case kRtSigtimedwait: return "rt_sigtimedwait";
        case kRtSigqueueinfo: return "rt_sigqueueinfo";
        case kRtSigsuspend: return "rt_sigsuspend";
        case kRtTgsigqueueinfo: return "rt_tgsigqueueinfo";
        case kArmNrCacheflush: return "__ARM_NR_cacheflush";
        case kArmNrSetTls: return "__ARM_NR_set_tls";
        default: return "unimplemented";
    }
}

std::string SyscallLayer::guest_string(std::uint32_t address) const {
    if (address == 0) return {};
    const std::uint8_t* p = mem_.host_ptr(address, 1, kPageRead);
    if (p == nullptr) return {};
    std::uint32_t length = 0;
    while (length < 512 && p[length] != 0) ++length;
    return std::string(reinterpret_cast<const char*>(p), length);
}

SyscallLayer::SyscallLayer(GuestMemory& memory, Cpu& cpu, const LoadedImage& image)
    : mem_(memory), cpu_(cpu) {
    brk_base_ = page_round_up(image.load_end);
    brk_end_ = brk_base_ + (8u << 20);
    brk_current_ = brk_base_;
}

std::uint32_t& SyscallLayer::reg(std::size_t i) { return cpu_.jit().Regs()[i]; }

std::uint32_t SyscallLayer::syscall_arg(std::size_t i) const {
    // The ARM syscall ABI passes up to seven arguments in r0-r6, and bionic's syscall() wrapper
    // loads exactly that many before svc. The fifth argument is r4 -- not the first word of the
    // stack. Reading the stack there returned a leftover stack pointer as the whence, which made
    // every seek land at EOF and every read return zero.
    if (i < 7) return cpu_.jit().Regs()[i];
    // Beyond seven there is no register left, so the kernel reads the caller's stack.
    const std::uint32_t sp = cpu_.jit().Regs()[13];
    const std::uint32_t at = sp + static_cast<std::uint32_t>(i - 7) * 4;
    std::uint32_t value = 0;
    if (mem_.accessible(at, 4, kPageRead)) std::memcpy(&value, mem_.base() + at, 4);
    return value;
}

SyscallResult SyscallLayer::handle(std::uint32_t svc) {
    const std::int32_t number = svc != 0 ? static_cast<std::int32_t>(svc) : static_cast<std::int32_t>(reg(7));
    ++handled_;

    auto entry = std::find_if(census_.begin(), census_.end(),
                              [number](const CensusEntry& e) { return e.number == number; });
    if (entry == census_.end()) {
        census_.push_back(CensusEntry{number, syscall_name(number), 0});
        entry = census_.end() - 1;
    }
    ++entry->calls;

    // The private __ARM_NR_set_tls is the one syscall whose effect is a CPU register, not a
    // host resource, so it is served here rather than in the dispatch table.
    if (number == kArmNrSetTls) {
        cpu_.cp15().set_tpidruro(reg(0));
        reg(0) = 0;
        return {};
    }

    if (number == kExit || number == kExitGroup) {
        SyscallResult result;
        result.kind = SyscallResult::Kind::Exit;
        result.exit_code = static_cast<std::int32_t>(reg(0));
        return result;
    }

    const std::int32_t result = dispatch(number);
    if (result == -ENOSYS) {
        const std::string description = std::string(syscall_name(number)) + " (#" + std::to_string(number) + ")";
        if (std::find(unimplemented_.begin(), unimplemented_.end(), description) == unimplemented_.end()) {
            unimplemented_.push_back(description);
        }
    }
    reg(0) = static_cast<std::uint32_t>(result);
    return {};
}

std::int32_t SyscallLayer::dispatch(std::int32_t number) {
    switch (number) {
        case kWrite: {
            const int fd = static_cast<int>(reg(0));
            const std::uint32_t buffer = reg(1);
            const std::uint32_t count = reg(2);
            const std::uint8_t* host = mem_.host_ptr(buffer, count, kPageRead);
            if (host == nullptr) return -EFAULT;
            const ssize_t written = ::write(fd, host, count);
            return written < 0 ? -errno : static_cast<std::int32_t>(written);
        }

        case kWritev: {
            const int fd = static_cast<int>(reg(0));
            const std::uint32_t iov = reg(1);
            const std::uint32_t count = reg(2);
            if (count > 1024) return -EINVAL;
            std::vector<iovec> vec(count);
            std::vector<std::vector<std::uint8_t>> owned(count);
            for (std::uint32_t i = 0; i < count; ++i) {
                std::uint32_t base = 0, len = 0;
                if (!mem_.accessible(iov + i * 8, 8, kPageRead)) return -EFAULT;
                std::memcpy(&base, mem_.base() + iov + i * 8, 4);
                std::memcpy(&len, mem_.base() + iov + i * 8 + 4, 4);
                const std::uint8_t* src = mem_.host_ptr(base, len, kPageRead);
                if (src == nullptr) return -EFAULT;
                owned[i].assign(src, src + len);
                vec[i].iov_base = owned[i].data();
                vec[i].iov_len = count != 0 ? len : 0;
            }
            const ssize_t written = ::writev(fd, vec.data(), static_cast<int>(count));
            return written < 0 ? -errno : static_cast<std::int32_t>(written);
        }

        case kRead: {
            const std::int32_t fd = static_cast<std::int32_t>(reg(0));
            const std::uint32_t buffer = reg(1);
            const std::uint32_t count = reg(2);
            std::uint8_t* host = mem_.host_ptr(buffer, count, kPageWrite);
            if (host == nullptr) return -EFAULT;
            const auto it = files_.find(fd);
            if (it != files_.end()) {
                const ssize_t got = ::pread(it->second.host_fd, host, count, static_cast<off_t>(it->second.offset));
                if (io_trace_ < io_trace_limit_) {
                    ++io_trace_;
                    std::fprintf(stderr, "dh2: read fd=%d off=0x%llx want=%u got=%zd\n", fd,
                                 static_cast<unsigned long long>(it->second.offset), count, got);
                }
                if (got < 0) return -errno;
                it->second.offset += static_cast<std::uint64_t>(got);
                return static_cast<std::int32_t>(got);
            }
            const ssize_t got = ::read(fd, host, count);
            return got < 0 ? -errno : static_cast<std::int32_t>(got);
        }

        case kPread64: {
            const std::int32_t fd = static_cast<std::int32_t>(reg(0));
            const std::uint32_t buffer = reg(1);
            const std::uint32_t count = reg(2);
            const std::uint32_t offset_low = reg(3);
            const std::uint32_t offset_high = reg(4);
            std::uint8_t* host = mem_.host_ptr(buffer, count, kPageWrite);
            if (host == nullptr) return -EFAULT;
            const auto it = files_.find(fd);
            if (it == files_.end()) return -EBADF;
            const std::uint64_t offset =
                static_cast<std::uint64_t>(offset_low) | (static_cast<std::uint64_t>(offset_high) << 32);
            const ssize_t got = ::pread(it->second.host_fd, host, count, static_cast<off_t>(offset));
            return got < 0 ? -errno : static_cast<std::int32_t>(got);
        }

        case kBrk: {
            const std::uint32_t requested = reg(0);
            if (requested == 0) return static_cast<std::int32_t>(brk_current_);
            const std::uint32_t aligned = page_round_up(requested);
            if (aligned > brk_current_) {
                if (aligned > brk_end_) return static_cast<std::int32_t>(brk_current_);
                if (!mem_.map_anon(brk_current_, aligned - brk_current_, PROT_READ | PROT_WRITE)) {
                    return static_cast<std::int32_t>(brk_current_);
                }
                brk_current_ = aligned;
            } else if (aligned < brk_current_) {
                mem_.unmap(aligned, brk_current_ - aligned);
                brk_current_ = aligned;
            }
            return static_cast<std::int32_t>(brk_current_);
        }

        case kMmap2: {
            const std::uint32_t requested = reg(0);
            const std::uint32_t length = reg(1);
            const int prot = static_cast<int>(reg(2));
            const int flags = static_cast<int>(reg(3));
            // reg(4) is the fd, reg(5) is the offset in 4096-byte units.
            const std::uint64_t offset = static_cast<std::uint64_t>(reg(5)) * 4096;
            const int host_flags = (flags & MAP_SHARED) ? MAP_SHARED : MAP_PRIVATE;
            if (length == 0) return -EINVAL;
            if (flags & MAP_ANONYMOUS) {
                const std::uint32_t at = requested != 0 ? page_round_down(requested)
                                                        : mem_.find_free(length, kStackTop);
                if (at == 0) return -ENOMEM;
                if (!mem_.map_anon(at, length, prot)) return -ENOMEM;
                return static_cast<std::int32_t>(at);
            }
            const std::int32_t fd = static_cast<std::int32_t>(reg(4));
            const std::uint32_t at = requested != 0 ? page_round_down(requested) : mem_.find_free(length, kStackTop);
            if (at == 0) return -ENOMEM;
            // A file the guest opened goes through our read-only view, so the descriptor it holds is
            // ours, not the host's. Passing it straight to mmap() mapped whatever the host had under
            // that number -- which is not the guest's file, and is why an asset could parse into
            // nonsense.
            int host_fd = fd;
            const auto opened = files_.find(fd);
            if (opened != files_.end()) host_fd = opened->second.host_fd;
            if (!mem_.map_file(at, length, prot, host_flags, host_fd, offset)) return -ENOMEM;
            if (opened != files_.end()) {
                ++file_maps_;
                std::printf("  mmap2        : 0x%08x+0x%x from a guest fd %d (host %d, offset 0x%llx)\n", at,
                            length, fd, host_fd, static_cast<unsigned long long>(offset));
            }
            return static_cast<std::int32_t>(at);
        }

        case kMunmap: {
            if (!mem_.unmap(page_round_down(reg(0)), reg(1))) return -EINVAL;
            return 0;
        }

        case kMremap: {
            // bionic's __cxa_atexit pool grows its block with this; without it every atexit
            // registration prints a failure and the constructor path degrades.
            const std::uint32_t old_address = page_round_down(reg(0));
            const std::uint32_t old_size = page_round_up(reg(1));
            const std::uint32_t new_size = page_round_up(reg(2));
            const int flags = static_cast<int>(reg(3));
            const std::uint32_t requested = page_round_down(reg(4));
            if (new_size == 0) return -EINVAL;
            if (new_size <= old_size) return static_cast<std::int32_t>(old_address);

            // Grow in place when the pages that follow are free.
            if (!mem_.accessible(old_address, old_size, kPageRead) ||
                !mem_.range_free(old_address + old_size, new_size - old_size)) {
                constexpr int kMremapMayMove = 1;
                if ((flags & kMremapMayMove) == 0 && requested == 0) return -ENOMEM;
                const std::uint32_t target =
                    requested != 0 ? requested : mem_.find_free(new_size, kStackTop);
                if (target == 0 || !mem_.map_anon(target, new_size, PROT_READ | PROT_WRITE)) return -ENOMEM;
                const std::uint8_t* source = mem_.host_ptr(old_address, old_size, kPageRead);
                if (source == nullptr) return -EFAULT;
                std::memmove(mem_.base() + target, source, old_size);
                mem_.unmap(old_address, old_size);
                return static_cast<std::int32_t>(target);
            }
            if (!mem_.map_anon(old_address + old_size, new_size - old_size, PROT_READ | PROT_WRITE)) {
                return -ENOMEM;
            }
            return static_cast<std::int32_t>(old_address);
        }

        case kMadvise:
        case kMsync:
            return 0;

        case kReadv: {
            const int fd = static_cast<int>(reg(0));
            const std::uint32_t iov = reg(1);
            const std::uint32_t count = reg(2);
            if (count > 1024) return -EINVAL;
            std::vector<iovec> vec(count);
            for (std::uint32_t i = 0; i < count; ++i) {
                std::uint32_t base = 0, len = 0;
                if (!mem_.accessible(iov + i * 8, 8, kPageRead)) return -EFAULT;
                std::memcpy(&base, mem_.base() + iov + i * 8, 4);
                std::memcpy(&len, mem_.base() + iov + i * 8 + 4, 4);
                std::uint8_t* destination = mem_.host_ptr(base, len, kPageWrite);
                if (destination == nullptr) return -EFAULT;
                vec[i].iov_base = destination;
                vec[i].iov_len = len;
            }
            const ssize_t got = ::readv(fd, vec.data(), static_cast<int>(count));
            return got < 0 ? -errno : static_cast<std::int32_t>(got);
        }

        case kMprotect: {
            if (!mem_.protect(page_round_down(reg(0)), reg(1), static_cast<int>(reg(2)))) return -EINVAL;
            return 0;
        }

        case kGetpid: return static_cast<std::int32_t>(guest_pid());
        case kGettid: return static_cast<std::int32_t>(guest_pid());
        case kGetuid32: return 0;
        case kGetgid32: return 0;
        case kGeteuid32: return 0;
        case kGetegid32: return 0;
        case kUmask: return 022;

        case kClockGettime: {
            const int clock = static_cast<int>(reg(0));
            const std::uint32_t out = reg(1);
            std::uint8_t* host = mem_.host_ptr(out, 8, kPageWrite);
            if (host == nullptr) return -EFAULT;
            timespec ts{};
            if (::clock_gettime(clock, &ts) != 0) return -errno;
            std::int32_t sec = static_cast<std::int32_t>(ts.tv_sec);
            std::int32_t nsec = static_cast<std::int32_t>(ts.tv_nsec);
            std::memcpy(host, &sec, 4);
            std::memcpy(host + 4, &nsec, 4);
            return 0;
        }

        case kClockGettime64: {
            const int clock = static_cast<int>(reg(0));
            const std::uint32_t out = reg(1);
            std::uint8_t* host = mem_.host_ptr(out, 16, kPageWrite);
            if (host == nullptr) return -EFAULT;
            timespec ts{};
            if (::clock_gettime(clock, &ts) != 0) return -errno;
            std::int64_t sec = ts.tv_sec;
            std::int64_t nsec = ts.tv_nsec;
            std::memcpy(host, &sec, 8);
            std::memcpy(host + 8, &nsec, 8);
            return 0;
        }

        case kGettimeofday: {
            const std::uint32_t out = reg(0);
            std::uint8_t* host = mem_.host_ptr(out, 8, kPageWrite);
            if (host == nullptr) return -EFAULT;
            timeval tv{};
            if (::gettimeofday(&tv, nullptr) != 0) return -errno;
            std::int32_t sec = static_cast<std::int32_t>(tv.tv_sec);
            std::int32_t usec = static_cast<std::int32_t>(tv.tv_usec);
            std::memcpy(host, &sec, 4);
            std::memcpy(host + 4, &usec, 4);
            return 0;
        }

        case kGetrandom: {
            const std::uint32_t buffer = reg(0);
            const std::uint32_t length = reg(1);
            std::uint8_t* host = mem_.host_ptr(buffer, length, kPageWrite);
            if (host == nullptr) return -EFAULT;
            // The flags are advisory here; what matters is that the guest gets entropy back.
            random_bytes(host, length);
            return static_cast<std::int32_t>(length);
        }

        case kFutex: {
            // One guest thread in this build. A wait that would block cannot be honoured, so it
            // reports the value-changed case (EAGAIN), which is a defined outcome the caller
            // already handles; a wake is a no-op.
            const int operation = static_cast<int>(reg(1)) & 0x7f;
            if (operation == FUTEX_WAKE) return 0;
            if (operation == FUTEX_WAIT) {
                const std::uint32_t address = reg(0);
                const std::uint32_t value = reg(2);
                std::uint32_t observed = 0;
                std::uint8_t* host = mem_.host_ptr(address, 4, kPageRead);
                if (host == nullptr) return -EFAULT;
                std::memcpy(&observed, host, 4);
                if (observed != value) return -EAGAIN;
                return 0;
            }
            return -ENOSYS;
        }

        case kRtSigprocmask: return 0;
        case kRtSigaction: return 0;
        case kGetppid: return 1;
        case kPersonality:
            // bionic reads the personality (clearing ADDR_NO_RANDOMIZE for a child thread) and
            // restores it. There is nothing to read here, so report success with a defined value.
            return 0;
        case kSchedGetscheduler: return 0;  // SCHED_OTHER
        case kSchedSetscheduler:
        case kSchedGetparam: return 0;
        case kSchedYield: return 0;
        case kSchedGetPriorityMax: return 0;
        case kSchedGetPriorityMin: return 0;
        case kSigaltstack: {
            // Record the alternate stack so a later query returns it; nothing needs it yet.
            altstack_ = reg(0);
            const std::uint32_t old = reg(1);
            if (old != 0) {
                std::uint8_t* host = mem_.host_ptr(old, 8, kPageWrite);
                if (host == nullptr) return -EFAULT;
                std::memset(host, 0, 8);
            }
            return 0;
        }
        case kSocket:
            // bionic opens an AF_UNIX datagram socket for its log fallback and for netd calls.
            // Forward it so the caller sees a real, nameable failure instead of ENOSYS.
            return static_cast<std::int32_t>(::syscall(SYS_socket, static_cast<int>(reg(0)),
                                                       static_cast<int>(reg(1)), static_cast<int>(reg(2))));

        case kRtSigpending: {
            const std::uint32_t out = reg(0);
            std::uint8_t* host = mem_.host_ptr(out, 8, kPageWrite);
            if (host == nullptr) return -EFAULT;
            std::memset(host, 0, 8);
            return 0;
        }
        case kRtTgsigqueueinfo:
        case kRtSigqueueinfo:
            // Signals are not delivered in this build (one guest thread, no signal layer), so a
            // queued signal is acknowledged rather than reported as unsupported: the alternative
            // makes abort()'s own path take an unspecified route.
            return 0;
        case kRtSigsuspend: return -EINTR;
        case kUgetrlimit: {
            // struct rlimit32 { u32 cur; u32 max; }
            const std::uint32_t which = reg(0);
            const std::uint32_t out = reg(1);
            std::uint8_t* host = mem_.host_ptr(out, 8, kPageWrite);
            if (host == nullptr) return -EFAULT;
            std::uint32_t value = 0;
            if (which == 3 /* RLIMIT_STACK */) value = 8u << 20;
            else if (which == 4 /* RLIMIT_CORE */) value = 0;
            else if (which == 0 /* RLIMIT_CPU */) value = 0xFFFFFFFFu;
            else if (which == 7 /* RLIMIT_NOFILE */) value = 1024;
            std::memcpy(host, &value, 4);
            std::memcpy(host + 4, &value, 4);
            return 0;
        }
        case kSetTidAddress: tid_address_ = reg(0); return static_cast<std::int32_t>(guest_pid());
        case kSetRobustList: return 0;
        case kSchedGetaffinity: {
            const std::uint32_t mask = reg(2);
            const std::uint32_t length = reg(1);
            if (length < 4) return -EINVAL;
            std::uint8_t* host = mem_.host_ptr(mask, 4, kPageWrite);
            if (host == nullptr) return -EFAULT;
            std::uint32_t bits = 1;
            std::memcpy(host, &bits, 4);
            return 4;
        }

        case kPrctl: return 0;
        case kArmNrCacheflush: return 0;

        case kUname: {
            const std::uint32_t out = reg(0);
            std::uint8_t* host = mem_.host_ptr(out, 65 * 6, kPageWrite);
            if (host == nullptr) return -EFAULT;
            utsname u{};
            if (::uname(&u) != 0) return -errno;
            std::memset(host, 0, 65 * 6);
            std::memcpy(host + 0 * 65, u.sysname, std::strlen(u.sysname));
            std::memcpy(host + 1 * 65, u.nodename, std::strlen(u.nodename));
            std::memcpy(host + 2 * 65, u.release, std::strlen(u.release));
            std::memcpy(host + 3 * 65, u.version, std::strlen(u.version));
            std::memcpy(host + 4 * 65, u.machine, std::strlen(u.machine));
            return 0;
        }

        case kFcntl64: return 0;

        case kReadlink:
        case kReadlinkat: {
            // Only /proc/self/exe matters for a process that wants to know its own path; there
            // is no filesystem layer yet, so everything else is ENOENT.
            return -ENOENT;
        }

        case kGetcwd: return -ENOENT;
        case kAccess:
        case kFaccessat: {
            const std::uint32_t path = number == kAccess ? reg(0) : reg(1);
            const std::string text = guest_string(path);
            if (vfs_ != nullptr && !vfs_->resolve(text).empty()) return 0;
            path_attempts_.push_back(std::string(syscall_name(number)) + "(" + text + ") -> -ENOENT");
            return -ENOENT;
        }

        case kOpen:
        case kOpenat: {
            const std::uint32_t path = number == kOpen ? reg(0) : reg(1);
            const std::string text = guest_string(path);
            if (vfs_ != nullptr) {
                const std::string host_path = vfs_->resolve(text);
                if (!host_path.empty()) {
                    const int fd = ::open(host_path.c_str(), O_RDONLY | O_CLOEXEC);
                    if (fd >= 0) {
                        struct stat info {};
                        ::fstat(fd, &info);
                        OpenFile file;
                        file.host_fd = fd;
                        file.size = static_cast<std::uint64_t>(info.st_size);
                        file.mode = 0100644;
                        const std::int32_t guest_fd = next_fd_++;
                        files_[guest_fd] = file;
                        path_attempts_.push_back("open(" + text + ") -> fd " + std::to_string(guest_fd) +
                                                 " (" + std::to_string(file.size) + " bytes)");
                        return guest_fd;
                    }
                }
            }
            path_attempts_.push_back("open(" + text + ") -> -ENOENT");
            return -ENOENT;
        }

        case kClose: {
            const std::int32_t fd = static_cast<std::int32_t>(reg(0));
            const auto it = files_.find(fd);
            if (it == files_.end()) return -EBADF;
            ::close(it->second.host_fd);
            files_.erase(it);
            return 0;
        }

        case kLlseek: {
            // _llseek(fd, offset_high, offset_low, loff_t* result, whence). bionic uses this on
            // 32-bit ARM for its large-file seek, and the engine's asset streams use it heavily:
            // it was the single unimplemented syscall standing between the engine and its own
            // content, and returning an error made a stream report a size of -1.
            const std::int32_t fd = static_cast<std::int32_t>(reg(0));
            const std::uint32_t offset_high = reg(1);
            const std::uint32_t offset_low = reg(2);
            const std::uint32_t result = reg(3);
            const std::uint32_t whence = syscall_arg(4);
            const auto it = files_.find(fd);
            if (it == files_.end()) return -EBADF;
            const std::int64_t base = whence == SEEK_SET   ? 0
                                      : whence == SEEK_CUR ? static_cast<std::int64_t>(it->second.offset)
                                                           : static_cast<std::int64_t>(it->second.size);
            const std::int64_t delta =
                static_cast<std::int64_t>((static_cast<std::uint64_t>(offset_high) << 32) | offset_low);
            const std::int64_t next = base + delta;
            if (next < 0) return -EINVAL;
            it->second.offset = static_cast<std::uint64_t>(next);
            if (io_trace_ < io_trace_limit_) {
                ++io_trace_;
                std::fprintf(stderr, "dh2: _llseek fd=%d whence=%u -> 0x%llx\n", fd, whence,
                             static_cast<unsigned long long>(next));
            }
            if (result != 0 && mem_.accessible(result, 8, kPageWrite)) {
                const std::uint64_t value = static_cast<std::uint64_t>(next);
                mem_.copy_in(result, &value, 8);
            }
            return 0;
        }

        case kLseek: {
            const std::int32_t fd = static_cast<std::int32_t>(reg(0));
            const auto it = files_.find(fd);
            if (it == files_.end()) return -EBADF;
            const std::int32_t offset = static_cast<std::int32_t>(reg(1));
            const int whence = static_cast<int>(reg(2));
            const std::int64_t base = whence == SEEK_SET   ? 0
                                      : whence == SEEK_CUR ? static_cast<std::int64_t>(it->second.offset)
                                                           : static_cast<std::int64_t>(it->second.size);
            const std::int64_t next = base + offset;
            if (next < 0) return -EINVAL;
            it->second.offset = static_cast<std::uint64_t>(next);
            return static_cast<std::int32_t>(next);
        }

        case kFstat64: {
            const std::int32_t fd = static_cast<std::int32_t>(reg(0));
            const auto it = files_.find(fd);
            if (it == files_.end()) return -EBADF;
            std::uint8_t* out = mem_.host_ptr(reg(1), kStat64Size, kPageWrite);
            if (out == nullptr) return -EFAULT;
            fill_stat64(out, it->second.mode, it->second.size);
            return 0;
        }

        case kStat64:
        case kFstatat64: {
            const std::uint32_t path = number == kStat64 ? reg(0) : reg(1);
            const std::uint32_t out_address = number == kStat64 ? reg(1) : reg(2);
            const std::string text = guest_string(path);
            const std::string host_path = vfs_ != nullptr ? vfs_->resolve(text) : std::string();
            if (host_path.empty()) {
                path_attempts_.push_back("stat(" + text + ") -> -ENOENT");
                return -ENOENT;
            }
            std::uint8_t* out = mem_.host_ptr(out_address, kStat64Size, kPageWrite);
            if (out == nullptr) return -EFAULT;
            struct stat info {};
            ::stat(host_path.c_str(), &info);
            fill_stat64(out, 0100644, static_cast<std::uint64_t>(info.st_size));
            return 0;
        }

        case kConnect:
            // Gameloft Live. There is no server and no route; a device would report the
            // connection refused, and the engine's network state machine handles that. ENOSYS
            // was worse: it left the socket open and the next write raised SIGPIPE.
            return -ECONNREFUSED;

        case kIoctl:
        case kDup:
        case kPoll:
            return -ENOENT;

        default:
            return -ENOSYS;
    }
}

void SyscallLayer::print_census(FILE* out) const {
    std::vector<CensusEntry> sorted = census_;
    std::sort(sorted.begin(), sorted.end(),
              [](const CensusEntry& a, const CensusEntry& b) { return a.number < b.number; });
    std::fprintf(out, "  syscalls issued: %llu across %zu number(s)\n",
                 static_cast<unsigned long long>(handled_), sorted.size());
    for (const auto& e : sorted) {
        std::fprintf(out, "    %-24s #%-6d calls=%llu\n", e.name.c_str(), e.number,
                     static_cast<unsigned long long>(e.calls));
    }
    if (!path_attempts_.empty()) {
        std::fprintf(out, "  paths tried: %zu\n", path_attempts_.size());
        for (const auto& p : path_attempts_) std::fprintf(out, "    %s\n", p.c_str());
    }
    if (!unimplemented_.empty()) {
        std::fprintf(out, "  not implemented: %zu\n", unimplemented_.size());
        for (const auto& u : unimplemented_) std::fprintf(out, "    %s\n", u.c_str());
    }
}

}  // namespace dh2
