#include "AssetPipeline/GLTFLoader.h"
#include "AssetPipeline/ImageLoader.h"
#include "Platform/FileSystem.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <filesystem>
#include "AssetPipeline/TucanoAsset.h"
#include "AssetPipeline/DracoMesh.h"
#include "AssetPipeline/AssetImport.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <limits>
#include <unordered_map>
#include <cstring>

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

/// Decoded images, keyed by source image and colour space. glTF routinely points many materials
/// at the same image — Sponza has ~25 materials over a much smaller set of files — and without
/// this each material view decoded and uploaded its own copy of the same pixels.
using TextureCache = std::unordered_map<const cgltf_image*, std::shared_ptr<Texture>>;

std::shared_ptr<Texture> loadTexture(rhi::Device& device, const cgltf_texture_view& view, const std::string& baseDir,
                                     bool srgb, TextureCache* cacheSrgb = nullptr,
                                     TextureCache* cacheLinear = nullptr) {
  if (!view.texture || !view.texture->image) {
    return nullptr;
  }
  const cgltf_image* image = view.texture->image;

  // Colour space is part of the identity: the same file used as albedo and as a mask needs two
  // different GPU formats, so they cannot share one entry.
  TextureCache* cache = srgb ? cacheSrgb : cacheLinear;
  if (cache) {
    auto it = cache->find(image);
    if (it != cache->end()) return it->second;
  }

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
  auto tex = Texture::create(device, desc, img.pixels.data(), img.width * 4);
  if (cache) (*cache)[image] = tex;
  return tex;
}

