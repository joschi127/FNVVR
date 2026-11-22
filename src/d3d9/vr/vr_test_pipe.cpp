#include "vr_test_pipe.h"

#include <algorithm>
#include <cstdio>
#include <psapi.h>

#include <chrono>
#include <utility>

#include "../../util/log/log.h"
#include "../../util/util_env.h"
#include "../../util/util_string.h"

#include "vr_test_commands.h"

namespace dxvk {

  namespace {

    /// A request the render thread has not answered within this time fails
    constexpr auto ReplyTimeout = std::chrono::seconds(60);

    /**
     * \brief Access rules that let only the game's own user open the pipe
     *
     * The descriptor and its list point into this object, so it has to live
     * until the pipe is created.
     */
    struct UserOnlyAccess {
      std::vector<BYTE>    user;
      std::vector<BYTE>    acl;
      SECURITY_DESCRIPTOR  descriptor = { };

      bool build() {
        HANDLE token = nullptr;

        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
          return false;

        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        user.resize(size);
        bool ok = size && GetTokenInformation(token, TokenUser, user.data(), size, &size);
        CloseHandle(token);

        if (!ok)
          return false;

        PSID sid = reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid;
        acl.resize(sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) + GetLengthSid(sid));
        auto* list = reinterpret_cast<ACL*>(acl.data());

