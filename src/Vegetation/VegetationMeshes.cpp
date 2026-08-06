#include "Vegetation/VegetationMeshes.h"
#include "AssetPipeline/GLTFLoader.h"
#include "Renderer/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
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

namespace {

/// One slice of the atlas being rasterised into.
struct SliceTarget {
	std::vector<uint8_t>& pixels;
	std::vector<float>& depth; // slice-local, reset per view
	uint32_t atlasSize;
	uint32_t x0, y0, w, h;
};

/// Orthographic, depth-tested triangle fill. Deliberately tiny: impostor slices are ~64x64 and a
/// vegetation LOD0 is a few hundred triangles, so a scanline rasteriser costs microseconds and
/// avoids standing up an offscreen render pass just to bake a texture at load time.
void rasterTriangle(SliceTarget& tgt, const glm::vec3 p[3], const glm::vec3& faceNormal,
                    const glm::vec3& baseColor, const glm::vec3& lightDir) {
	const float minXf = std::min({p[0].x, p[1].x, p[2].x});
	const float maxXf = std::max({p[0].x, p[1].x, p[2].x});
	const float minYf = std::min({p[0].y, p[1].y, p[2].y});
	const float maxYf = std::max({p[0].y, p[1].y, p[2].y});

	int minX = std::max(0, int(std::floor(minXf)));
	int maxX = std::min(int(tgt.w) - 1, int(std::ceil(maxXf)));
	int minY = std::max(0, int(std::floor(minYf)));
	int maxY = std::min(int(tgt.h) - 1, int(std::ceil(maxYf)));
	if (minX > maxX || minY > maxY) return;

	const float area = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[2].x - p[0].x) * (p[1].y - p[0].y);
	if (std::abs(area) < 1e-8f) return;
	const float invArea = 1.0f / area;

	// Vegetation cards are double-sided, so shade with the normal flipped toward the viewer
	// rather than back-face culling — otherwise half of every leaf disappears.
	glm::vec3 n = faceNormal;
	if (n.z < 0.0f) n = -n;
	const float wrap = 0.35f; // wrap lighting stands in for the foliage's translucency
	const float ndl = std::clamp((glm::dot(n, -lightDir) + wrap) / (1.0f + wrap), 0.0f, 1.0f);
	const float shade = 0.35f + 0.65f * ndl;
	const glm::vec3 lit = baseColor * shade;

	for (int y = minY; y <= maxY; ++y) {
		for (int x = minX; x <= maxX; ++x) {
			const float px = float(x) + 0.5f;
			const float py = float(y) + 0.5f;
			float w0 = ((p[1].x - p[0].x) * (py - p[0].y) - (px - p[0].x) * (p[1].y - p[0].y)) * invArea;
			float w1 = ((px - p[0].x) * (p[2].y - p[0].y) - (p[2].x - p[0].x) * (py - p[0].y)) * invArea;
			if (w0 < 0.0f || w1 < 0.0f || w0 + w1 > 1.0f) continue;
			const float w2 = 1.0f - w0 - w1;
			const float z = p[0].z * w2 + p[1].z * w1 + p[2].z * w0;

			const size_t di = size_t(y) * tgt.w + size_t(x);
			if (z >= tgt.depth[di]) continue;
			tgt.depth[di] = z;

			const size_t ai =
			    ((size_t(tgt.y0) + size_t(y)) * size_t(tgt.atlasSize) + size_t(tgt.x0) + size_t(x)) * 4;
			tgt.pixels[ai + 0] = uint8_t(std::clamp(lit.r, 0.0f, 1.0f) * 255.0f);
			tgt.pixels[ai + 1] = uint8_t(std::clamp(lit.g, 0.0f, 1.0f) * 255.0f);
			tgt.pixels[ai + 2] = uint8_t(std::clamp(lit.b, 0.0f, 1.0f) * 255.0f);
			tgt.pixels[ai + 3] = 255;
		}
	}
}

