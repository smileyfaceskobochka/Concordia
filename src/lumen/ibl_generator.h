#pragma once
#include "memoria/types.h"
#include <memory>
#include <string>
#include <vector>

namespace Memoria {
class Allocator;
}

namespace Lumen {

struct IBLMaps {
  std::shared_ptr<Memoria::TextureAsset> irradianceMap;
  std::shared_ptr<Memoria::TextureAsset> prefilterMap;
  std::shared_ptr<Memoria::TextureAsset> brdfLUT;
};

class IBLGenerator {
public:
  static IBLMaps generate(Memoria::Allocator &allocator, VkDevice device,
                          VkQueue transferQueue, VkCommandPool transferPool,
                          const std::string &skyboxCrossPath);

  static IBLMaps generateFromFaces(Memoria::Allocator &allocator,
                                   VkDevice device, VkQueue transferQueue,
                                   VkCommandPool transferPool,
                                   const std::vector<std::vector<float>> &srcFaces,
                                   int srcFaceSize);

  static IBLMaps generateFromHDR(Memoria::Allocator &allocator,
                                  VkDevice device, VkQueue transferQueue,
                                  VkCommandPool transferPool,
                                  const std::string &hdrPath,
                                  int faceSize = 512);
};

} // namespace Lumen
