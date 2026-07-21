#include "AssetPipeline/GLTFLoader.h"
#include "AssetPipeline/ImageLoader.h"
#include "Platform/FileSystem.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <limits>
#include <unordered_map>

namespace tucano {
namespace {

glm::mat4 nodeLocalMatrix(const cgltf_node* node) {
  if (node->has_matrix) {
    return glm::make_mat4(node->matrix);
  }
  Transform t;
  if (node->has_translation) {
    t.translation = glm::make_vec3(node->translation);
  }
  if (node->has_rotation) {
    t.rotation = glm::quat(node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2]);
  }
  if (node->has_scale) {
    t.scale = glm::make_vec3(node->scale);
  }
  return t.matrix();
}

std::shared_ptr<Texture> loadTexture(rhi::Device& device, const cgltf_texture_view& view, const std::string& baseDir,
                                     bool srgb) {
  if (!view.texture || !view.texture->image) {
    return nullptr;
  }
  const cgltf_image* image = view.texture->image;
  ImageData img;
  if (image->uri) {
    img = loadImageRGBA8(joinPath(baseDir, image->uri));
  } else if (image->buffer_view && image->buffer_view->buffer->data) {
    const auto* bv = image->buffer_view;
    const auto* data = static_cast<const uint8_t*>(bv->buffer->data) + bv->offset;
    img = loadImageRGBA8FromMemory(data, bv->size);
  } else {
    return nullptr;
  }

  rhi::TextureDesc desc{};
  desc.width = img.width;
  desc.height = img.height;
  desc.format = srgb ? rhi::Format::R8G8B8A8_UNORM_SRGB : rhi::Format::R8G8B8A8_UNORM;
  desc.usage = rhi::TextureUsage::ShaderResource;
  desc.debugName = image->name ? image->name : "gltf_tex";
  return Texture::create(device, desc, img.pixels.data(), img.width * 4);
}

std::shared_ptr<Material> loadMaterial(rhi::Device& device, const cgltf_material& mat, const std::string& baseDir) {
  auto m = std::make_shared<Material>();
  m->name = mat.name ? mat.name : "material";
  if (mat.has_pbr_metallic_roughness) {
    const auto& pbr = mat.pbr_metallic_roughness;
    m->baseColorFactor = glm::make_vec4(pbr.base_color_factor);
    m->metallicFactor = pbr.metallic_factor;
    m->roughnessFactor = pbr.roughness_factor;
    m->albedo = loadTexture(device, pbr.base_color_texture, baseDir, true);
    m->metallicRoughness = loadTexture(device, pbr.metallic_roughness_texture, baseDir, false);
  }
  m->normal = loadTexture(device, mat.normal_texture, baseDir, false);
  m->ao = loadTexture(device, mat.occlusion_texture, baseDir, false);
  m->emissive = loadTexture(device, mat.emissive_texture, baseDir, true);
  m->emissiveFactor = glm::make_vec3(mat.emissive_factor);
  m->alphaMask = mat.alpha_mode == cgltf_alpha_mode_mask;
  m->alphaCutoff = mat.alpha_cutoff;
  return m;
}

uint32_t materialIndexOf(const cgltf_data* data, const cgltf_material* mat) {
  if (!mat) {
    return 0;
  }
  for (cgltf_size i = 0; i < data->materials_count; ++i) {
    if (&data->materials[i] == mat) {
      return static_cast<uint32_t>(i);
    }
  }
  return 0;
}

std::shared_ptr<Mesh> loadMesh(rhi::Device& device, const cgltf_data* data, const cgltf_mesh& mesh) {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<SubMesh> submeshes;

  for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
    const cgltf_primitive& prim = mesh.primitives[p];
    const cgltf_accessor* posAcc = nullptr;
    const cgltf_accessor* nrmAcc = nullptr;
    const cgltf_accessor* uvAcc = nullptr;
    const cgltf_accessor* tanAcc = nullptr;

    for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
      const auto& attr = prim.attributes[a];
      if (attr.type == cgltf_attribute_type_position) {
        posAcc = attr.data;
      } else if (attr.type == cgltf_attribute_type_normal) {
        nrmAcc = attr.data;
      } else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) {
        uvAcc = attr.data;
      } else if (attr.type == cgltf_attribute_type_tangent) {
        tanAcc = attr.data;
      }
    }
    if (!posAcc) {
      continue;
    }

    const uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
    SubMesh sub;
    sub.indexOffset = static_cast<uint32_t>(indices.size());
    sub.materialIndex = materialIndexOf(data, prim.material);
    sub.aabbMin = glm::vec3(std::numeric_limits<float>::max());
    sub.aabbMax = glm::vec3(std::numeric_limits<float>::lowest());

    vertices.resize(baseVertex + posAcc->count);
    for (cgltf_size i = 0; i < posAcc->count; ++i) {
      Vertex& v = vertices[baseVertex + i];
      float p[3]{};
      cgltf_accessor_read_float(posAcc, i, p, 3);
      v.position = {p[0], p[1], p[2]};
      sub.aabbMin = glm::min(sub.aabbMin, v.position);
      sub.aabbMax = glm::max(sub.aabbMax, v.position);

      if (nrmAcc) {
        float n[3]{};
        cgltf_accessor_read_float(nrmAcc, i, n, 3);
        v.normal = glm::normalize(glm::vec3(n[0], n[1], n[2]));
      } else {
        v.normal = {0, 1, 0};
      }
      if (uvAcc) {
        float uv[2]{};
        cgltf_accessor_read_float(uvAcc, i, uv, 2);
        v.uv = {uv[0], uv[1]};
      }
      if (tanAcc) {
        float t[4]{};
        cgltf_accessor_read_float(tanAcc, i, t, 4);
        v.tangent = {t[0], t[1], t[2], t[3]};
      } else {
        v.tangent = {1, 0, 0, 1};
      }
      v.color = {1, 1, 1, 1};
    }

    if (prim.indices) {
      for (cgltf_size i = 0; i < prim.indices->count; ++i) {
        indices.push_back(baseVertex + static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, i)));
      }
      sub.indexCount = static_cast<uint32_t>(prim.indices->count);
    } else {
      for (cgltf_size i = 0; i < posAcc->count; ++i) {
        indices.push_back(baseVertex + static_cast<uint32_t>(i));
      }
      sub.indexCount = static_cast<uint32_t>(posAcc->count);
    }
    submeshes.push_back(sub);
  }

  if (vertices.empty()) {
    return nullptr;
  }
  return Mesh::create(device, vertices, indices, std::move(submeshes));
}

