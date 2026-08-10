#include "AssetPipeline/AssetImport.h"

#include "AssetPipeline/DracoMesh.h"
#include "AssetPipeline/FBXLoader.h"
#include "AssetPipeline/GLTFLoader.h"
#include "AssetPipeline/TucanoAsset.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include "AssetPipeline/ImageLoader.h"
#include "Renderer/Texture.h"

#include <filesystem>
#include <iostream>

namespace tucano {
namespace {

std::string lowerExtension(const std::string& path) {
	std::string ext = std::filesystem::path(path).extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(),
	               [](unsigned char c) { return char(std::tolower(c)); });
	return ext;
}

/// Area-weighted vertex normals. glTF/FBX exporters routinely omit normals, and a mesh with a
/// zero normal is invisible under any lighting model, so this is not optional.
void computeNormals(const std::vector<float>& positions, const std::vector<uint32_t>& indices,
                    std::vector<float>& outNormals) {
	const size_t vertexCount = positions.size() / 3;
	outNormals.assign(vertexCount * 3, 0.0f);
	for (size_t t = 0; t + 2 < indices.size(); t += 3) {
		const uint32_t i0 = indices[t + 0], i1 = indices[t + 1], i2 = indices[t + 2];
		if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) continue;
		const glm::vec3 p0(positions[i0 * 3], positions[i0 * 3 + 1], positions[i0 * 3 + 2]);
		const glm::vec3 p1(positions[i1 * 3], positions[i1 * 3 + 1], positions[i1 * 3 + 2]);
		const glm::vec3 p2(positions[i2 * 3], positions[i2 * 3 + 1], positions[i2 * 3 + 2]);
		// Un-normalised cross product weights each face by twice its area.
		const glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
		for (uint32_t idx : {i0, i1, i2}) {
			outNormals[idx * 3 + 0] += n.x;
			outNormals[idx * 3 + 1] += n.y;
			outNormals[idx * 3 + 2] += n.z;
		}
	}
	for (size_t v = 0; v < vertexCount; ++v) {
		glm::vec3 n(outNormals[v * 3], outNormals[v * 3 + 1], outNormals[v * 3 + 2]);
		const float len = glm::length(n);
		n = len > 1e-12f ? n / len : glm::vec3(0, 1, 0);
		outNormals[v * 3 + 0] = n.x;
		outNormals[v * 3 + 1] = n.y;
		outNormals[v * 3 + 2] = n.z;
	}
}

/// Per-vertex tangents from the UV gradient (Lengyel). Falls back to an arbitrary basis on
/// degenerate UVs so normal mapping never reads a zero tangent.
void computeTangents(const std::vector<float>& positions, const std::vector<float>& normals,
                     const std::vector<float>& uvs, const std::vector<uint32_t>& indices,
                     std::vector<glm::vec4>& outTangents) {
	const size_t vertexCount = positions.size() / 3;
	std::vector<glm::vec3> tan(vertexCount, glm::vec3(0.0f));
	const bool haveUVs = uvs.size() >= vertexCount * 2;

	if (haveUVs) {
		for (size_t t = 0; t + 2 < indices.size(); t += 3) {
			const uint32_t i0 = indices[t], i1 = indices[t + 1], i2 = indices[t + 2];
			if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) continue;
			const glm::vec3 p0(positions[i0 * 3], positions[i0 * 3 + 1], positions[i0 * 3 + 2]);
			const glm::vec3 p1(positions[i1 * 3], positions[i1 * 3 + 1], positions[i1 * 3 + 2]);
			const glm::vec3 p2(positions[i2 * 3], positions[i2 * 3 + 1], positions[i2 * 3 + 2]);
			const glm::vec2 w0(uvs[i0 * 2], uvs[i0 * 2 + 1]);
			const glm::vec2 w1(uvs[i1 * 2], uvs[i1 * 2 + 1]);
			const glm::vec2 w2(uvs[i2 * 2], uvs[i2 * 2 + 1]);
			const glm::vec3 e1 = p1 - p0, e2 = p2 - p0;
			const glm::vec2 d1 = w1 - w0, d2 = w2 - w0;
			const float det = d1.x * d2.y - d2.x * d1.y;
			if (std::abs(det) < 1e-12f) continue;
			const glm::vec3 T = (e1 * d2.y - e2 * d1.y) / det;
			tan[i0] += T;
			tan[i1] += T;
			tan[i2] += T;
		}
	}

	outTangents.resize(vertexCount);
	for (size_t v = 0; v < vertexCount; ++v) {
		const glm::vec3 n(normals[v * 3], normals[v * 3 + 1], normals[v * 3 + 2]);
		glm::vec3 t = tan[v];
		if (glm::dot(t, t) < 1e-12f) {
			// No usable UV gradient — any vector orthogonal to n will do.
			const glm::vec3 axis = std::abs(n.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
			t = glm::cross(axis, n);
		}
		t = t - n * glm::dot(n, t); // Gram-Schmidt
		const float len = glm::length(t);
		t = len > 1e-12f ? t / len : glm::vec3(1, 0, 0);
		outTangents[v] = glm::vec4(t, 1.0f);
	}
}