        return InitializeAcl(list, DWORD(acl.size()), ACL_REVISION)
            && AddAccessAllowedAce(list, ACL_REVISION, GENERIC_ALL, sid)
            && InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION)
            && SetSecurityDescriptorDacl(&descriptor, TRUE, list, FALSE);
      }
    };

    HANDLE createPipe(const std::string& path, PSECURITY_DESCRIPTOR descriptor) {
      SECURITY_ATTRIBUTES attributes = { };
      attributes.nLength = sizeof(attributes);
      attributes.lpSecurityDescriptor = descriptor;

      return CreateNamedPipeA(path.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 64 * 1024, 8 * 1024, 0, &attributes);
    }

  }


  std::string vrTestMemoryStatus() {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    auto low  = reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
    auto high = reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);

    uint64_t freeBytes = 0, largest = 0;

    for (uintptr_t at = low; at < high; ) {
      MEMORY_BASIC_INFORMATION region;

      if (!VirtualQuery(reinterpret_cast<void*>(at), &region, sizeof(region)) || !region.RegionSize)
        break;

      if (region.State == MEM_FREE) {
        freeBytes += region.RegionSize;
        largest = std::max<uint64_t>(largest, region.RegionSize);
      }

      uintptr_t next = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;

      if (next <= at)
        break;

      at = next;
    }

    // K32GetProcessMemoryInfo is in kernel32 since Windows 7; looked up to avoid linking psapi
    using GetInfo = BOOL (WINAPI*)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
    auto getInfo = reinterpret_cast<GetInfo>(reinterpret_cast<void*>(
      GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo")));
    PROCESS_MEMORY_COUNTERS_EX counters = { };
    counters.cb = sizeof(counters);
    bool haveCounters = getInfo
      && getInfo(GetCurrentProcess(), reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&counters), sizeof(counters));

    constexpr double MB = 1024.0 * 1024.0;
    double total = double(high - low + 1);
    char text[192];
    std::snprintf(text, sizeof(text), "vfree=%.0f vlargest=%.0f vused=%.0f vtotal=%.0f private=%.0f workingset=%.0f",
      double(freeBytes) / MB, double(largest) / MB, (total - double(freeBytes)) / MB, total / MB,
      haveCounters ? double(counters.PrivateUsage) / MB : -1.0,
      haveCounters ? double(counters.WorkingSetSize) / MB : -1.0);
    return text;
  }


  VrTestPipe::~VrTestPipe() {
    stop();
  }


  std::string VrTestPipe::start(const std::string& name) {
    UserOnlyAccess access;

    if (!access.build()) {
      Logger::err(str::format("VR: Test channel: no user-only access rules (", GetLastError(), ")"));
      return std::string();
    }

    std::string path = "\\\\.\\pipe\\" + name;
    HANDLE pipe = createPipe(path, &access.descriptor);

    if (pipe == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED) {
      path += "-" + std::to_string(GetCurrentProcessId());
      pipe = createPipe(path, &access.descriptor);
    }

    if (pipe == INVALID_HANDLE_VALUE) {
      Logger::err(str::format("VR: Test channel: CreateNamedPipe failed (", GetLastError(), ")"));
      return std::string();
    }

    m_pipe = pipe;
    m_worker = dxvk::thread([this] { serve(); });
    return path;
  }


  void VrTestPipe::stop() {
    if (!m_worker.joinable())
      return;

    m_quit = true;

    // The worker blocks in ConnectNamedPipe, ReadFile or WriteFile
    while (WaitForSingleObject(m_worker.native_handle(), 50) == WAIT_TIMEOUT)
      CancelSynchronousIo(m_worker.native_handle());

    m_worker.join();
    CloseHandle(m_pipe);
    m_pipe = nullptr;
  }


  std::shared_ptr<VrTestJob> VrTestPipe::poll() {
    if (!m_waiting.load(std::memory_order_acquire))
      return nullptr;

    std::lock_guard<std::mutex> guard(m_lock);
    m_waiting.store(false, std::memory_order_relaxed);
    return std::exchange(m_job, nullptr);
  }


  std::vector<std::string> VrTestPipe::hand(const std::string& line) {
    auto job = std::make_shared<VrTestJob>();
    job->line = line;
    std::future<std::vector<std::string>> answer = job->reply.get_future();

    { std::lock_guard<std::mutex> guard(m_lock);
      m_job = job;
      m_waiting.store(true, std::memory_order_release);
    }

    auto deadline = std::chrono::steady_clock::now() + ReplyTimeout;

    while (std::chrono::steady_clock::now() < deadline && !m_quit) {
      if (answer.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready)
        return answer.get();
    }

    bool picked;

    { std::lock_guard<std::mutex> guard(m_lock);
      picked = m_job != job;

      if (!picked) {
        m_job = nullptr;
        m_waiting.store(false, std::memory_order_relaxed);
      }
    }

    // A picked-up job may still be waiting for frames on the render thread
    job->abandoned = true;

    if (m_quit)
      return { "err the game is closing" };

    return { picked ? "err no answer in time" : "err no frame picked the request up in time" };
  }


  void VrTestPipe::serveClient() {
    std::string pending;
    bool dropping = false;
    char chunk[2048];

    while (!m_quit) {
      DWORD got = 0;

      if (!ReadFile(m_pipe, chunk, sizeof(chunk), &got, nullptr) || !got)
        return;

      for (DWORD i = 0; i < got; i++) {
        char c = chunk[i];

        if (dropping) {
          dropping = c != '\n';
          continue;
        }

        if (c != '\n') {
          pending.push_back(c);

          if (pending.size() > VrTestMaxLine) {
            pending.clear();
            dropping = true;

            std::string out = "err line too long\n";
            DWORD wrote = 0;

            if (!WriteFile(m_pipe, out.data(), DWORD(out.size()), &wrote, nullptr))
              return;
          }

          continue;
        }

        if (!pending.empty() && pending.back() == '\r')
          pending.pop_back();

        // Answered here, so it also works while no frames run (loading screens)
        std::string request = pending;
        request.erase(0, request.find_first_not_of(" \t"));
        request.erase(request.find_last_not_of(" \t") + 1);

        std::vector<std::string> answer = (request == "memory" || request == "MEMORY")
          ? std::vector<std::string> { "ok " + vrTestMemoryStatus() }
          : hand(pending);
        pending.clear();

        std::string out;

        for (const auto& l : answer)
          out.append(l).push_back('\n');

        DWORD wrote = 0;

        if (!WriteFile(m_pipe, out.data(), DWORD(out.size()), &wrote, nullptr) || wrote != out.size())
          return;
      }
    }
  }


  void VrTestPipe::serve() {
    env::setThreadName("dxvk-vr-test");

    while (!m_quit) {
      bool connected = ConnectNamedPipe(m_pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;

      if (m_quit)
        break;

      DWORD error = connected ? 0 : GetLastError();

      if (connected) {
        serveClient();
      } else if (error != ERROR_OPERATION_ABORTED && error != ERROR_NO_DATA) {
        Logger::warn(str::format("VR: Test channel: ConnectNamedPipe failed (", error, ")"));
        Sleep(500);
      }

      DisconnectNamedPipe(m_pipe);
    }
  }

}
