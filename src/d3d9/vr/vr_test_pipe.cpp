#include "vr_test_pipe.h"

#include <sddl.h>

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

    /// DACL with one entry: full access for the user running the game
    PSECURITY_DESCRIPTOR ownerOnlyDescriptor() {
      PSECURITY_DESCRIPTOR result = nullptr;
      HANDLE token = nullptr;

      if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return nullptr;

      DWORD needed = 0;
      GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
      std::vector<BYTE> info(needed);
      LPWSTR sidText = nullptr;

      if (needed && GetTokenInformation(token, TokenUser, info.data(), needed, &needed)
       && ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(info.data())->User.Sid, &sidText)) {
        std::wstring sddl = std::wstring(L"D:P(A;;GA;;;") + sidText + L")";

        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &result, nullptr))
          result = nullptr;

        LocalFree(sidText);
      }

      CloseHandle(token);
      return result;
    }

    HANDLE createPipe(const std::string& path, PSECURITY_DESCRIPTOR descriptor) {
      SECURITY_ATTRIBUTES attributes = { };
      attributes.nLength = sizeof(attributes);
      attributes.lpSecurityDescriptor = descriptor;

      return CreateNamedPipeA(path.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 64 * 1024, 8 * 1024, 0, &attributes);
    }

  }


  VrTestPipe::~VrTestPipe() {
    stop();
  }


  std::string VrTestPipe::start(const std::string& name) {
    PSECURITY_DESCRIPTOR descriptor = ownerOnlyDescriptor();

    if (!descriptor) {
      Logger::err(str::format("VR: Test channel: no owner-only security descriptor (", GetLastError(), ")"));
      return std::string();
    }

    std::string path = "\\\\.\\pipe\\" + name;
    HANDLE pipe = createPipe(path, descriptor);

    if (pipe == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED) {
      path += "-" + std::to_string(GetCurrentProcessId());
      pipe = createPipe(path, descriptor);
    }

    LocalFree(descriptor);

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

        std::vector<std::string> answer = hand(pending);
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

      if (connected) {
        serveClient();
      } else if (GetLastError() != ERROR_OPERATION_ABORTED) {
        Logger::warn(str::format("VR: Test channel: ConnectNamedPipe failed (", GetLastError(), ")"));
        Sleep(500);
      }

      DisconnectNamedPipe(m_pipe);
    }
  }

}
