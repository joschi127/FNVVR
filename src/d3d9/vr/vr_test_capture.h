#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vr_backend.h"

namespace dxvk {

  /**
   * \brief Reads images submitted to the VR backend back to system memory
   *
   * For the test channel's captures only; does nothing until asked. Uses the
   * device and queue of the VR graphics binding. Must be called while the
   * queue is locked (inside IVRBackend::submitFrame or submitPanel), and
   * waits for the copy, so a capture stalls that one frame.
   */
  class VrTestCapture {

  public:

    VrTestCapture() = default;
    VrTestCapture(const VrTestCapture&) = delete;
    VrTestCapture& operator = (const VrTestCapture&) = delete;

    /// Takes the device from the binding; false if a function is missing
    bool init(const VrGraphicsBinding& binding);

    bool ready() const { return m_ready; }

    /**
     * \brief Copies an image into RGBA8 pixels, top row first
     *
     * \param [in] opaque Set alpha to 255 (eye images carry no useful alpha)
     * \param [out] error Why it failed
     */
    bool read(const VrEyeImage& image, bool opaque, std::vector<uint8_t>& rgba, std::string& error);

  private:

    bool                    m_ready = false;
    VkDevice                m_device = VK_NULL_HANDLE;
    VkQueue                 m_queue = VK_NULL_HANDLE;
    uint32_t                m_family = 0;
    VkPhysicalDeviceMemoryProperties m_memory = { };

    PFN_vkCreateBuffer              m_createBuffer = nullptr;
    PFN_vkDestroyBuffer             m_destroyBuffer = nullptr;
    PFN_vkGetBufferMemoryRequirements m_bufferRequirements = nullptr;
    PFN_vkAllocateMemory            m_allocateMemory = nullptr;
    PFN_vkFreeMemory                m_freeMemory = nullptr;
    PFN_vkBindBufferMemory          m_bindBufferMemory = nullptr;
    PFN_vkMapMemory                 m_mapMemory = nullptr;
    PFN_vkUnmapMemory               m_unmapMemory = nullptr;
    PFN_vkCreateCommandPool         m_createCommandPool = nullptr;
    PFN_vkDestroyCommandPool        m_destroyCommandPool = nullptr;
    PFN_vkAllocateCommandBuffers    m_allocateCommandBuffers = nullptr;
    PFN_vkBeginCommandBuffer        m_beginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer          m_endCommandBuffer = nullptr;
    PFN_vkCmdPipelineBarrier        m_cmdPipelineBarrier = nullptr;
    PFN_vkCmdCopyImageToBuffer      m_cmdCopyImageToBuffer = nullptr;
    PFN_vkCreateFence               m_createFence = nullptr;
    PFN_vkDestroyFence              m_destroyFence = nullptr;
    PFN_vkWaitForFences             m_waitForFences = nullptr;
    PFN_vkQueueSubmit               m_queueSubmit = nullptr;

  };

  /**
   * \brief Converts one row of pixels of a supported format to RGBA8
   *
   * \returns \c false for a format the capture does not handle
   */
  bool vrTestConvertRow(VkFormat format, const uint8_t* src, uint32_t width, bool opaque, uint8_t* dst);

  /// Bytes per pixel of a format vrTestConvertRow handles, 0 otherwise
  uint32_t vrTestBytesPerPixel(VkFormat format);

}
