#include "AssetPipeline/DracoMesh.h"

#include <draco/compression/encode.h>
#include <draco/compression/decode.h>
#include <draco/core/encoder_buffer.h>
#include <draco/core/decoder_buffer.h>
#include <draco/mesh/mesh.h>
#include <draco/point_cloud/point_cloud.h>

#include <cstring>

namespace tucano::asset {

bool DracoMesh::encode(
	const float* positions, uint32_t vertexCount,
	const float* normals,
	const float* uvs,
	const uint32_t* indices, uint32_t indexCount,
	int quantizationBits,
	std::vector<uint8_t>& outDracoData)
{
	if (!positions || vertexCount == 0) return false;

	draco::Mesh mesh;
	mesh.set_num_points(vertexCount);

	// Position attribute (required)
	draco::GeometryAttribute posAttr;
	posAttr.Init(draco::GeometryAttribute::POSITION, nullptr, 3, draco::DT_FLOAT32, false,
	             sizeof(float) * 3, 0);
	int posAttId = mesh.AddAttribute(posAttr, false, vertexCount);
	for (uint32_t i = 0; i < vertexCount; ++i)
		mesh.attribute(posAttId)->SetAttributeValue(
			draco::AttributeValueIndex(i), &positions[i * 3]);

	// Normal attribute (optional)
	int nrmAttId = -1;
	if (normals) {
		draco::GeometryAttribute nrmAttr;
		nrmAttr.Init(draco::GeometryAttribute::NORMAL, nullptr, 3, draco::DT_FLOAT32, false,
		             sizeof(float) * 3, 0);
		nrmAttId = mesh.AddAttribute(nrmAttr, false, vertexCount);
		for (uint32_t i = 0; i < vertexCount; ++i)
			mesh.attribute(nrmAttId)->SetAttributeValue(
				draco::AttributeValueIndex(i), &normals[i * 3]);
	}

	// UV attribute (optional)
	int uvAttId = -1;
	if (uvs) {
		draco::GeometryAttribute uvAttr;
		uvAttr.Init(draco::GeometryAttribute::TEX_COORD, nullptr, 2, draco::DT_FLOAT32, false,
		            sizeof(float) * 2, 0);
		uvAttId = mesh.AddAttribute(uvAttr, false, vertexCount);
		for (uint32_t i = 0; i < vertexCount; ++i)
			mesh.attribute(uvAttId)->SetAttributeValue(
				draco::AttributeValueIndex(i), &uvs[i * 2]);
	}

	// Index data (optional — if not present, mesh is point cloud)
	if (indices && indexCount > 0) {
		mesh.SetNumFaces(indexCount / 3);
		for (uint32_t i = 0; i < indexCount / 3; ++i) {
			draco::Mesh::Face face;
			face[0] = indices[i * 3 + 0];
			face[1] = indices[i * 3 + 1];
			face[2] = indices[i * 3 + 2];
			mesh.SetFace(draco::FaceIndex(i), face);
		}
	}

	draco::Encoder encoder;
	encoder.SetSpeedOptions(5, 5); // balanced speed/compression
	encoder.SetAttributeQuantization(draco::GeometryAttribute::POSITION, quantizationBits);
	if (nrmAttId >= 0)
		encoder.SetAttributeQuantization(draco::GeometryAttribute::NORMAL, 8);
	if (uvAttId >= 0)
		encoder.SetAttributeQuantization(draco::GeometryAttribute::TEX_COORD, 10);

	draco::EncoderBuffer buffer;
	if (!encoder.EncodeMeshToBuffer(mesh, &buffer).ok()) return false;

	outDracoData.assign(buffer.data(), buffer.data() + buffer.size());
	return true;
}

bool DracoMesh::decode(
	const uint8_t* dracoData, size_t dracoSize,
	MeshDecompressed& outMesh)
{
	if (!dracoData || dracoSize == 0) return false;

	draco::DecoderBuffer buffer;
	buffer.Init(reinterpret_cast<const char*>(dracoData), dracoSize);

	draco::Decoder decoder;
	auto status = decoder.DecodeMeshFromBuffer(&buffer);
	if (!status.ok()) return false;

	std::unique_ptr<draco::Mesh> mesh = std::move(status).value();
	if (!mesh) return false;

	outMesh.vertexCount = mesh->num_points();
	outMesh.indexCount = mesh->num_faces() * 3;

	// Positions
	const auto* posAttr = mesh->GetNamedAttribute(draco::GeometryAttribute::POSITION);
	if (posAttr) {
		outMesh.positions.resize(outMesh.vertexCount * 3);
		for (uint32_t i = 0; i < outMesh.vertexCount; ++i) {
			auto val = posAttr->GetValue<float, 3>(draco::AttributeValueIndex(i));
			outMesh.positions[i * 3 + 0] = val[0];
			outMesh.positions[i * 3 + 1] = val[1];
			outMesh.positions[i * 3 + 2] = val[2];
		}
	}

	// Normals
	const auto* nrmAttr = mesh->GetNamedAttribute(draco::GeometryAttribute::NORMAL);
	if (nrmAttr) {
		outMesh.normals.resize(outMesh.vertexCount * 3);
		for (uint32_t i = 0; i < outMesh.vertexCount; ++i) {
			auto val = nrmAttr->GetValue<float, 3>(draco::AttributeValueIndex(i));
			outMesh.normals[i * 3 + 0] = val[0];
			outMesh.normals[i * 3 + 1] = val[1];
			outMesh.normals[i * 3 + 2] = val[2];
		}
	}

	// UVs
	const auto* uvAttr = mesh->GetNamedAttribute(draco::GeometryAttribute::TEX_COORD);
	if (uvAttr) {
		outMesh.uvs.resize(outMesh.vertexCount * 2);
		for (uint32_t i = 0; i < outMesh.vertexCount; ++i) {
			auto val = uvAttr->GetValue<float, 2>(draco::AttributeValueIndex(i));
			outMesh.uvs[i * 2 + 0] = val[0];
			outMesh.uvs[i * 2 + 1] = val[1];
		}
	}

	// Indices
	outMesh.indices.resize(outMesh.indexCount);
	for (uint32_t i = 0; i < mesh->num_faces(); ++i) {
		const auto& face = mesh->face(draco::FaceIndex(i));
		outMesh.indices[i * 3 + 0] = face[0].value();
		outMesh.indices[i * 3 + 1] = face[1].value();
		outMesh.indices[i * 3 + 2] = face[2].value();
	}

	return true;
}

} // namespace tucano::asset