/// Widens coverage by one texel so bilinear filtering of the atlas does not eat the silhouette
/// edge, and fills the transparent border with the nearest opaque colour so filtering never
/// bleeds black into the fringe.
void dilateEdges(std::vector<uint8_t>& pixels, uint32_t atlasSize, uint32_t x0, uint32_t y0,
                 uint32_t w, uint32_t h) {
	// Snapshot only the slice. Copying the whole atlas here would move several megabytes per
	// slice, which across every type and view dwarfs the rasterisation itself.
	std::vector<uint8_t> src(size_t(w) * h * 4);
	for (uint32_t y = 0; y < h; ++y) {
		std::memcpy(&src[size_t(y) * w * 4], &pixels[((size_t(y0 + y) * atlasSize) + x0) * 4],
		            size_t(w) * 4);
	}
	auto atlasAt = [&](uint32_t x, uint32_t y) {
		return ((size_t(y0 + y) * atlasSize) + x0 + x) * 4;
	};
	auto srcAt = [&](uint32_t x, uint32_t y) { return (size_t(y) * w + x) * 4; };

	for (uint32_t y = 0; y < h; ++y) {
		for (uint32_t x = 0; x < w; ++x) {
			if (src[srcAt(x, y) + 3] != 0) continue;
			int r = 0, g = 0, b = 0, count = 0;
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					const int nx = int(x) + dx, ny = int(y) + dy;
					if (nx < 0 || ny < 0 || nx >= int(w) || ny >= int(h)) continue;
					const size_t j = srcAt(uint32_t(nx), uint32_t(ny));
					if (src[j + 3] == 0) continue;
					r += src[j + 0];
					g += src[j + 1];
					b += src[j + 2];
					++count;
				}
			}
			if (count == 0) continue;
			const size_t i = atlasAt(x, y);
			pixels[i + 0] = uint8_t(r / count);
			pixels[i + 1] = uint8_t(g / count);
			pixels[i + 2] = uint8_t(b / count);
			pixels[i + 3] = 96; // partial coverage: softens the alpha-tested edge
		}
	}
}

/// Fallback when a type has no CPU geometry: a tapered blade silhouette, which at least reads as
/// vegetation rather than as a rectangle.
void bakeGenericSilhouette(std::vector<uint8_t>& pixels, uint32_t atlasSize, uint32_t x0,
                           uint32_t y0, uint32_t w, uint32_t h, const glm::vec3& color) {
	for (uint32_t y = 0; y < h; ++y) {
		const float v = float(y) / float(h);           // 0 at top
		const float widthAt = 0.15f + 0.35f * v * v;   // narrow tip, wide base
		for (uint32_t x = 0; x < w; ++x) {
			const float u = (float(x) + 0.5f) / float(w) - 0.5f;
			if (std::abs(u) > widthAt) continue;
			const float edge = 1.0f - std::abs(u) / widthAt;
			const float shade = 0.45f + 0.55f * (1.0f - v);
			const size_t i = ((size_t(y0 + y) * atlasSize) + x0 + x) * 4;
			pixels[i + 0] = uint8_t(std::clamp(color.r * shade, 0.0f, 1.0f) * 255.0f);
			pixels[i + 1] = uint8_t(std::clamp(color.g * shade, 0.0f, 1.0f) * 255.0f);
			pixels[i + 2] = uint8_t(std::clamp(color.b * shade, 0.0f, 1.0f) * 255.0f);
			pixels[i + 3] = uint8_t(std::clamp(edge * 4.0f, 0.0f, 1.0f) * 255.0f);
		}
	}
}

} // namespace

ImpostorSource ImpostorSource::fromMesh(const Mesh* mesh, const glm::vec4& color) {
	ImpostorSource src;
	src.baseColor = color;
	if (mesh) {
		src.positions = &mesh->cpuPositions();
		src.indices = &mesh->packedIndices();
	}
	return src;
}