std::shared_ptr<Material> loadMaterial(rhi::Device& device, const cgltf_material& mat, const std::string& baseDir,
                                       TextureCache& cacheSrgb, TextureCache& cacheLinear) {
  auto m = std::make_shared<Material>();
  m->name = mat.name ? mat.name : "material";
  if (mat.has_pbr_metallic_roughness) {
    const auto& pbr = mat.pbr_metallic_roughness;
    m->baseColorFactor = glm::make_vec4(pbr.base_color_factor);
    m->metallicFactor = pbr.metallic_factor;
    m->roughnessFactor = pbr.roughness_factor;
    m->albedo = loadTexture(device, pbr.base_color_texture, baseDir, true, &cacheSrgb, &cacheLinear);
    m->metallicRoughness = loadTexture(device, pbr.metallic_roughness_texture, baseDir, false, &cacheSrgb, &cacheLinear);
  }
  m->normal = loadTexture(device, mat.normal_texture, baseDir, false, &cacheSrgb, &cacheLinear);
  m->ao = loadTexture(device, mat.occlusion_texture, baseDir, false, &cacheSrgb, &cacheLinear);
  m->emissive = loadTexture(device, mat.emissive_texture, baseDir, true, &cacheSrgb, &cacheLinear);
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
    const cgltf_accessor* jointAcc = nullptr;   // JOINTS_0
    const cgltf_accessor* weightAcc = nullptr;  // WEIGHTS_0

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
      } else if (attr.type == cgltf_attribute_type_joints && attr.index == 0) {
        jointAcc = attr.data;
      } else if (attr.type == cgltf_attribute_type_weights && attr.index == 0) {
        weightAcc = attr.data;
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

      // Skinning influences. Only the first set (JOINTS_0/WEIGHTS_0) is read: four bones per
      // vertex covers virtually every rig, and more would need a wider vertex format.
      if (jointAcc && weightAcc) {
        cgltf_uint j[4]{};
        float w[4]{};
        cgltf_accessor_read_uint(jointAcc, i, j, 4);
        cgltf_accessor_read_float(weightAcc, i, w, 4);
        const uint8_t idx[4] = {uint8_t(j[0] & 0xFF), uint8_t(j[1] & 0xFF),
                                uint8_t(j[2] & 0xFF), uint8_t(j[3] & 0xFF)};
        v.setSkinning(idx, glm::vec4(w[0], w[1], w[2], w[3]));
      }
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

void setTransformFromWorld(Transform& t, const glm::mat4& world) {
  t.translation = glm::vec3(world[3]);
  glm::vec3 axisX(world[0]);
  glm::vec3 axisY(world[1]);
  glm::vec3 axisZ(world[2]);
  t.scale = {glm::length(axisX), glm::length(axisY), glm::length(axisZ)};
  // A quaternion cannot store a mirror; keep the reflection in scale.x (same as the editor gizmo).
  if (glm::determinant(glm::mat3(world)) < 0.0f) {
    t.scale.x = -t.scale.x;
  }
  constexpr float kMin = 1e-8f;
  axisX = (t.scale.x > kMin || t.scale.x < -kMin) ? axisX / t.scale.x : glm::vec3(1, 0, 0);
  axisY = t.scale.y > kMin ? axisY / t.scale.y : glm::vec3(0, 1, 0);
  axisZ = t.scale.z > kMin ? axisZ / t.scale.z : glm::vec3(0, 0, 1);
  t.rotation = glm::normalize(glm::quat_cast(glm::mat3(axisX, axisY, axisZ)));
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
      // ECS rebuilds worldMatrix from transform every frame. Khronos Sponza is centimetres with
      // node scale 0.008 — if scale stays at the default 1, the camera at (0,2,0) sits 2 cm off
      // the floor and the lighting pass fills the frame with sky.
      setTransformFromWorld(obj.transform, world);
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
  TextureCache cacheSrgb, cacheLinear;
  for (cgltf_size i = 0; i < data->materials_count; ++i) {
    materials.push_back(loadMaterial(device, data->materials[i], baseDir, cacheSrgb, cacheLinear));
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

  glm::vec3 worldMin(std::numeric_limits<float>::max());
  glm::vec3 worldMax(std::numeric_limits<float>::lowest());
  for (const auto& obj : outScene.objects) {
    if (!obj.mesh) {
      continue;
    }
    for (const auto& sub : obj.mesh->submeshes()) {
      const glm::vec3 a = glm::vec3(obj.worldMatrix * glm::vec4(sub.aabbMin, 1.0f));
      const glm::vec3 b = glm::vec3(obj.worldMatrix * glm::vec4(sub.aabbMax, 1.0f));
      worldMin = glm::min(worldMin, glm::min(a, b));
      worldMax = glm::max(worldMax, glm::max(a, b));
    }
  }
  std::cout << "Loaded glTF: " << path << " (" << outScene.objects.size() << " objects, " << materials.size()
            << " materials, " << (cacheSrgb.size() + cacheLinear.size()) << " unique textures)\n";
  if (!outScene.objects.empty()) {
    const glm::vec3& s = outScene.objects.front().transform.scale;
    std::cout << "  world AABB [" << worldMin.x << "," << worldMin.y << "," << worldMin.z << "] .. ["
              << worldMax.x << "," << worldMax.y << "," << worldMax.z << "] scale=(" << s.x << "," << s.y
              << "," << s.z << ")\n";
  }
  return !outScene.objects.empty();
}


int importGLTFAsTuasset(const std::string& path, const std::string& outputDir,
                        const asset::AssetGuid& sourceGuid) {
	using namespace asset;

	cgltf_options options{};
	cgltf_data* data = nullptr;
	if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success) return 0;
	if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success) { cgltf_free(data); return 0; }

	const std::string baseDir = parentPath(path);
	std::string assetName = std::filesystem::path(path).stem().string();
	std::string assetDir = outputDir + "/" + assetName;
	std::filesystem::create_directories(assetDir);
	int count = 0;

	try {
		for (size_t mi = 0; mi < data->meshes_count; ++mi) {
			const auto& mesh = data->meshes[mi];
			for (size_t pi = 0; pi < mesh.primitives_count; ++pi) {
				const auto& prim = mesh.primitives[pi];
				if (!prim.attributes_count) continue;

				std::string meshName = mesh.name ? mesh.name : (assetName + "_mesh" + std::to_string(mi));
				meshName += "_prim" + std::to_string(pi);
				// Derived from the source's identity when the caller knows it, so a rename of the
				// .gltf does not change the ids of what came out of it. The path hash is the
				// fallback for callers with no registry.
				AssetGuid guid = sourceGuid.valid()
				                     ? AssetGuid::forSubAsset(sourceGuid, "mesh/" + meshName)
				                     : AssetGuid::fromPath(path + "/mesh/" + meshName);

				std::vector<float> positions, normals, uvs;
				std::vector<uint32_t> indices;
				uint32_t vertexCount = 0, indexCount = 0;

				for (size_t ai = 0; ai < prim.attributes_count; ++ai) {
					const auto& attr = prim.attributes[ai];
					if (attr.type == cgltf_attribute_type_position) {
						vertexCount = uint32_t(attr.data->count);
						positions.resize(vertexCount * 3);
						for (uint32_t i = 0; i < vertexCount; ++i) { float v[3]; cgltf_accessor_read_float(attr.data, i, v, 3); positions[i*3+0]=v[0]; positions[i*3+1]=v[1]; positions[i*3+2]=v[2]; }
					}
					if (attr.type == cgltf_attribute_type_normal) {
						uint32_t nc = uint32_t(attr.data->count); normals.resize(nc * 3);
						for (uint32_t i = 0; i < nc; ++i) { float v[3]; cgltf_accessor_read_float(attr.data, i, v, 3); normals[i*3+0]=v[0]; normals[i*3+1]=v[1]; normals[i*3+2]=v[2]; }
					}
					if (attr.type == cgltf_attribute_type_texcoord) {
						uint32_t tc = uint32_t(attr.data->count); uvs.resize(tc * 2);
						for (uint32_t i = 0; i < tc; ++i) { float v[2]; cgltf_accessor_read_float(attr.data, i, v, 2); uvs[i*2+0]=v[0]; uvs[i*2+1]=v[1]; }
					}
				}

				if (vertexCount == 0) continue;

				uint32_t nrmCount = normals.empty() ? 0 : uint32_t(normals.size() / 3);
				uint32_t uvCount  = uvs.empty()    ? 0 : uint32_t(uvs.size() / 2);
				if (nrmCount > 0 && nrmCount != vertexCount) {
					std::cerr << "[Tuasset] normal count mismatch for primitive " << pi << " of mesh " << meshName
					          << " (normals=" << nrmCount << " vertices=" << vertexCount << ") — discarding normals\n";
					normals.clear();
				}
				if (uvCount > 0 && uvCount != vertexCount) {
					std::cerr << "[Tuasset] texcoord count mismatch for primitive " << pi << " of mesh " << meshName
					          << " (uvs=" << uvCount << " vertices=" << vertexCount << ") — discarding uvs\n";
					uvs.clear();
				}

				if (prim.indices) {
					indexCount = uint32_t(prim.indices->count);
					indices.resize(indexCount);
					for (uint32_t i = 0; i < indexCount; ++i) indices[i] = uint32_t(cgltf_accessor_read_index(prim.indices, i));
				} else {
					indices.resize(vertexCount);
					for (uint32_t i = 0; i < vertexCount; ++i) indices[i] = i;
					indexCount = vertexCount;
				}

				std::vector<uint8_t> dracoData;
				bool dracoOk = DracoMesh::encode(
					positions.data(), vertexCount,
					normals.empty() ? nullptr : normals.data(),
					uvs.empty() ? nullptr : uvs.data(),
					indices.data(), indexCount, 10, dracoData);

				TucanoAssetWriter writer(AssetType::Mesh, guid, path);
				writer.setMetadata("{\"name\":\"" + meshName + "\",\"vertexCount\":" + std::to_string(vertexCount) + ",\"indexCount\":" + std::to_string(indexCount) + "}");

				MeshAssetData md{};
				md.vertexCount = vertexCount;
				md.indexCount = indexCount;
				md.submeshCount = 1;

				if (dracoOk) {
					writer.addFlags(uint32_t(AssetFlags::DracoEnc));
					std::vector<uint8_t> payload;
					payload.insert(payload.end(), (const uint8_t*)&md, (const uint8_t*)&md + sizeof(md));
					uint64_t dsz = uint64_t(dracoData.size());
					payload.insert(payload.end(), (const uint8_t*)&dsz, (const uint8_t*)&dsz + sizeof(dsz));
					payload.insert(payload.end(), dracoData.begin(), dracoData.end());
					writer.addChunk(ChunkType::DRCV, payload.data(), uint32_t(payload.size()));
				} else {
					// Uncompressed fallback carries normals and UVs too. Writing bare positions
					// dropped both, so anything that fell back here came out unlit and untextured.
					std::vector<uint8_t> payload;
					auto appendFloats = [&](const std::vector<float>& v) {
						payload.insert(payload.end(), (const uint8_t*)v.data(),
						               (const uint8_t*)v.data() + v.size() * sizeof(float));
					};
					payload.insert(payload.end(), (const uint8_t*)&md, (const uint8_t*)&md + sizeof(md));
					appendFloats(positions);
					if (!normals.empty()) { writer.addFlags(uint32_t(AssetFlags::HasNormals)); appendFloats(normals); }
					if (!uvs.empty())     { writer.addFlags(uint32_t(AssetFlags::HasUVs));     appendFloats(uvs); }
					writer.addChunk(ChunkType::Vert, payload.data(), uint32_t(payload.size()));
					writer.addChunk(ChunkType::Indx, indices.data(), indexCount * uint32_t(sizeof(uint32_t)));
				}

				// Material. The format carries no texture pixels yet, so record where the maps
				// live: without this every cooked asset came back white and untextured.
				if (prim.material) {
					const auto& gm = *prim.material;
					auto uriOf = [&](const cgltf_texture_view& view) -> std::string {
						if (!view.texture || !view.texture->image || !view.texture->image->uri) return {};
						return std::filesystem::absolute(joinPath(baseDir, view.texture->image->uri))
						    .lexically_normal()
						    .string();
					};
					glm::vec4 baseColor(1.0f);
					float metallic = 1.0f, roughness = 1.0f;
					std::string albedoPath, ormPath;
					if (gm.has_pbr_metallic_roughness) {
						const auto& pbr = gm.pbr_metallic_roughness;
						baseColor = glm::make_vec4(pbr.base_color_factor);
						metallic = pbr.metallic_factor;
						roughness = pbr.roughness_factor;
						albedoPath = uriOf(pbr.base_color_texture);
						ormPath = uriOf(pbr.metallic_roughness_texture);
					}
					const auto matlChunk = encodeMaterialChunk(
					    baseColor, glm::make_vec3(gm.emissive_factor), metallic, roughness,
					    gm.alpha_cutoff, gm.alpha_mode == cgltf_alpha_mode_mask, albedoPath,
					    uriOf(gm.normal_texture), ormPath, uriOf(gm.emissive_texture));
					writer.addChunk(ChunkType::Matl, matlChunk.data(), uint32_t(matlChunk.size()));
				}

				if (writer.write(assetDir + "/" + meshName + ".tuasset")) ++count;
			}
		}
	} catch (const std::exception& e) {
		std::cerr << "[Tuasset] import exception: " << e.what() << "\n";
	} catch (...) {
		std::cerr << "[Tuasset] import unknown exception\n";
	}

	cgltf_free(data);
	return count;
}

} // namespace tucano
