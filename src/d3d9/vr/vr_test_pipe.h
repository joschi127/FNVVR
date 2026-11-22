#pragma once

#include <windows.h>

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../../util/thread.h"

namespace dxvk {

  /**
   * \brief One request of a test tool, handed to the render thread
   */
  struct VrTestJob {
    std::string line;
    /// All answer lines; the last one is "ok ..." or "err ..."
    std::promise<std::vector<std::string>> reply;
    /// Set by the pipe when it gave up waiting; the render thread then drops the job
    std::atomic<bool> abandoned = { false };
  };

  /**
   * \brief The game process's memory for the "memory" request
   *
   * Free, largest free and used address space (the scarce resource of a
   * 32-bit game), committed and resident memory, in MB. Walks the address
   * space with VirtualQuery, about a millisecond; safe from any thread.
   */
  std::string vrTestMemoryStatus();

  /**
   * \brief Named pipe of the VR test channel
   *
   * One tool at a time, only the Windows user running the game, no remote
   * clients. The pipe has a thread of its own with blocking I/O; the render
   * thread takes requests with poll() and answers through the job's promise.
   */
  class VrTestPipe {

  public:

    VrTestPipe() = default;
    VrTestPipe(const VrTestPipe&) = delete;
    VrTestPipe& operator = (const VrTestPipe&) = delete;
    ~VrTestPipe();

    /**
     * \brief Listens on \\.\pipe\<name>
     *
     * If another game already listens under that name, the pipe gets the
     * process id appended.
     * \returns The full pipe name, empty on failure
     */
    std::string start(const std::string& name);

    void stop();

    /**
     * \brief A waiting request, if any
     *
     * Without one this is a single atomic load, so an idle channel costs
     * next to nothing per frame.
     */
    std::shared_ptr<VrTestJob> poll();

  private:

    void serve();
    void serveClient();
    std::vector<std::string> hand(const std::string& line);

    HANDLE                     m_pipe = nullptr;
    dxvk::thread               m_worker;
    std::atomic<bool>          m_quit    = { false };
    std::atomic<bool>          m_waiting = { false };
    std::mutex                 m_lock;
    std::shared_ptr<VrTestJob> m_job;

  };

}
