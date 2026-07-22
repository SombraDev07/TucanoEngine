#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace tucano::shadows {

/// Matriz de view ortográfica alinhada com a direção do sol.
/// Estável para sol a pino (troca o up) e melhor distribuição de texels que lookAt simples.
inline glm::mat4 lightDirOrthoMatrix(const glm::vec3& sunDir) {
  glm::vec3 up(0, 1, 0);
  if (glm::abs(sunDir.y) > 0.999f) {
    up = glm::vec3(0, 0, 1);
  }
  return glm::lookAtLH(glm::vec3(0), sunDir, up);
}

/// Matriz skewed: alinha texels com a direção do sol (sol baixo no horizonte).
inline glm::mat4 calcSkewedLightMatrix(const glm::vec3& sunDir, bool align) {
  glm::mat4 lightItm(1.0f);
  lightItm[0] = glm::vec4(1, 0, 0, 0);
  lightItm[1] = glm::vec4(0, 0, 1, 0);
  lightItm[2] = glm::vec4(-sunDir, 0);

  if (align && (glm::abs(sunDir.z) > 0.00001f || glm::abs(sunDir.x) > 0.00001f)) {
    float angle = glm::atan(sunDir.z, sunDir.x);
    return glm::inverse(lightItm * glm::rotate(glm::mat4(1), -angle, glm::vec3(0, 0, 1)));
  }
  return glm::inverse(lightItm);
}

/// Escala view-proj da luz para coordenadas de shadow map [0,1].
inline glm::mat4 scaleShadowZ(const glm::mat4& viewProj, float xyScale, float invZRange, float zOfs) {
  glm::mat4 scale(1.0f);
  scale[0][0] = xyScale;
  scale[1][1] = -xyScale;
  scale[2][2] = invZRange;
  scale[3][2] = zOfs;
  return scale * viewProj;
}

/// Divide um bounding box que cruza bordas do toroid em 1-4 sub-regiões que não cruzam bordas.
inline int toroidalBoxSplit(const glm::vec3& boxMin, const glm::vec3& boxMax, const glm::vec3& origin,
                            const glm::vec3& gridSize, glm::vec3* outMins, glm::vec3* outMaxs) {
  glm::ivec3 cellMin(int(glm::floor((boxMin.x - origin.x) / gridSize.x)),
                     int(glm::floor((boxMin.y - origin.y) / gridSize.y)),
                     int(glm::floor((boxMin.z - origin.z) / gridSize.z)));
  glm::ivec3 cellMax(int(glm::floor((boxMax.x - origin.x) / gridSize.x)),
                     int(glm::floor((boxMax.y - origin.y) / gridSize.y)),
                     int(glm::floor((boxMax.z - origin.z) / gridSize.z)));
  if (cellMin == cellMax) {
    outMins[0] = boxMin;
    outMaxs[0] = boxMax;
    return 1;
  }
  int count = 0;
  for (int z = cellMin.z; z <= cellMax.z && count < 4; ++z)
    for (int y = cellMin.y; y <= cellMax.y && count < 4; ++y)
      for (int x = cellMin.x; x <= cellMax.x && count < 4; ++x) {
        outMins[count] = glm::vec3(glm::max(boxMin.x, origin.x + float(x) * gridSize.x),
                                   glm::max(boxMin.y, origin.y + float(y) * gridSize.y),
                                   glm::max(boxMin.z, origin.z + float(z) * gridSize.z));
        outMaxs[count] = glm::vec3(glm::min(boxMax.x, origin.x + float(x + 1) * gridSize.x),
                                   glm::min(boxMax.y, origin.y + float(y + 1) * gridSize.y),
                                   glm::min(boxMax.z, origin.z + float(z + 1) * gridSize.z));
        count++;
      }
  return count > 0 ? count : 1;
}

} // namespace tucano::shadows