/// Length-prefixed string, so a path containing anything is safe to round-trip.
void writeString(std::vector<uint8_t>& out, const std::string& s) {
	const uint32_t len = uint32_t(s.size());
	out.insert(out.end(), (const uint8_t*)&len, (const uint8_t*)&len + sizeof(len));
	out.insert(out.end(), s.begin(), s.end());
}

bool readString(const std::vector<uint8_t>& in, size_t& cursor, std::string& out) {
	if (cursor + sizeof(uint32_t) > in.size()) return false;
	uint32_t len = 0;
	std::memcpy(&len, in.data() + cursor, sizeof(len));
	cursor += sizeof(len);
	if (cursor + len > in.size()) return false;
	out.assign(reinterpret_cast<const char*>(in.data() + cursor), len);
	cursor += len;
	return true;
}

/// Loads an image off disk into a GPU texture. Returns null for an empty path or a missing file —
/// a texture that moved should cost that map, not the whole material.
std::shared_ptr<Texture> loadTextureFromPath(rhi::Device& device, const std::string& path,
                                             bool srgb) {
	if (path.empty() || !std::filesystem::exists(path)) return nullptr;
	ImageData img = loadImageRGBA8(path);
	if (img.pixels.empty() || img.width == 0 || img.height == 0) return nullptr;

	rhi::TextureDesc desc{};
	desc.width = img.width;
	desc.height = img.height;
	desc.format = srgb ? rhi::Format::R8G8B8A8_UNORM_SRGB : rhi::Format::R8G8B8A8_UNORM;
	desc.usage = rhi::TextureUsage::ShaderResource;
	desc.debugName = "tuasset_tex";
	return Texture::create(device, desc, img.pixels.data(), img.width * 4);
}

} // namespace


std::vector<uint8_t> encodeMaterialChunk(const glm::vec4& baseColor, const glm::vec3& emissive,
                                         float metallic, float roughness, float alphaCutoff,
                                         bool alphaMask, const std::string& albedoPath,
                                         const std::string& normalPath, const std::string& ormPath,
                                         const std::string& emissivePath) {
	asset::MaterialAssetData md{};
	md.baseColor[0] = baseColor.r;
	md.baseColor[1] = baseColor.g;
	md.baseColor[2] = baseColor.b;
	md.baseColor[3] = baseColor.a;
	md.emissive[0] = emissive.r;
	md.emissive[1] = emissive.g;
	md.emissive[2] = emissive.b;
	md.emissive[3] = 0.0f;
	md.metallic = metallic;
	md.roughness = roughness;
	md.alphaCutoff = alphaCutoff;
	md.flags = alphaMask ? 1u : 0u;

	std::vector<uint8_t> out;
	out.insert(out.end(), (const uint8_t*)&md, (const uint8_t*)&md + sizeof(md));
	writeString(out, albedoPath);
	writeString(out, normalPath);
	writeString(out, ormPath);
	writeString(out, emissivePath);
	return out;
}

bool isImportableModel(const std::string& path) {
	const std::string ext = lowerExtension(path);
	return ext == ".gltf" || ext == ".glb" || ext == ".fbx";
}