std::vector<uint8_t> bakeImpostorPixels(const std::vector<ImpostorSource>& types,
                                        uint32_t viewsPerType, uint32_t atlasSize) {
	const uint32_t grid = kImpostorGrid; // must match InstanceCloudRender::billboardGrid
	const uint32_t cell = atlasSize / grid;
	const uint32_t views = std::clamp(viewsPerType, 1u, cell);
	const uint32_t sliceW = std::max(1u, cell / views);

	std::vector<uint8_t> pixels(size_t(atlasSize) * atlasSize * 4, 0);
	std::vector<float> depth(size_t(sliceW) * cell);

	// Fixed key light for the bake. Impostors are only used past the LOD2 distance, where a
	// static bake is indistinguishable from re-lighting per frame.
	const glm::vec3 lightDir = glm::normalize(glm::vec3(-0.35f, -0.85f, -0.4f));

	const uint32_t n = std::min(uint32_t(types.size()), grid * grid);
	for (uint32_t t = 0; t < n; ++t) {
		const ImpostorSource& src = types[t];
		const uint32_t ox = (t % grid) * cell;
		const uint32_t oy = (t / grid) * cell;
		const glm::vec3 color(src.baseColor);

		const std::vector<glm::vec3>* positions = src.positions;
		const std::vector<uint32_t>* indices = src.indices;
		const bool haveGeometry =
		    positions && indices && positions->size() >= 3 && indices->size() >= 3;

		if (!haveGeometry) {
			for (uint32_t view = 0; view < views; ++view) {
				bakeGenericSilhouette(pixels, atlasSize, ox + view * sliceW, oy, sliceW, cell, color);
			}
			continue;
		}

		// Fit an orthographic frame around the mesh so it fills the slice at every yaw. The
		// horizontal extent uses the XZ radius, which is yaw-invariant — sizing per view would
		// make the plant appear to breathe as the camera orbits it.
		glm::vec3 lo(std::numeric_limits<float>::max());
		glm::vec3 hi(std::numeric_limits<float>::lowest());
		for (const auto& p : *positions) {
			lo = glm::min(lo, p);
			hi = glm::max(hi, p);
		}
		const glm::vec3 centre((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f);
		const float radiusXZ = std::max(
		    0.001f, 0.5f * std::max(hi.x - lo.x, hi.z - lo.z) * 1.41421356f);
		const float height = std::max(0.001f, hi.y - lo.y);

		// The billboard quad is twice as tall as it is wide (halfW 0.55r, halfH 1.1r in
		// InstanceBillboard.hlsl), so the UV box must be too. Fitting the mesh to the slice's
		// pixel rectangle instead would squash every plant horizontally on screen.
		const float boxHeight = std::max(height, radiusXZ * 4.0f);
		const float boxWidth = boxHeight * 0.5f;

		for (uint32_t view = 0; view < views; ++view) {
			const uint32_t sox = ox + view * sliceW;
			std::fill(depth.begin(), depth.end(), std::numeric_limits<float>::max());
			SliceTarget target{pixels, depth, atlasSize, sox, oy, sliceW, cell};

			const float yaw = (float(view) / float(views)) * 6.2831853f;
			const float cs = std::cos(yaw);
			const float sn = std::sin(yaw);

			auto project = [&](const glm::vec3& world) {
				// Rotate about Y, then look down -Z: x' is the screen axis, z' the depth axis.
				const float lx = world.x - centre.x;
				const float lz = world.z - centre.z;
				const float rx = lx * cs - lz * sn;
				const float rz = lx * sn + lz * cs;
				// Anchored at the base: the quad grows upward from the instance origin and its
				// v runs 1 at the bottom to 0 at the top.
				const float up = (world.y - lo.y) / boxHeight;
				return glm::vec3((rx / boxWidth + 0.5f) * float(sliceW),
				                 (1.0f - up) * float(cell), rz);
			};

			for (size_t i = 0; i + 2 < indices->size(); i += 3) {
				const uint32_t i0 = (*indices)[i + 0];
				const uint32_t i1 = (*indices)[i + 1];
				const uint32_t i2 = (*indices)[i + 2];
				if (i0 >= positions->size() || i1 >= positions->size() || i2 >= positions->size())
					continue;

				const glm::vec3 w0 = (*positions)[i0];
				const glm::vec3 w1 = (*positions)[i1];
				const glm::vec3 w2 = (*positions)[i2];
				glm::vec3 faceN = glm::cross(w1 - w0, w2 - w0);
				const float len = glm::length(faceN);
				faceN = len > 1e-8f ? faceN / len : glm::vec3(0, 1, 0);
				// Face normal in view space, so the shading term tracks the yaw.
				const glm::vec3 viewN(faceN.x * cs - faceN.z * sn, faceN.y,
				                      faceN.x * sn + faceN.z * cs);

				const glm::vec3 tri[3] = {project(w0), project(w1), project(w2)};
				rasterTriangle(target, tri, viewN, color, lightDir);
			}

			dilateEdges(pixels, atlasSize, sox, oy, sliceW, cell);
		}
	}

	return pixels;
}

std::shared_ptr<rhi::Texture> bakeImpostorAtlas(rhi::Device& device,
                                                const std::vector<ImpostorSource>& types,
                                                uint32_t viewsPerType, uint32_t atlasSize) {
	const std::vector<uint8_t> pixels = bakeImpostorPixels(types, viewsPerType, atlasSize);
	if (pixels.empty()) return nullptr;

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
