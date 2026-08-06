#include "AssetPipeline/FBXLoader.h"
#include "AssetPipeline/DracoMesh.h"
#include "AssetPipeline/TucanoAsset.h"

#include <ofbx.h>

#include <glm/glm.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace tucano {
namespace {

glm::mat4 toGlm(const ofbx::DMatrix& m) {
	glm::mat4 out(1.0f);
	for (int c = 0; c < 4; ++c)
		for (int r = 0; r < 4; ++r) out[c][r] = float(m.m[c * 4 + r]);
	return out;
}

} // namespace

int importFBXAsTuasset(const std::string& fbxPath, const std::string& outputDir) {
	using namespace asset;

	std::ifstream file(fbxPath, std::ios::binary | std::ios::ate);
	if (!file.is_open()) {
		std::cerr << "[Tuasset] cannot open FBX: " << fbxPath << "\n";
		return 0;
	}
	const std::streamoff sz = file.tellg();
	if (sz <= 0) return 0;
	const size_t fileSize = size_t(sz);
	file.seekg(0);
	std::vector<uint8_t> fbxData(fileSize);
	file.read(reinterpret_cast<char*>(fbxData.data()), std::streamsize(fileSize));
	file.close();

	// TRIANGULATE is not a load flag in this OpenFBX revision — polygons are triangulated
	// per-partition below via ofbx::triangulate.
	const ofbx::LoadFlags flags = ofbx::LoadFlags::NONE;
	ofbx::IScene* scene = ofbx::load(fbxData.data(), fileSize, static_cast<uint16_t>(flags));
	if (!scene) {
		std::cerr << "[Tuasset] OpenFBX failed to parse: " << fbxPath << "\n";
		return 0;
	}

	// FBX authors in centimetres by convention (UnitScaleFactor 1 == 1 cm); the rest of the
	// engine and every glTF asset are in metres, so normalise here rather than leaving every
	// imported FBX 100x too large.
	float unitScale = 0.01f;
	if (const auto* settings = scene->getGlobalSettings()) {
		if (settings->UnitScaleFactor > 0.0f) unitScale = settings->UnitScaleFactor * 0.01f;
	}

	const std::string assetName = std::filesystem::path(fbxPath).stem().string();
	const std::string assetDir = outputDir + "/" + assetName;
	std::filesystem::create_directories(assetDir);

	int count = 0;

	for (int mi = 0; mi < scene->getMeshCount(); ++mi) {
		const ofbx::Mesh* mesh = scene->getMesh(mi);
		if (!mesh) continue;

		const ofbx::GeometryData& geomData = mesh->getGeometryData();
		if (!geomData.hasVertices()) continue;

		const ofbx::Vec3Attributes posAttr = geomData.getPositions();
		const ofbx::Vec3Attributes nrmAttr = geomData.getNormals();
		const ofbx::Vec2Attributes uvAttr = geomData.getUVs(0);
		if (!posAttr.values || posAttr.count == 0) continue;

		// Node placement. Without this every mesh landed at the origin in its own local frame,
		// so multi-part FBX models imported as a pile of overlapping pieces.
		const glm::mat4 world = toGlm(mesh->getGlobalTransform()) * toGlm(mesh->getGeometricMatrix());
		const glm::mat3 normalMatrix = glm::mat3(world);

		const std::string meshName =
		    mesh->name && mesh->name[0] ? mesh->name : (assetName + "_mesh" + std::to_string(mi));
		const AssetGuid guid = AssetGuid::fromPath(fbxPath + "/mesh/" + meshName);

		std::vector<float> positions, normals, uvs;
		std::vector<uint32_t> indices;

		// Corners are de-indexed: each triangulated corner becomes its own vertex. Draco
		// re-welds duplicates during encoding, so this costs nothing in the output file.
		std::vector<int> triIndices;
		std::vector<int> scratch;
		for (int p = 0; p < geomData.getPartitionCount(); ++p) {
			const ofbx::GeometryPartition partition = geomData.getPartition(p);
			if (!partition.polygons || partition.polygon_count == 0) continue;
			triIndices.resize(size_t(partition.max_polygon_triangles) * 3 + 3);

			for (int pi = 0; pi < partition.polygon_count; ++pi) {
				const ofbx::GeometryPartition::Polygon& poly = partition.polygons[pi];
				if (poly.vertex_count < 3) continue;
				scratch.resize(size_t(poly.vertex_count));
				const uint32_t produced =
				    ofbx::triangulate(geomData, poly, triIndices.data(), scratch.data());

				for (uint32_t k = 0; k + 2 < produced; k += 3) {
					for (uint32_t e = 0; e < 3; ++e) {
						const int corner = triIndices[k + e];
						if (corner < 0 || corner >= posAttr.count) continue;

						const ofbx::Vec3 p3 = posAttr.get(corner);
						const glm::vec4 wp =
						    world * glm::vec4(float(p3.x), float(p3.y), float(p3.z), 1.0f);
						indices.push_back(uint32_t(positions.size() / 3));
						positions.push_back(wp.x * unitScale);
						positions.push_back(wp.y * unitScale);
						positions.push_back(wp.z * unitScale);

						if (nrmAttr.values && corner < nrmAttr.count) {
							const ofbx::Vec3 n3 = nrmAttr.get(corner);
							glm::vec3 n = normalMatrix * glm::vec3(float(n3.x), float(n3.y), float(n3.z));
							const float len = glm::length(n);
							n = len > 1e-8f ? n / len : glm::vec3(0, 1, 0);
							normals.push_back(n.x);
							normals.push_back(n.y);
							normals.push_back(n.z);
						}
						if (uvAttr.values && corner < uvAttr.count) {
							const ofbx::Vec2 t = uvAttr.get(corner);
							uvs.push_back(float(t.x));
							uvs.push_back(1.0f - float(t.y)); // FBX UV origin is bottom-left
						}
					}
				}
			}
		}

		const uint32_t vertexCount = uint32_t(positions.size() / 3);
		const uint32_t indexCount = uint32_t(indices.size());
		if (vertexCount == 0 || indexCount < 3) {
			std::cerr << "[Tuasset] FBX mesh '" << meshName << "' produced no triangles — skipped\n";
			continue;
		}
		// Attribute arrays must be whole or absent; a partial one would desync the vertex stream.
		if (normals.size() != size_t(vertexCount) * 3) normals.clear();
		if (uvs.size() != size_t(vertexCount) * 2) uvs.clear();

		MeshAssetData md{};
		md.vertexCount = vertexCount;
		md.indexCount = indexCount;
		md.submeshCount = 1;

		TucanoAssetWriter writer(AssetType::Mesh, guid, fbxPath);
		writer.setMetadata("{\"name\":\"" + meshName + "\",\"vertexCount\":" +
		                   std::to_string(vertexCount) + ",\"indexCount\":" +
		                   std::to_string(indexCount) + "}");

		std::vector<uint8_t> dracoBlob;
		const bool dracoOk = DracoMesh::encode(
		    positions.data(), vertexCount, normals.empty() ? nullptr : normals.data(),
		    uvs.empty() ? nullptr : uvs.data(), indices.data(), indexCount, 14, dracoBlob) &&
		    !dracoBlob.empty();

		std::vector<uint8_t> payload;
		payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&md),
		               reinterpret_cast<const uint8_t*>(&md) + sizeof(md));

		if (dracoOk) {
			writer.addFlags(uint32_t(AssetFlags::DracoEnc));
			const uint64_t dracoSz = uint64_t(dracoBlob.size());
			payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&dracoSz),
			               reinterpret_cast<const uint8_t*>(&dracoSz) + sizeof(dracoSz));
			payload.insert(payload.end(), dracoBlob.begin(), dracoBlob.end());
			writer.addChunk(ChunkType::DRCV, payload.data(), uint32_t(payload.size()));
		} else {
			// Previously a Draco failure wrote a header-only .tuasset and still counted as a
			// successful import, so the browser filled with assets that contained no geometry.
			auto appendFloats = [&](const std::vector<float>& v) {
				payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(v.data()),
				               reinterpret_cast<const uint8_t*>(v.data()) + v.size() * sizeof(float));
			};
			appendFloats(positions);
			if (!normals.empty()) { writer.addFlags(uint32_t(AssetFlags::HasNormals)); appendFloats(normals); }
			if (!uvs.empty())     { writer.addFlags(uint32_t(AssetFlags::HasUVs));     appendFloats(uvs); }
			writer.addChunk(ChunkType::Vert, payload.data(), uint32_t(payload.size()));
			writer.addChunk(ChunkType::Indx, indices.data(), indexCount * uint32_t(sizeof(uint32_t)));
		}

		if (writer.write(assetDir + "/" + meshName + ".tuasset")) {
			++count;
		} else {
			std::cerr << "[Tuasset] failed to write " << meshName << ".tuasset\n";
		}
	}

	scene->destroy();
	std::cout << "[Tuasset] FBX import: " << count << " asset(s) from " << fbxPath << "\n";
	return count;
}

} // namespace tucano
