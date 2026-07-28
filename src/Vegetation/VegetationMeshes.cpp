#include "Vegetation/VegetationMeshes.h"
#include "AssetPipeline/GLTFLoader.h"
#include "Renderer/Scene.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace tucano::veg {
namespace {

void pushQuad(std::vector<Vertex>& verts, std::vector<uint32_t>& indices,
              const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
              const glm::vec3& n, const glm::vec4& color) {
	const uint32_t base = uint32_t(verts.size());
	auto add = [&](const glm::vec3& p, const glm::vec2& uv) {
		Vertex v{};
		v.position = p;
		v.normal = n;
		v.tangent = {1, 0, 0, 1};
		v.uv = uv;
		v.color = color;
		verts.push_back(v);
	};
	add(p0, {0, 1});
	add(p1, {1, 1});
	add(p2, {1, 0});
	add(p3, {0, 0});
	// Tip verts (uv.y=0) get lower alpha so alpha-test softens blade tips.
	verts[base + 2].color.a = 0.15f;
	verts[base + 3].color.a = 0.15f;
	indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

} // namespace

std::shared_ptr<Mesh> makeProceduralGrassMesh(rhi::Device& device, float width, float height) {
	std::vector<Vertex> verts;
	std::vector<uint32_t> indices;
	const float hw = width * 0.5f;
	const glm::vec4 color{0.25f, 0.55f, 0.18f, 1.0f};

	// Card A (facing +Z)
	pushQuad(verts, indices,
	         {-hw, 0, 0}, {hw, 0, 0}, {hw * 0.3f, height, 0}, {-hw * 0.3f, height, 0},
	         {0, 0, 1}, color);
	// Card B (facing +X) — crossed
	pushQuad(verts, indices,
	         {0, 0, -hw}, {0, 0, hw}, {0, height, hw * 0.3f}, {0, height, -hw * 0.3f},
	         {1, 0, 0}, color);

	SubMesh sub{};
	sub.indexCount = uint32_t(indices.size());
	sub.aabbMin = {-hw, 0, -hw};
	sub.aabbMax = {hw, height, hw};
	return Mesh::create(device, verts, indices, {sub});
}

std::shared_ptr<Mesh> makeProceduralPlantMesh(rhi::Device& device, float height) {
	std::vector<Vertex> verts;
	std::vector<uint32_t> indices;
	const float stemW = 0.04f;
	const float leafW = 0.35f;
	const float leafH = height * 0.45f;
	const glm::vec4 stemColor{0.2f, 0.35f, 0.12f, 1.0f};
	const glm::vec4 leafColor{0.3f, 0.6f, 0.2f, 1.0f};

	pushQuad(verts, indices,
	         {-stemW, 0, 0}, {stemW, 0, 0}, {stemW * 0.5f, height, 0}, {-stemW * 0.5f, height, 0},
	         {0, 0, 1}, stemColor);

	for (int i = 0; i < 3; ++i) {
		const float yaw = float(i) * 2.094395f; // 120 deg
		const float cy = std::cos(yaw);
		const float sy = std::sin(yaw);
		const float y0 = height * (0.35f + 0.15f * float(i));
		const glm::vec3 right{cy, 0, sy};
		const glm::vec3 tip = right * leafW + glm::vec3(0, y0 + leafH * 0.15f, 0);
		const glm::vec3 baseL = -right * (leafW * 0.15f) + glm::vec3(0, y0, 0);
		const glm::vec3 baseR = right * (leafW * 0.15f) + glm::vec3(0, y0, 0);
		const glm::vec3 tipL = tip - glm::vec3(0, leafH * 0.05f, 0) - right * 0.02f;
		const glm::vec3 tipR = tip - glm::vec3(0, leafH * 0.05f, 0) + right * 0.02f;
		const glm::vec3 n = glm::normalize(glm::cross(tip - baseL, baseR - baseL));
		pushQuad(verts, indices, baseL, baseR, tipR, tipL, n, leafColor);
	}

	SubMesh sub{};
	sub.indexCount = uint32_t(indices.size());
	sub.aabbMin = {-leafW, 0, -leafW};
	sub.aabbMax = {leafW, height, leafW};
	return Mesh::create(device, verts, indices, {sub});
}

std::shared_ptr<Mesh> loadVegetationMesh(rhi::Device& device, const std::string& path,
                                         std::string* error) {
	if (path.empty()) {
		if (error) *error = "Empty mesh path";
		return nullptr;
	}
	Scene tmp;
	if (!loadGLTFScene(device, path, tmp) || tmp.objects.empty() || !tmp.objects[0].mesh) {
		if (error) *error = "Failed to load GLTF mesh: " + path;
		return nullptr;
	}
	return tmp.objects[0].mesh;
}

std::shared_ptr<Mesh> makeProceduralGrassLOD1Mesh(rhi::Device& device, float width, float height) {
	std::vector<Vertex> verts;
	std::vector<uint32_t> indices;
	const float hw = width * 0.5f;
	const glm::vec4 color{0.22f, 0.5f, 0.16f, 1.0f};
	pushQuad(verts, indices,
	         {-hw, 0, 0}, {hw, 0, 0}, {hw * 0.25f, height, 0}, {-hw * 0.25f, height, 0},
	         {0, 0, 1}, color);
	SubMesh sub{};
	sub.indexCount = uint32_t(indices.size());
	sub.aabbMin = {-hw, 0, -0.01f};
	sub.aabbMax = {hw, height, 0.01f};
	return Mesh::create(device, verts, indices, {sub});
}

std::shared_ptr<Mesh> makeBillboardQuadMesh(rhi::Device& device) {
	std::vector<Vertex> verts;
	std::vector<uint32_t> indices;
	const glm::vec4 color{1, 1, 1, 1};
	pushQuad(verts, indices,
	         {-0.5f, -0.5f, 0}, {0.5f, -0.5f, 0}, {0.5f, 0.5f, 0}, {-0.5f, 0.5f, 0},
	         {0, 0, 1}, color);
	// Full opacity for billboard mesh (silhouette comes from atlas / PS)
	for (auto& v : verts) v.color.a = 1.0f;
	SubMesh sub{};
	sub.indexCount = uint32_t(indices.size());
	sub.aabbMin = {-0.5f, -0.5f, -0.01f};
	sub.aabbMax = {0.5f, 0.5f, 0.01f};
	return Mesh::create(device, verts, indices, {sub});
}

std::shared_ptr<rhi::Texture> bakeImpostorAtlas(rhi::Device& device, uint32_t typeCount,
                                                uint32_t viewsPerType, uint32_t atlasSize) {
	const uint32_t grid = 16;
	const uint32_t cell = atlasSize / grid;
	std::vector<uint8_t> pixels(size_t(atlasSize) * atlasSize * 4, 0);

	auto plot = [&](uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
		if (x >= atlasSize || y >= atlasSize) return;
		size_t i = (size_t(y) * atlasSize + x) * 4;
		pixels[i + 0] = r;
		pixels[i + 1] = g;
		pixels[i + 2] = b;
		pixels[i + 3] = a;
	};

	const uint32_t n = std::min(typeCount, grid * grid);
	const uint32_t views = std::max(1u, viewsPerType);
	for (uint32_t t = 0; t < n; ++t) {
		const uint32_t gx = t % grid;
		const uint32_t gy = t / grid;
		const uint32_t ox = gx * cell;
		const uint32_t oy = gy * cell;
		const bool grass = (t % 2) == 0;
		const uint32_t sliceW = std::max(1u, cell / views);

		// Soft plant/grass silhouette per yaw view (horizontal slices inside the cell).
		for (uint32_t view = 0; view < views; ++view) {
			const uint32_t sox = ox + view * sliceW;
			const float yaw = (float(view) / float(views)) * 6.2831853f;
			const float squash = 0.55f + 0.45f * std::abs(std::cos(yaw));
			const float cx = float(sox) + float(sliceW) * 0.5f;
			const float cy = float(oy) + float(cell) * 0.55f;
			const float rw = float(sliceW) * (grass ? 0.35f : 0.42f) * squash;
			const float rh = float(cell) * (grass ? 0.42f : 0.38f);
			for (uint32_t y = oy; y < oy + cell; ++y) {
				for (uint32_t x = sox; x < sox + sliceW && x < ox + cell; ++x) {
					float dx = (float(x) - cx) / rw;
					float dy = (float(y) - cy) / rh;
					float e = dx * dx + dy * dy;
					if (e > 1.0f) continue;
					float a = std::pow(std::max(0.0f, 1.0f - e), 1.4f);
					uint8_t alpha = uint8_t(std::clamp(a * 255.0f, 0.0f, 255.0f));
					if (alpha < 8) continue;
					if (grass) {
						plot(x, y, 55, 140, 40, alpha);
					} else {
						plot(x, y, 70, 150, 55, alpha);
					}
				}
			}
			if (!grass) {
				for (uint32_t y = oy + cell / 2; y < oy + cell - 2; ++y) {
					for (int dx = -1; dx <= 1; ++dx) {
						plot(uint32_t(int(cx) + dx), y, 45, 80, 30, 220);
					}
				}
			}
		}
	}

	rhi::TextureDesc desc{};
	desc.width = atlasSize;
	desc.height = atlasSize;
	desc.format = rhi::Format::R8G8B8A8_UNORM;
	desc.usage = rhi::TextureUsage::ShaderResource;
	desc.debugName = "VegImpostorAtlas";
	const uint32_t rowPitch = atlasSize * 4;
	return device.createTexture(desc, pixels.data(), rowPitch);
}

} // namespace tucano::veg