int importModelAsTuasset(const std::string& path, const std::string& outputDir,
                         const asset::AssetGuid& sourceGuid) {
	const std::string ext = lowerExtension(path);
	if (ext == ".gltf" || ext == ".glb") return importGLTFAsTuasset(path, outputDir, sourceGuid);
	if (ext == ".fbx") return importFBXAsTuasset(path, outputDir, sourceGuid);
	std::cerr << "[Tuasset] unsupported source format '" << ext << "' for " << path << "\n";
	return 0;
}

std::shared_ptr<Mesh> loadTuassetMesh(rhi::Device& device, const std::string& path,
                                      std::string* error, std::shared_ptr<Material>* outMaterial) {
	using namespace asset;
	auto fail = [&](const std::string& msg) -> std::shared_ptr<Mesh> {
		if (error) *error = msg;
		return nullptr;
	};

	TucanoAssetHeader header{};
	std::vector<AssetDependency> deps;
	std::string metadata;
	std::vector<ChunkEntry> chunks;
	if (!TucanoAssetReader::read(path, header, deps, metadata, chunks))
		return fail("not a readable .tuasset (bad magic, version or CRC): " + path);
	if (header.type != AssetType::Mesh)
		return fail("asset is not a mesh: " + path);

	const ChunkEntry* drcv = nullptr;
	const ChunkEntry* vert = nullptr;
	const ChunkEntry* indx = nullptr;
	const ChunkEntry* matl = nullptr;
	for (const auto& c : chunks) {
		if (c.type == ChunkType::DRCV) drcv = &c;
		else if (c.type == ChunkType::Vert) vert = &c;
		else if (c.type == ChunkType::Indx) indx = &c;
		else if (c.type == ChunkType::Matl) matl = &c;
	}

	// Material, when the asset carries one. Without this every cooked asset came back with a
	// default white Material and the model rendered untextured.
	if (outMaterial && matl) {
		std::vector<uint8_t> blob;
		if (TucanoAssetReader::readChunk(path, *matl, blob) && blob.size() >= sizeof(MaterialAssetData)) {
			MaterialAssetData md{};
			std::memcpy(&md, blob.data(), sizeof(md));

			auto mat = std::make_shared<Material>();
			mat->name = std::filesystem::path(path).stem().string();
			mat->baseColorFactor = {md.baseColor[0], md.baseColor[1], md.baseColor[2], md.baseColor[3]};
			mat->emissiveFactor = {md.emissive[0], md.emissive[1], md.emissive[2]};
			mat->metallicFactor = md.metallic;
			mat->roughnessFactor = md.roughness;
			mat->alphaCutoff = md.alphaCutoff;
			mat->alphaMask = (md.flags & 1u) != 0;

			size_t cursor = sizeof(MaterialAssetData);
			std::string albedoPath, normalPath, ormPath, emissivePath;
			readString(blob, cursor, albedoPath);
			readString(blob, cursor, normalPath);
			readString(blob, cursor, ormPath);
			readString(blob, cursor, emissivePath);

			// Only albedo and emissive are colour data; the rest are linear.
			mat->albedo = loadTextureFromPath(device, albedoPath, true);
			mat->normal = loadTextureFromPath(device, normalPath, false);
			mat->metallicRoughness = loadTextureFromPath(device, ormPath, false);
			mat->emissive = loadTextureFromPath(device, emissivePath, true);
			*outMaterial = std::move(mat);
		}
	}

	std::vector<float> positions, normals, uvs;
	std::vector<uint32_t> indices;

	if (drcv) {
		std::vector<uint8_t> blob;
		if (!TucanoAssetReader::readChunk(path, *drcv, blob))
			return fail("DRCV chunk unreadable (truncated file?): " + path);
		if (blob.size() < sizeof(MeshAssetData) + sizeof(uint64_t))
			return fail("DRCV chunk too small: " + path);

		uint64_t dracoSize = 0;
		std::memcpy(&dracoSize, blob.data() + sizeof(MeshAssetData), sizeof(dracoSize));
		const size_t payloadOffset = sizeof(MeshAssetData) + sizeof(uint64_t);
		if (dracoSize == 0 || payloadOffset + dracoSize > blob.size())
			return fail("DRCV payload size out of range: " + path);

		MeshDecompressed decoded;
		if (!DracoMesh::decode(blob.data() + payloadOffset, size_t(dracoSize), decoded))
			return fail("Draco decode failed: " + path);

		positions = std::move(decoded.positions);
		normals = std::move(decoded.normals);
		uvs = std::move(decoded.uvs);
		indices = std::move(decoded.indices);
	} else if (vert) {
		std::vector<uint8_t> blob;
		if (!TucanoAssetReader::readChunk(path, *vert, blob))
			return fail("VERT chunk unreadable: " + path);
		if (blob.size() < sizeof(MeshAssetData)) return fail("VERT chunk too small: " + path);

		MeshAssetData md{};
		std::memcpy(&md, blob.data(), sizeof(md));
		const size_t vc = md.vertexCount;
		if (vc == 0) return fail("VERT chunk declares zero vertices: " + path);

		size_t cursor = sizeof(MeshAssetData);
		auto take = [&](std::vector<float>& dst, size_t floats) -> bool {
			if (cursor + floats * sizeof(float) > blob.size()) return false;
			dst.resize(floats);
			std::memcpy(dst.data(), blob.data() + cursor, floats * sizeof(float));
			cursor += floats * sizeof(float);
			return true;
		};
		if (!take(positions, vc * 3)) return fail("VERT positions truncated: " + path);
		if ((header.flags & uint32_t(AssetFlags::HasNormals)) && !take(normals, vc * 3))
			return fail("VERT normals truncated: " + path);
		if ((header.flags & uint32_t(AssetFlags::HasUVs)) && !take(uvs, vc * 2))
			return fail("VERT uvs truncated: " + path);

		if (indx) {
			std::vector<uint8_t> ib;
			if (!TucanoAssetReader::readChunk(path, *indx, ib))
				return fail("INDX chunk unreadable: " + path);
			indices.resize(ib.size() / sizeof(uint32_t));
			if (!indices.empty()) std::memcpy(indices.data(), ib.data(), indices.size() * sizeof(uint32_t));
		}
	} else {
		return fail("no DRCV or VERT chunk in: " + path);
	}

	const size_t vertexCount = positions.size() / 3;
	if (vertexCount == 0) return fail("decoded mesh has no vertices: " + path);

	if (indices.empty()) {
		indices.resize(vertexCount);
		for (size_t i = 0; i < vertexCount; ++i) indices[i] = uint32_t(i);
	}
	// A single out-of-range index is an immediate GPU fault, so drop the whole degenerate
	// triangle rather than trusting the file.
	indices.erase(std::remove_if(indices.begin(), indices.end(),
	                             [&](uint32_t i) { return i >= vertexCount; }),
	              indices.end());
	indices.resize((indices.size() / 3) * 3);
	if (indices.empty()) return fail("mesh has no valid triangles: " + path);

	if (normals.size() < vertexCount * 3) computeNormals(positions, indices, normals);

	std::vector<glm::vec4> tangents;
	computeTangents(positions, normals, uvs, indices, tangents);

	const bool haveUVs = uvs.size() >= vertexCount * 2;
	std::vector<Vertex> vertices(vertexCount);
	glm::vec3 aabbMin(std::numeric_limits<float>::max());
	glm::vec3 aabbMax(std::numeric_limits<float>::lowest());
	for (size_t v = 0; v < vertexCount; ++v) {
		Vertex& out = vertices[v];
		out.position = {positions[v * 3], positions[v * 3 + 1], positions[v * 3 + 2]};
		out.normal = {normals[v * 3], normals[v * 3 + 1], normals[v * 3 + 2]};
		out.tangent = tangents[v];
		out.uv = haveUVs ? glm::vec2(uvs[v * 2], uvs[v * 2 + 1]) : glm::vec2(0.0f);
		out.color = glm::vec4(1.0f);
		aabbMin = glm::min(aabbMin, out.position);
		aabbMax = glm::max(aabbMax, out.position);
	}

	SubMesh sub{};
	sub.indexOffset = 0;
	sub.indexCount = uint32_t(indices.size());
	sub.materialIndex = 0;
	sub.aabbMin = aabbMin;
	sub.aabbMax = aabbMax;

	return Mesh::create(device, vertices, indices, {sub});
}

} // namespace tucano
