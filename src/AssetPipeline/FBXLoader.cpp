#include "AssetPipeline/FBXLoader.h"
#include "AssetPipeline/DracoMesh.h"
#include "AssetPipeline/TucanoAsset.h"

#include <ofbx.h>

#include <filesystem>
#include <cstring>

namespace tucano {

int importFBXAsTuasset(const std::string& fbxPath, const std::string& outputDir) {
	using namespace asset;

	// Read entire FBX file
	std::ifstream file(fbxPath, std::ios::binary | std::ios::ate);
	if (!file.is_open()) return 0;
	size_t fileSize = file.tellg();
	file.seekg(0);
	std::vector<uint8_t> fbxData(fileSize);
	file.read(reinterpret_cast<char*>(fbxData.data()), fileSize);
	file.close();

	ofbx::LoadFlags flags = ofbx::LoadFlags::TRIANGULATE | ofbx::LoadFlags::IGNORE_EMBEDDED_TEXTURES;
	ofbx::IScene* scene = ofbx::load(fbxData.data(), static_cast<int>(fileSize),
	                                 static_cast<uint64_t>(flags));
	if (!scene) return 0;

	std::string assetName = std::filesystem::path(fbxPath).stem().string();
	std::string assetDir = outputDir + "/" + assetName;
	std::filesystem::create_directories(assetDir);

	int count = 0;

	// Process meshes
	for (int mi = 0; mi < scene->getMeshCount(); ++mi) {
		const ofbx::Mesh* mesh = scene->getMesh(mi);
		if (!mesh) continue;

		const ofbx::Geometry* geom = mesh->getGeometry();
		if (!geom) continue;

		int vertCount = geom->getVertexCount();
		const ofbx::Vec3* verts = geom->getVertices();
		const ofbx::Vec3* norms = geom->getNormals();
		const ofbx::Vec2* uvs = geom->getUVs();
		const int* faceIndices = geom->getFaceIndices();
		int indexCount = geom->getIndexCount();

		if (!verts || vertCount == 0) continue;

		std::string meshName = mesh->name ? mesh->name : (assetName + "_mesh" + std::to_string(mi));
		AssetGuid guid = AssetGuid::fromPath(fbxPath + "/mesh/" + meshName);

		// Convert to flat arrays
		std::vector<float> posData(vertCount * 3);
		for (int i = 0; i < vertCount; ++i) {
			posData[i * 3 + 0] = static_cast<float>(verts[i].x);
			posData[i * 3 + 1] = static_cast<float>(verts[i].y);
			posData[i * 3 + 2] = static_cast<float>(verts[i].z);
		}

		std::vector<float> nrmData;
		if (norms) {
			nrmData.resize(vertCount * 3);
			for (int i = 0; i < vertCount; ++i) {
				nrmData[i * 3 + 0] = static_cast<float>(norms[i].x);
				nrmData[i * 3 + 1] = static_cast<float>(norms[i].y);
				nrmData[i * 3 + 2] = static_cast<float>(norms[i].z);
			}
		}

		std::vector<float> uvData;
		if (uvs) {
			uvData.resize(vertCount * 2);
			for (int i = 0; i < vertCount; ++i) {
				uvData[i * 2 + 0] = static_cast<float>(uvs[i].x);
				uvData[i * 2 + 1] = static_cast<float>(1.0f - uvs[i].y);
			}
		}

		std::vector<uint32_t> idxData;
		if (faceIndices && indexCount > 0) {
			idxData.resize(indexCount);
			for (int i = 0; i < indexCount; ++i)
				idxData[i] = static_cast<uint32_t>(faceIndices[i] >= 0 ? faceIndices[i] : 0);
		}

		// Draco compress
		TucanoAssetWriter writer(AssetType::Mesh, guid, fbxPath);
		writer.setMetadata("{\"name\":\"" + meshName + "\",\"vertexCount\":" + std::to_string(vertCount) + "}");

		// Write mesh metadata + Draco data
		MeshAssetData meshMeta{};
		meshMeta.vertexCount = uint32_t(vertCount);
		meshMeta.indexCount = uint32_t(indexCount);
		meshMeta.submeshCount = 1;

		std::vector<uint8_t> dracoBlob;
		DracoMesh::encode(posData.data(), vertCount,
		                  nrmData.empty() ? nullptr : nrmData.data(),
		                  uvData.empty() ? nullptr : uvData.data(),
		                  idxData.empty() ? nullptr : idxData.data(), uint32_t(idxData.size()),
		                  10, dracoBlob);

		std::vector<uint8_t> payload;
		payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&meshMeta),
		               reinterpret_cast<const uint8_t*>(&meshMeta + 1));
		uint64_t dracoSize = uint64_t(dracoBlob.size());
		payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&dracoSize),
		               reinterpret_cast<const uint8_t*>(&dracoSize + 1));
		payload.insert(payload.end(), dracoBlob.begin(), dracoBlob.end());

		writer.addChunk(ChunkType::DRCV, metaBytes.data(), uint32_t(metaBytes.size()));

		if (writer.write(assetDir + "/" + meshName + ".tuasset")) ++count;
	}

	// Process materials
	for (int mi = 0; mi < scene->getMaterialCount(); ++mi) {
		const ofbx::Material* mat = scene->getMaterial(mi);
		if (!mat) continue;

		std::string matName = mat->name ? mat->name : (assetName + "_mat" + std::to_string(mi));
		AssetGuid guid = AssetGuid::fromPath(fbxPath + "/material/" + matName);

		TucanoAssetWriter writer(AssetType::Material, guid, fbxPath);
		writer.setMetadata("{\"name\":\"" + matName + "\"}");

		MaterialAssetData matData{};
		ofbx::Color diffuse = mat->getDiffuseColor();
		matData.baseColor[0] = diffuse.r;
		matData.baseColor[1] = diffuse.g;
		matData.baseColor[2] = diffuse.b;
		matData.baseColor[3] = 1.0f;
		matData.roughness = 1.0f - mat->getSpecularFactor(); // rough approximation
		matData.metallic = 0.0f;

		writer.addChunk(ChunkType::Matl, &matData, sizeof(matData));
		if (writer.write(assetDir + "/" + matName + ".tuasset")) ++count;
	}

	scene->destroy();
	return count;
}

} // namespace tucano
