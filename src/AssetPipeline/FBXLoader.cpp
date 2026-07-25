#include "AssetPipeline/FBXLoader.h"
#include "AssetPipeline/DracoMesh.h"
#include "AssetPipeline/TucanoAsset.h"

#include <ofbx.h>

#include <filesystem>
#include <fstream>
#include <vector>
#include <cstring>

namespace tucano {

int importFBXAsTuasset(const std::string& fbxPath, const std::string& outputDir) {
	using namespace asset;

	std::ifstream file(fbxPath, std::ios::binary | std::ios::ate);
	if (!file.is_open()) return 0;
	size_t fileSize = file.tellg();
	file.seekg(0);
	std::vector<uint8_t> fbxData(fileSize);
	file.read(reinterpret_cast<char*>(fbxData.data()), fileSize);
	file.close();

	ofbx::LoadFlags flags = ofbx::LoadFlags::NONE;
	ofbx::IScene* scene = ofbx::load(fbxData.data(), static_cast<int>(fileSize),
	                                 static_cast<uint64_t>(flags));
	if (!scene) return 0;

	std::string assetName = std::filesystem::path(fbxPath).stem().string();
	std::string assetDir = outputDir + "/" + assetName;
	std::filesystem::create_directories(assetDir);

	int count = 0;

	for (int mi = 0; mi < scene->getMeshCount(); ++mi) {
		const ofbx::Mesh* mesh = scene->getMesh(mi);
		if (!mesh) continue;

		const ofbx::GeometryData& geomData = mesh->getGeometryData();
		if (!geomData.hasVertices()) continue;

		ofbx::Vec3Attributes posAttr = geomData.getPositions();
		ofbx::Vec3Attributes nrmAttr = geomData.getNormals();
		ofbx::Vec2Attributes uvAttr = geomData.getUVs(0);

		const int* idxData = posAttr.indices;
		int vertCount = posAttr.count;
		if (!idxData || vertCount == 0) continue;

		std::string meshName = mesh->name ? mesh->name : (assetName + "_mesh" + std::to_string(mi));
		AssetGuid guid = AssetGuid::fromPath(fbxPath + "/mesh/" + meshName);

		std::vector<float> positions(vertCount * 3);
		std::vector<float> normals;
		std::vector<float> uvs;
		std::vector<uint32_t> indices(vertCount);

		for (int i = 0; i < vertCount; ++i) {
			int idx = idxData[i];
			const ofbx::Vec3& p = posAttr.values[idx];
			positions[i * 3 + 0] = static_cast<float>(p.x);
			positions[i * 3 + 1] = static_cast<float>(p.y);
			positions[i * 3 + 2] = static_cast<float>(p.z);
			indices[i] = static_cast<uint32_t>(i);
		}

		if (nrmAttr.values) {
			normals.resize(vertCount * 3);
			for (int i = 0; i < vertCount; ++i) {
				int idx = nrmAttr.indices ? nrmAttr.indices[i] : i;
				const ofbx::Vec3& n = nrmAttr.values[idx];
				normals[i * 3 + 0] = static_cast<float>(n.x);
				normals[i * 3 + 1] = static_cast<float>(n.y);
				normals[i * 3 + 2] = static_cast<float>(n.z);
			}
		}

		if (uvAttr.values) {
			uvs.resize(vertCount * 2);
			for (int i = 0; i < vertCount; ++i) {
				int idx = uvAttr.indices ? uvAttr.indices[i] : i;
				const ofbx::Vec2& u = uvAttr.values[idx];
				uvs[i * 2 + 0] = static_cast<float>(u.x);
				uvs[i * 2 + 1] = 1.0f - static_cast<float>(u.y);
			}
		}

		TucanoAssetWriter writer(AssetType::Mesh, guid, fbxPath);
		writer.setMetadata("{\"name\":\"" + meshName + "\",\"vertexCount\":" + std::to_string(vertCount) + "}");

		std::vector<uint8_t> dracoBlob;
		DracoMesh::encode(positions.data(), uint32_t(vertCount),
		                  normals.empty() ? nullptr : normals.data(),
		                  uvs.empty() ? nullptr : uvs.data(),
		                  indices.data(), uint32_t(indices.size()), 10, dracoBlob);

		if (!dracoBlob.empty()) {
			MeshAssetData meshMeta{};
			meshMeta.vertexCount = uint32_t(vertCount);
			meshMeta.indexCount = uint32_t(indices.size());
			meshMeta.submeshCount = 1;

			std::vector<uint8_t> metaBytes;
			metaBytes.insert(metaBytes.end(), reinterpret_cast<const uint8_t*>(&meshMeta),
			                 reinterpret_cast<const uint8_t*>(&meshMeta + 1));
			uint64_t dracoSz = uint64_t(dracoBlob.size());
			metaBytes.insert(metaBytes.end(), reinterpret_cast<const uint8_t*>(&dracoSz),
			                 reinterpret_cast<const uint8_t*>(&dracoSz + 1));
			metaBytes.insert(metaBytes.end(), dracoBlob.begin(), dracoBlob.end());

			writer.addChunk(ChunkType::DRCV, metaBytes.data(), uint32_t(metaBytes.size()));
		}

		if (writer.write(assetDir + "/" + meshName + ".tuasset")) ++count;
	}

	scene->destroy();
	return count;
}

} // namespace tucano
