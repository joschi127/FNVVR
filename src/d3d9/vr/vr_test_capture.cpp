#include "vr_test_capture.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>

namespace dxvk {

  namespace {

    float halfToFloat(uint16_t h) {
      uint32_t sign = (h >> 15) & 1;
      uint32_t exp  = (h >> 10) & 0x1F;
      uint32_t mant = h & 0x3FF;
      float value;

      if (exp == 0)
        value = std::ldexp(float(mant), -24);
      else if (exp == 31)
        value = mant ? 0.0f : 65504.0f;
      else
        value = std::ldexp(float(mant | 0x400), int(exp) - 25);

      return sign ? -value : value;
    }

    uint8_t unit(float v) {
      v = std::min(std::max(v, 0.0f), 1.0f);
      return uint8_t(v * 255.0f + 0.5f);
    }

    float linearToSrgb(float c) {
      c = std::min(std::max(c, 0.0f), 1.0f);
      return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    }

  }


  uint32_t vrTestBytesPerPixel(VkFormat format) {
    switch (format) {
      case VK_FORMAT_R8G8B8A8_UNORM:
      case VK_FORMAT_R8G8B8A8_SRGB:
      case VK_FORMAT_B8G8R8A8_UNORM:
      case VK_FORMAT_B8G8R8A8_SRGB:
      case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
      case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
        return 4;
      case VK_FORMAT_R16G16B16A16_SFLOAT:
      case VK_FORMAT_R16G16B16A16_UNORM:
        return 8;
      default:
        return 0;
    }
  }


  bool vrTestConvertRow(VkFormat format, const uint8_t* src, uint32_t width, bool opaque, uint8_t* dst) {
    for (uint32_t x = 0; x < width; x++) {
      uint8_t* out = dst + x * 4;

      switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
          std::memcpy(out, src + x * 4, 4);
          break;

        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
          out[0] = src[x * 4 + 2];
          out[1] = src[x * 4 + 1];
          out[2] = src[x * 4 + 0];
          out[3] = src[x * 4 + 3];
          break;

        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32: {
          uint32_t v;
          std::memcpy(&v, src + x * 4, 4);
          uint32_t lo = (v >> 2) & 0xFF, mid = (v >> 12) & 0xFF, hi = (v >> 22) & 0xFF;
          bool abgr = format == VK_FORMAT_A2B10G10R10_UNORM_PACK32;
          out[0] = uint8_t(abgr ? lo : hi);
          out[1] = uint8_t(mid);
          out[2] = uint8_t(abgr ? hi : lo);
          out[3] = uint8_t(((v >> 30) & 3) * 85);
          break;
        }

        case VK_FORMAT_R16G16B16A16_SFLOAT: {
          uint16_t v[4];
          std::memcpy(v, src + x * 8, 8);

          for (int c = 0; c < 3; c++)
            out[c] = unit(linearToSrgb(halfToFloat(v[c])));

          out[3] = unit(halfToFloat(v[3]));
          break;
        }

        case VK_FORMAT_R16G16B16A16_UNORM: {
          uint16_t v[4];
          std::memcpy(v, src + x * 8, 8);

          for (int c = 0; c < 4; c++)
            out[c] = uint8_t(v[c] >> 8);
          break;
        }

        default:
          return false;
      }

      if (opaque)
        out[3] = 255;
    }

