// Gate for the source-model -> .tuasset -> geometry pipeline.
//
// Runs headless (no device), so it checks everything up to the GPU upload: the importer writes
// files, the reader accepts them (magic + version + CRC), the chunk table offsets land inside the
// file, and the payload decodes back to the geometry that went in.
//
//   TucanoAssetTest [<model> ...]
//
// With no arguments it synthesises a cube and round-trips that, so the gate runs on any machine
// regardless of which sample assets are checked out.

#include "AssetPipeline/AssetImport.h"
#include "AssetPipeline/GLTFLoader.h"
#include "Renderer/Scene.h"
#include "RHI/RHI.h"
#include "AssetPipeline/DracoMesh.h"
#include "AssetPipeline/TucanoAsset.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace tucano;
using namespace tucano::asset;

namespace {

int g_failures = 0;

void check(bool cond, const std::string& what) {
	std::cout << (cond ? "  [ok]   " : "  [FAIL] ") << what << "\n";
	if (!cond) ++g_failures;
}

/// Reads a cooked mesh asset back to raw arrays — the same work loadTuassetMesh() does before it
/// touches the device, minus the GPU upload.
bool decodeAsset(const std::string& path, uint32_t& outVerts, uint32_t& outIndices,
                 bool& outHasNormals, bool& outHasUVs, std::string& err) {
	TucanoAssetHeader header{};
	std::vector<AssetDependency> deps;
	std::string metadata;
	std::vector<ChunkEntry> chunks;
	if (!TucanoAssetReader::read(path, header, deps, metadata, chunks)) {
		err = "TucanoAssetReader::read rejected the file (magic/version/CRC)";
		return false;
	}

	const std::uintmax_t fileSize = std::filesystem::file_size(path);
	for (const auto& c : chunks) {
		if (c.offset + c.size > fileSize) {
			err = "chunk table points past EOF";
			return false;
		}
	}

	for (const auto& c : chunks) {
		if (c.type != ChunkType::DRCV) continue;
		std::vector<uint8_t> blob;
		if (!TucanoAssetReader::readChunk(path, c, blob)) { err = "DRCV unreadable"; return false; }
		uint64_t dracoSize = 0;
		std::memcpy(&dracoSize, blob.data() + sizeof(MeshAssetData), sizeof(dracoSize));
		MeshDecompressed decoded;
		if (!DracoMesh::decode(blob.data() + sizeof(MeshAssetData) + sizeof(uint64_t),
		                       size_t(dracoSize), decoded)) {
			err = "Draco decode failed";
			return false;
		}
		outVerts = uint32_t(decoded.positions.size() / 3);
		outIndices = uint32_t(decoded.indices.size());
		outHasNormals = !decoded.normals.empty();
		outHasUVs = !decoded.uvs.empty();
		return true;
	}

	for (const auto& c : chunks) {
		if (c.type != ChunkType::Vert) continue;
		std::vector<uint8_t> blob;
		if (!TucanoAssetReader::readChunk(path, c, blob)) { err = "VERT unreadable"; return false; }
		MeshAssetData md{};
		std::memcpy(&md, blob.data(), sizeof(md));
		outVerts = md.vertexCount;
		outIndices = md.indexCount;
		outHasNormals = (header.flags & uint32_t(AssetFlags::HasNormals)) != 0;
		outHasUVs = (header.flags & uint32_t(AssetFlags::HasUVs)) != 0;
		size_t expected = sizeof(MeshAssetData) + size_t(md.vertexCount) * 3 * sizeof(float);
		if (outHasNormals) expected += size_t(md.vertexCount) * 3 * sizeof(float);
		if (outHasUVs) expected += size_t(md.vertexCount) * 2 * sizeof(float);
		if (blob.size() < expected) { err = "VERT payload shorter than its declared attributes"; return false; }
		return true;
	}

	err = "no DRCV or VERT chunk";
	return false;
}

/// Writer/reader round-trip on synthetic data — no importer, no third-party decoder involved.
void testRawRoundTrip(const std::string& outDir) {
	std::cout << "\n== raw writer/reader round-trip ==\n";
	const std::string path = outDir + "/roundtrip.tuasset";

	std::vector<float> verts(3 * 12);
	for (size_t i = 0; i < verts.size(); ++i) verts[i] = float(i) * 0.25f;
	std::vector<uint32_t> idx(18);
	for (size_t i = 0; i < idx.size(); ++i) idx[i] = uint32_t(i) % 12u;

	MeshAssetData md{};
	md.vertexCount = 12;
	md.indexCount = uint32_t(idx.size());
	md.submeshCount = 1;

	{
		TucanoAssetWriter w(AssetType::Mesh, AssetGuid::fromPath("test/roundtrip"), "synthetic");
		w.setMetadata("{\"name\":\"roundtrip\"}");
		std::vector<uint8_t> payload;
		payload.insert(payload.end(), (const uint8_t*)&md, (const uint8_t*)&md + sizeof(md));
		payload.insert(payload.end(), (const uint8_t*)verts.data(),
		               (const uint8_t*)verts.data() + verts.size() * sizeof(float));
		w.addChunk(ChunkType::Vert, payload.data(), uint32_t(payload.size()));
		w.addChunk(ChunkType::Indx, idx.data(), uint32_t(idx.size() * sizeof(uint32_t)));
		check(w.write(path), "writer produced a file");
	}

	TucanoAssetHeader header{};
	std::vector<AssetDependency> deps;
	std::string metadata;
	std::vector<ChunkEntry> chunks;
	const bool read = TucanoAssetReader::read(path, header, deps, metadata, chunks);
	check(read, "reader accepts the file it just wrote (CRC verify)");
	if (!read) return;

	check(metadata == "{\"name\":\"roundtrip\"}", "metadata survives the round-trip");
	check(chunks.size() == 3, "chunk table has META + VERT + INDX (" + std::to_string(chunks.size()) + ")");

	const std::uintmax_t fileSize = std::filesystem::file_size(path);
	bool inBounds = true;
	for (const auto& c : chunks) inBounds = inBounds && (c.offset + c.size <= fileSize);
	check(inBounds, "every chunk lies inside the file");

	bool vertOk = false, indxOk = false;
	for (const auto& c : chunks) {
		std::vector<uint8_t> data;
		if (!TucanoAssetReader::readChunk(path, c, data)) continue;
		if (c.type == ChunkType::Vert && data.size() >= sizeof(md) + sizeof(float) * 2) {
			float f0 = 0, f1 = 0;
			std::memcpy(&f0, data.data() + sizeof(md), sizeof(float));
			std::memcpy(&f1, data.data() + sizeof(md) + sizeof(float), sizeof(float));
			vertOk = (f0 == verts[0] && f1 == verts[1]);
		}
		if (c.type == ChunkType::Indx && data.size() >= sizeof(uint32_t) * 3) {
			uint32_t i0 = 0, i2 = 0;
			std::memcpy(&i0, data.data(), sizeof(uint32_t));
			std::memcpy(&i2, data.data() + 2 * sizeof(uint32_t), sizeof(uint32_t));
			indxOk = (i0 == idx[0] && i2 == idx[2]);
		}
	}
	check(vertOk, "VERT chunk reads back the bytes that were written");
	check(indxOk, "INDX chunk reads back the bytes that were written");
}

/// Draco encode/decode of a unit cube, then the same data through the asset container.
void testSyntheticCube(const std::string& outDir) {
	std::cout << "\n== synthetic cube through Draco + container ==\n";

	std::vector<float> positions;
	std::vector<float> normals;
	std::vector<uint32_t> indices;
	const float s = 0.5f;
	const float corners[8][3] = {{-s,-s,-s},{s,-s,-s},{s,s,-s},{-s,s,-s},
	                             {-s,-s, s},{s,-s, s},{s,s, s},{-s,s, s}};
	const int faces[6][4] = {{0,1,2,3},{5,4,7,6},{4,0,3,7},{1,5,6,2},{3,2,6,7},{4,5,1,0}};
	for (auto& f : faces) {
		const uint32_t base = uint32_t(positions.size() / 3);
		for (int k = 0; k < 4; ++k) {
			positions.insert(positions.end(), {corners[f[k]][0], corners[f[k]][1], corners[f[k]][2]});
			normals.insert(normals.end(), {0.0f, 1.0f, 0.0f});
		}
		indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
	}
	const uint32_t vertexCount = uint32_t(positions.size() / 3);

	std::vector<uint8_t> blob;
	const bool encoded = DracoMesh::encode(positions.data(), vertexCount, normals.data(), nullptr,
	                                       indices.data(), uint32_t(indices.size()), 14, blob);
	check(encoded && !blob.empty(), "Draco encodes the cube");
	if (!encoded) return;

	MeshAssetData md{};
	md.vertexCount = vertexCount;
	md.indexCount = uint32_t(indices.size());
	md.submeshCount = 1;

	const std::string path = outDir + "/cube.tuasset";
	{
		TucanoAssetWriter w(AssetType::Mesh, AssetGuid::fromPath("test/cube"), "synthetic-cube");
		w.setMetadata("{\"name\":\"cube\"}");
		w.addFlags(uint32_t(AssetFlags::DracoEnc));
		std::vector<uint8_t> payload;
		payload.insert(payload.end(), (const uint8_t*)&md, (const uint8_t*)&md + sizeof(md));
		const uint64_t dsz = uint64_t(blob.size());
		payload.insert(payload.end(), (const uint8_t*)&dsz, (const uint8_t*)&dsz + sizeof(dsz));
		payload.insert(payload.end(), blob.begin(), blob.end());
		w.addChunk(ChunkType::DRCV, payload.data(), uint32_t(payload.size()));
		check(w.write(path), "cube asset written");
	}

	uint32_t v = 0, i = 0;
	bool hasN = false, hasUV = false;
	std::string err;
	const bool ok = decodeAsset(path, v, i, hasN, hasUV, err);
	check(ok, ok ? "cube asset decodes" : "cube asset decodes — " + err);
	if (!ok) return;
	check(i == indices.size(),
	      "index count survives: " + std::to_string(i) + " == " + std::to_string(indices.size()));
	check(hasN, "normals survive the Draco round-trip");
}

void testImport(const std::string& model, const std::string& outDir) {
	std::cout << "\n== import " << model << " ==\n";
	if (!std::filesystem::exists(model)) {
		std::cout << "  [skip] file not present\n";
		return;
	}
	check(isImportableModel(model), "extension is recognised as importable");

	const int written = importModelAsTuasset(model, outDir);
	check(written > 0, "importer wrote " + std::to_string(written) + " asset(s)");
	if (written <= 0) return;

	const std::string dir = outDir + "/" + std::filesystem::path(model).stem().string();
	int checked = 0, decoded = 0;
	uint64_t totalVerts = 0, totalIndices = 0;
	std::string firstError;
	for (const auto& entry : std::filesystem::directory_iterator(dir)) {
		if (entry.path().extension() != ".tuasset") continue;
		++checked;
		uint32_t v = 0, i = 0;
		bool hasN = false, hasUV = false;
		std::string err;
		if (decodeAsset(entry.path().string(), v, i, hasN, hasUV, err)) {
			++decoded;
			totalVerts += v;
			totalIndices += i;
		} else if (firstError.empty()) {
			firstError = entry.path().filename().string() + ": " + err;
		}
	}
	check(checked == written, "every reported asset exists on disk");
	check(decoded == checked,
	      decoded == checked
	          ? "all " + std::to_string(decoded) + " asset(s) decode back to geometry"
	          : "all asset(s) decode back to geometry — first failure: " + firstError);
	check(totalIndices >= 3 && totalVerts >= 3,
	      "geometry is non-empty (" + std::to_string(totalVerts) + " verts, " +
	          std::to_string(totalIndices) + " indices)");
}

/// Loads a glTF through the runtime path the editor uses and reports whether its materials came
/// back with textures bound. Needs a device (textures are GPU resources), but no window.
void testGltfMaterials(const std::string& path) {
	std::cout << "\n== glTF materials: " << path << " ==\n";
	if (!std::filesystem::exists(path)) {
		std::cout << "  [skip] file not present\n";
		return;
	}

	std::shared_ptr<tucano::rhi::Device> device;
	try {
		device = tucano::rhi::Device::create(false);
	} catch (const std::exception& ex) {
		std::cout << "  [skip] no device: " << ex.what() << "\n";
		return;
	}
	if (!device) { std::cout << "  [skip] no device\n"; return; }

	tucano::Scene scene;
	if (!tucano::loadGLTFScene(*device, path, scene)) {
		check(false, "loadGLTFScene succeeded");
		return;
	}
	check(!scene.objects.empty(), "scene has objects (" + std::to_string(scene.objects.size()) + ")");

	size_t materials = 0, withAlbedo = 0, withNormal = 0, withOrm = 0;
	for (const auto& obj : scene.objects) {
		for (const auto& m : obj.materials) {
			if (!m) continue;
			++materials;
			if (m->albedo) ++withAlbedo;
			if (m->normal) ++withNormal;
			if (m->metallicRoughness) ++withOrm;
		}
	}
	std::cout << "  materials=" << materials << " albedo=" << withAlbedo << " normal=" << withNormal
	          << " metallicRoughness=" << withOrm << "\n";
	check(materials > 0, "objects carry materials");
	check(withAlbedo > 0, "at least one material has an albedo texture bound");
}

/// Full loop the editor uses: source model -> .tuasset -> Mesh + Material with textures bound.
void testTuassetMaterialRoundTrip(const std::string& model, const std::string& outDir) {
	std::cout << "\n== .tuasset material round-trip: " << model << " ==\n";
	if (!std::filesystem::exists(model)) { std::cout << "  [skip] file not present\n"; return; }

	std::shared_ptr<tucano::rhi::Device> device;
	try { device = tucano::rhi::Device::create(false); } catch (...) {}
	if (!device) { std::cout << "  [skip] no device\n"; return; }

	const int written = importModelAsTuasset(model, outDir);
	check(written > 0, "importer wrote " + std::to_string(written) + " asset(s)");
	if (written <= 0) return;

	const std::string dir = outDir + "/" + std::filesystem::path(model).stem().string();
	int loaded = 0, withMaterial = 0, withAlbedo = 0;
	std::string firstError;
	for (const auto& entry : std::filesystem::directory_iterator(dir)) {
		if (entry.path().extension() != ".tuasset") continue;
		std::string err;
		std::shared_ptr<tucano::Material> mat;
		auto mesh = loadTuassetMesh(*device, entry.path().string(), &err, &mat);
		if (!mesh) { if (firstError.empty()) firstError = err; continue; }
		++loaded;
		if (mat) ++withMaterial;
		if (mat && mat->albedo) ++withAlbedo;
	}
	check(loaded > 0, loaded > 0 ? "assets load back as meshes (" + std::to_string(loaded) + ")"
	                             : "assets load back as meshes — " + firstError);
	check(withMaterial == loaded,
	      "every asset carries a material (" + std::to_string(withMaterial) + "/" +
	          std::to_string(loaded) + ")");
	check(withAlbedo > 0,
	      "materials come back with albedo textures bound (" + std::to_string(withAlbedo) + ")");
}

} // namespace

int main(int argc, char** argv) {
	std::cout << std::unitbuf; // unbuffered: a crash must not swallow the progress so far
	std::cout << "TucanoAssetTest starting\n";
	const std::string outDir = "asset_test_out";
	std::filesystem::remove_all(outDir);
	std::filesystem::create_directories(outDir);

	testRawRoundTrip(outDir);
	testSyntheticCube(outDir);

	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "--gltf-materials" && i + 1 < argc) { testGltfMaterials(argv[++i]); continue; }
		if (arg == "--tuasset-materials" && i + 1 < argc) {
			testTuassetMaterialRoundTrip(argv[++i], outDir);
			continue;
		}
		testImport(arg, outDir);
	}

	std::cout << "\n" << (g_failures == 0 ? "ASSET PIPELINE: PASS" : "ASSET PIPELINE: FAIL")
	          << " (" << g_failures << " failure(s))\n";
	return g_failures == 0 ? 0 : 1;
}