void processNode(rhi::Device& device, const cgltf_data* data, const cgltf_node* node, const glm::mat4& parent,
                 const std::vector<std::shared_ptr<Material>>& materials,
                 const std::unordered_map<const cgltf_mesh*, std::shared_ptr<Mesh>>& meshes, Scene& scene) {
  const glm::mat4 world = parent * nodeLocalMatrix(node);
  if (node->mesh) {
    auto it = meshes.find(node->mesh);
    if (it != meshes.end() && it->second) {
      RenderObject obj;
      obj.mesh = it->second;
      obj.materials = materials;
      obj.name = node->name ? node->name : "node";
      obj.worldMatrix = world;
      obj.transform.translation = glm::vec3(world[3]);
      scene.objects.push_back(std::move(obj));
    }
  }
  for (cgltf_size i = 0; i < node->children_count; ++i) {
    processNode(device, data, node->children[i], world, materials, meshes, scene);
  }
}

} // namespace

bool loadGLTFScene(rhi::Device& device, const std::string& path, Scene& outScene) {
  cgltf_options options{};
  cgltf_data* data = nullptr;
  cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
  if (result != cgltf_result_success) {
    std::cerr << "cgltf_parse_file failed: " << path << "\n";
    return false;
  }
  result = cgltf_load_buffers(&options, data, path.c_str());
  if (result != cgltf_result_success) {
    cgltf_free(data);
    std::cerr << "cgltf_load_buffers failed\n";
    return false;
  }

  const std::string baseDir = parentPath(path);
  std::vector<std::shared_ptr<Material>> materials;
  materials.reserve(data->materials_count);
  for (cgltf_size i = 0; i < data->materials_count; ++i) {
    materials.push_back(loadMaterial(device, data->materials[i], baseDir));
  }
  if (materials.empty()) {
    materials.push_back(std::make_shared<Material>());
  }

  std::unordered_map<const cgltf_mesh*, std::shared_ptr<Mesh>> meshes;
  for (cgltf_size i = 0; i < data->meshes_count; ++i) {
    meshes[&data->meshes[i]] = loadMesh(device, data, data->meshes[i]);
  }

  // Store world matrices properly: extend RenderObject usage with baked matrix in transform
  const cgltf_scene* sc = data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
  if (sc) {
    for (cgltf_size i = 0; i < sc->nodes_count; ++i) {
      processNode(device, data, sc->nodes[i], glm::mat4(1.0f), materials, meshes, outScene);
    }
  }

  // Re-walk to assign exact world matrices via a second pass storing in object order
  // Simpler fix: bake world into mesh vertices is expensive; instead add worldMatrix to RenderObject
  cgltf_free(data);

  std::cout << "Loaded glTF: " << path << " (" << outScene.objects.size() << " objects, " << materials.size()
            << " materials)\n";
  return !outScene.objects.empty();
}

} // namespace tucano