    return true;
  }


  bool VrTestCapture::init(const VrGraphicsBinding& binding) {
    m_ready = false;

    if (!binding.getInstanceProcAddr || !binding.instance || !binding.device || !binding.queue)
      return false;

    auto instanceProc = binding.getInstanceProcAddr;
    auto getDeviceProc = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
      instanceProc(binding.instance, "vkGetDeviceProcAddr"));
    auto getMemoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
      instanceProc(binding.instance, "vkGetPhysicalDeviceMemoryProperties"));

    if (!getDeviceProc || !getMemoryProperties)
      return false;

    m_device = binding.device;
    m_queue  = binding.queue;
    m_family = binding.queueFamilyIndex;
    getMemoryProperties(binding.physicalDevice, &m_memory);

    bool ok = true;
    auto load = [&] (auto& fn, const char* name) {
      fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(getDeviceProc(m_device, name));
      ok = ok && fn;
    };

    load(m_createBuffer,           "vkCreateBuffer");
    load(m_destroyBuffer,          "vkDestroyBuffer");
    load(m_bufferRequirements,     "vkGetBufferMemoryRequirements");
    load(m_allocateMemory,         "vkAllocateMemory");
    load(m_freeMemory,             "vkFreeMemory");
    load(m_bindBufferMemory,       "vkBindBufferMemory");
    load(m_mapMemory,              "vkMapMemory");
    load(m_unmapMemory,            "vkUnmapMemory");
    load(m_createCommandPool,      "vkCreateCommandPool");
    load(m_destroyCommandPool,     "vkDestroyCommandPool");
    load(m_allocateCommandBuffers, "vkAllocateCommandBuffers");
    load(m_beginCommandBuffer,     "vkBeginCommandBuffer");
    load(m_endCommandBuffer,       "vkEndCommandBuffer");
    load(m_cmdPipelineBarrier,     "vkCmdPipelineBarrier");
    load(m_cmdCopyImageToBuffer,   "vkCmdCopyImageToBuffer");
    load(m_createFence,            "vkCreateFence");
    load(m_destroyFence,           "vkDestroyFence");
    load(m_waitForFences,          "vkWaitForFences");
    load(m_queueSubmit,            "vkQueueSubmit");
    load(m_invalidate,             "vkInvalidateMappedMemoryRanges");

    m_ready = ok;
    return ok;
  }


  bool VrTestCapture::read(const VrEyeImage& image, bool opaque, const RowSink& row, std::string& error) {
    if (!m_ready) {
      error = "no graphics device for captures";
      return false;
    }

    uint32_t bpp = vrTestBytesPerPixel(image.format);

    if (!bpp) {
      error = "image format " + std::to_string(uint32_t(image.format)) + " is not supported";
      return false;
    }

    if (!image.image || !image.extent.width || !image.extent.height || image.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
      error = "no image";
      return false;
    }

    VkDeviceSize size = VkDeviceSize(image.extent.width) * image.extent.height * bpp;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    bool ok = false;

    do {
      VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
      bufferInfo.size  = size;
      bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

      if (m_createBuffer(m_device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        error = "vkCreateBuffer failed";
        break;
      }

      VkMemoryRequirements requirements;
      m_bufferRequirements(m_device, buffer, &requirements);

      // Cached system memory reads fast on the CPU and leaves the small
      // device-local window (BAR) that DXVK needs alone
      uint32_t type = UINT32_MAX;
      auto pick = [&] (VkMemoryPropertyFlags wanted, VkMemoryPropertyFlags unwanted) {
        for (uint32_t i = 0; i < m_memory.memoryTypeCount && type == UINT32_MAX; i++) {
          VkMemoryPropertyFlags flags = m_memory.memoryTypes[i].propertyFlags;

          if ((requirements.memoryTypeBits & (1u << i)) && (flags & wanted) == wanted && !(flags & unwanted))
            type = i;
        }
      };

      pick(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      pick(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, 0);
      pick(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      pick(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 0);
      bool coherent = type != UINT32_MAX
        && (m_memory.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

      VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
      allocInfo.allocationSize  = requirements.size;
      allocInfo.memoryTypeIndex = type;

      if (type == UINT32_MAX || m_allocateMemory(m_device, &allocInfo, nullptr, &memory) != VK_SUCCESS
       || m_bindBufferMemory(m_device, buffer, memory, 0) != VK_SUCCESS) {
        error = "no host-visible memory for the copy";
        break;
      }

      VkCommandPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
      poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
      poolInfo.queueFamilyIndex = m_family;

      VkCommandBuffer cmd = VK_NULL_HANDLE;
      VkCommandBufferAllocateInfo cmdInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
      cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      cmdInfo.commandBufferCount = 1;

      if (m_createCommandPool(m_device, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        error = "vkCreateCommandPool failed";
        break;
      }

      cmdInfo.commandPool = pool;

      if (m_allocateCommandBuffers(m_device, &cmdInfo, &cmd) != VK_SUCCESS) {
        error = "vkAllocateCommandBuffers failed";
        break;
      }

      VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
      begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      m_beginCommandBuffer(cmd, &begin);

      VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
      barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      barrier.oldLayout = image.layout;
      barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image = image.image;
      barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, image.arrayLayer, 1 };

      m_cmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

      VkBufferImageCopy region = { };
      region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, image.arrayLayer, 1 };
      region.imageExtent = { image.extent.width, image.extent.height, 1 };
      m_cmdCopyImageToBuffer(cmd, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);

      VkBufferMemoryBarrier hostRead = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
      hostRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      hostRead.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
      hostRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      hostRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      hostRead.buffer = buffer;
      hostRead.size = VK_WHOLE_SIZE;
      m_cmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 0, nullptr, 1, &hostRead, 0, nullptr);

      barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
      barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      barrier.newLayout = image.layout;
      m_cmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

      m_endCommandBuffer(cmd);

      VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };

      if (m_createFence(m_device, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
        error = "vkCreateFence failed";
        break;
      }

      VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
      submit.commandBufferCount = 1;
      submit.pCommandBuffers = &cmd;

      if (m_queueSubmit(m_queue, 1, &submit, fence) != VK_SUCCESS) {
        error = "vkQueueSubmit failed";
        break;
      }

      // No time limit: freeing the objects while the GPU still copies would be worse than a stall
      if (m_waitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        error = "the copy did not finish (device lost?)";
        break;
      }

      void* data = nullptr;

      if (m_mapMemory(m_device, memory, 0, size, 0, &data) != VK_SUCCESS) {
        error = "vkMapMemory failed";
        break;
      }

      if (!coherent) {
        VkMappedMemoryRange range = { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
        range.memory = memory;
        range.size   = VK_WHOLE_SIZE;
        m_invalidate(m_device, 1, &range);
      }

      std::vector<uint8_t> line(size_t(image.extent.width) * 4);
      const uint8_t* src = static_cast<const uint8_t*>(data);
      ok = true;

      for (uint32_t y = 0; y < image.extent.height && ok; y++) {
        vrTestConvertRow(image.format, src + size_t(y) * image.extent.width * bpp, image.extent.width, opaque,
          line.data());
        ok = row(line.data());
      }

      if (!ok)
        error = "could not write the file";

      m_unmapMemory(m_device, memory);
    } while (false);

    if (fence)  m_destroyFence(m_device, fence, nullptr);
    if (pool)   m_destroyCommandPool(m_device, pool, nullptr);
    if (buffer) m_destroyBuffer(m_device, buffer, nullptr);
    if (memory) m_freeMemory(m_device, memory, nullptr);
    return ok;
  }

}
