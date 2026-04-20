#include "pencil_preprocess.h"
#include "shapes.h"

#include <glbinding/gl/gl.h>
#include <glbinding/Binding.h>
using namespace gl;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <set>



// Equations 3 and 4 of the paper
//   c_t'  = c_t - mu_b * c_a                          (eq. 3)
//   c_a   = c_t * (1 - c_s)                           (eq. 3)
//   if c_t is white enough, c_a = mu_w * c_a          (eq. 4)
// influence is in [0,1]
inline void DarkenPixel(float& ct, float strokeCol, float mu_b, float mu_w, float whiteThreshold, float influence)
{
    float ca = ct * (1.0f - strokeCol);
    if (ct > whiteThreshold) ca *= mu_w;
    ct -= mu_b * ca * influence;
    if (ct < 0.0f) ct = 0.0f;
}

struct StrokeParams {
    float length;         // in pixels along stroke direction
    float thickness;      // in pixels perpendicular to stroke
    float strokeCol;      // c_s in the paper (0 = fully dark stroke)
    float mu_b;           // how much darkness a single pass adds
    float mu_w;           // how strongly white pixels resist darkening
    float whiteThreshold; // what counts as white enough
    float angleJitter;    // +/- radians around nominal angle
};

// Stamp one stroke into image
void StampStroke(std::vector<float>& img, int W, int H, float cx, float cy, float angle, const StrokeParams& p)
{
    const float dx = std::cos(angle), dy = std::sin(angle);
    const float nx = -dy, ny = dx; // perpendicular axis
    const float halfLen = p.length * 0.5f;
    const float halfThick = p.thickness * 0.5f;
    const int sweep = (int)std::ceil(std::max(halfLen, halfThick)) + 1;

    for (int oy = -sweep; oy <= sweep; ++oy) {
        for (int ox = -sweep; ox <= sweep; ++ox) {
            float px = (float)ox, py = (float)oy;
            float u = px * dx + py * dy;  // along stroke
            float v = px * nx + py * ny;  // across stroke
            if (std::fabs(u) > halfLen)   continue;
            if (std::fabs(v) > halfThick) continue;

            float tAcross = std::fabs(v) / halfThick;
            float tAlong = std::fabs(u) / halfLen;
            float falloff = (1.0f - tAcross * tAcross) * (1.0f - tAlong);
            // grain is noise keyed on position
            float grain = 0.8f + 0.2f * std::fabs(std::sin(12.9898f * (cx + u) + 78.233f * (cy + v)) * 43758.5453f
                - std::floor(std::sin(12.9898f * (cx + u) + 78.233f * (cy + v)) * 43758.5453f));
            float influence = falloff * grain;
            if (influence <= 0.0f) continue;

            int ix = (int)std::floor(cx + (float)ox);
            int iy = (int)std::floor(cy + (float)oy);
            ix = ((ix % W) + W) % W;
            iy = ((iy % H) + H) % H;

            DarkenPixel(img[iy * W + ix],
                p.strokeCol, p.mu_b, p.mu_w, p.whiteThreshold,
                influence);
        }
    }
}

inline void UploadR8Tex2D(unsigned int& tex, const std::vector<uint8_t>& bytes, int W, int H)
{
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, (int)GL_R8,
        W, H, 0, GL_RED, GL_UNSIGNED_BYTE, bytes.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (int)GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (int)GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (int)GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (int)GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
}



unsigned int CreatePencilTonesTexture(int texSize, int numTones)
{
    const int W = texSize, H = texSize, N = numTones;

    std::vector<float>   work(W * H, 1.0f);
    std::vector<uint8_t> tex3D(W * H * N, 255);

    StrokeParams sp;
    sp.length = texSize * 0.35f;
    sp.thickness = 1.5f;
    sp.strokeCol = 0.0f;
    sp.mu_b = 0.20f;   // recommened 0.1 to 0.3
    sp.mu_w = 0.40f;   // recommenedd 0.3 to 0.5
    sp.whiteThreshold = 0.85f;
    sp.angleJitter = 0.12f;   // 7 degrees

    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);

    auto meanBrightness = [&]() {
        double s = 0.0;
        for (float v : work) s += v;
        return (float)(s / (double)(W * H));
        };

    auto snapshotSlice = [&](int slice) {
        uint8_t* dst = &tex3D[slice * W * H];
        for (int i = 0; i < W * H; ++i) {
            float v = std::clamp(work[i], 0.0f, 1.0f);
            dst[i] = (uint8_t)std::round(v * 255.0f);
        }
        };

    // Slice 0 is pure white
    snapshotSlice(0);

    // Target mean brightness for each slice linearly spaced from 1.0 down to darkestMean
    const float darkestMean = 0.12f;

    int nextSlice = 1;
    float nextTarget = 1.0f - (1.0f / (N - 1)) * (1.0f - darkestMean);

    const int strokesPerBatch = 16;
    const int maxBatches = 200000;
    int batches = 0;

    while (nextSlice < N && batches++ < maxBatches) {
        for (int k = 0; k < strokesPerBatch; ++k) {
            float cx = U(rng) * (float)W;
            float cy = U(rng) * (float)H;
            float angle = (U(rng) - 0.5f) * 2.0f * sp.angleJitter;
            StampStroke(work, W, H, cx, cy, angle, sp);
        }
        float m = meanBrightness();
        while (nextSlice < N && m <= nextTarget) {
            snapshotSlice(nextSlice);
            ++nextSlice;
            if (nextSlice < N) {
                nextTarget = 1.0f
                    - ((float)nextSlice / (float)(N - 1))
                    * (1.0f - darkestMean);
            }
        }
    }
    // If we hit the cap fill remaining slices with the darkest
    for (; nextSlice < N; ++nextSlice) snapshotSlice(nextSlice);

    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_3D, tex);
    glTexImage3D(GL_TEXTURE_3D, 0, (int)GL_R8,
        W, H, N, 0, GL_RED, GL_UNSIGNED_BYTE, tex3D.data());
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, (int)GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, (int)GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, (int)GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, (int)GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, (int)GL_LINEAR);
    glBindTexture(GL_TEXTURE_3D, 0);
    return tex;
}


unsigned int CreatePaperNormalMap(int size)
{
    const int S = size;

    std::mt19937 rng(2024);
    std::uniform_real_distribution<float> D(-1.0f, 1.0f);

    const int G = 64; // lattice size per side
    std::vector<float> lattice((G + 1) * (G + 1));
    for (auto& v : lattice) v = D(rng);
    auto latticeAt = [&](int x, int y) -> float {
        x = ((x % (G + 1)) + (G + 1)) % (G + 1);
        y = ((y % (G + 1)) + (G + 1)) % (G + 1);
        return lattice[y * (G + 1) + x];
        };
    auto valueNoise = [&](float u, float v) {
        float gx = u * (float)G, gy = v * (float)G;
        int x0 = (int)std::floor(gx), y0 = (int)std::floor(gy);
        float fx = gx - (float)x0, fy = gy - (float)y0;
        // smoothstep interpolation
        fx = fx * fx * (3.0f - 2.0f * fx);
        fy = fy * fy * (3.0f - 2.0f * fy);
        float v00 = latticeAt(x0, y0);
        float v10 = latticeAt(x0 + 1, y0);
        float v01 = latticeAt(x0, y0 + 1);
        float v11 = latticeAt(x0 + 1, y0 + 1);
        return (v00 * (1.0f - fx) + v10 * fx) * (1.0f - fy)
            + (v01 * (1.0f - fx) + v11 * fx) * fy;
        };

    std::vector<float> height(S * S);
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            float u = (float)x / (float)S;
            float v = (float)y / (float)S;
            float h = 0.0f, amp = 0.5f, freq = 1.0f;
            for (int oct = 0; oct < 4; ++oct) {
                h += amp * valueNoise(u * freq, v * freq);
                amp *= 0.5f;
                freq *= 2.0f;
            }
            height[y * S + x] = h;
        }
    }

    // strength controls perceived tooth
    const float strength = 4.0f;
    std::vector<uint8_t> nmap(S * S * 3);
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            int xm = (x - 1 + S) % S, xp = (x + 1) % S;
            int ym = (y - 1 + S) % S, yp = (y + 1) % S;
            float hL = height[y * S + xm];
            float hR = height[y * S + xp];
            float hD = height[ym * S + x];
            float hU = height[yp * S + x];

            glm::vec3 n((hL - hR) * strength,
                (hD - hU) * strength,
                1.0f);
            n = glm::normalize(n);

            int p = (y * S + x) * 3;
            nmap[p + 0] = (uint8_t)std::round((n.x * 0.5f + 0.5f) * 255.0f);
            nmap[p + 1] = (uint8_t)std::round((n.y * 0.5f + 0.5f) * 255.0f);
            nmap[p + 2] = (uint8_t)std::round((n.z * 0.5f + 0.5f) * 255.0f);
        }
    }

    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, (int)GL_RGB8,
        S, S, 0, GL_RGB, GL_UNSIGNED_BYTE, nmap.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (int)GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (int)GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (int)GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (int)GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}


unsigned int CreateContourPencilTex(int width, int height)
{
    const int W = width, H = height;
    std::vector<float> img(W * H, 1.0f);

    StrokeParams sp;
    sp.length = W * 0.55f;
    sp.thickness = 1.0f;
    sp.strokeCol = 0.0f;
    sp.mu_b = 0.30f;
    sp.mu_w = 0.50f;
    sp.whiteThreshold = 0.85f;
    sp.angleJitter = 0.10f;

    std::mt19937 rng(31415);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);

    const int numStrokes = 160;
    for (int i = 0; i < numStrokes; ++i) {
        float cx = U(rng) * (float)W;
        float cy = U(rng) * (float)H;
        float angle = (U(rng) - 0.5f) * 2.0f * sp.angleJitter;
        StampStroke(img, W, H, cx, cy, angle, sp);
    }

    std::vector<uint8_t> bytes(W * H);
    for (int i = 0; i < W * H; ++i) {
        float v = std::clamp(img[i], 0.0f, 1.0f);
        bytes[i] = (uint8_t)std::round(v * 255.0f);
    }

    unsigned int tex = 0;
    UploadR8Tex2D(tex, bytes, W, H);
    return tex;
}


std::vector<glm::vec3> ComputeMinCurvatureDirs(
    const std::vector<glm::vec3>& positions,
    const std::vector<glm::vec3>& normals,
    const std::vector<unsigned int>& indices)
{
    const size_t numVerts = positions.size();
    std::vector<glm::vec3> out(numVerts, glm::vec3(1.0f, 0.0f, 0.0f));

    // Build 1-ring neighborhood from triangle index list
    std::vector<std::set<unsigned int>> adj(numVerts);
    for (size_t t = 0; t + 2 < indices.size(); t += 3) {
        unsigned int a = indices[t], b = indices[t + 1], c = indices[t + 2];
        if (a < numVerts && b < numVerts && c < numVerts) {
            adj[a].insert(b); adj[a].insert(c);
            adj[b].insert(a); adj[b].insert(c);
            adj[c].insert(a); adj[c].insert(b);
        }
    }

    for (size_t v = 0; v < numVerts; ++v) {
        const glm::vec3& p = positions[v];
        glm::vec3 n = normals[v];
        float nLen = glm::length(n);
        if (nLen < 1e-6f) { out[v] = glm::vec3(1, 0, 0); continue; }
        n /= nLen;

        // accumulate curvature tensor
        glm::mat3 M(0.0f);
        float totalW = 0.0f;

        for (unsigned int vi : adj[v]) {
            glm::vec3 e = positions[vi] - p;
            float len2 = glm::dot(e, e);
            if (len2 < 1e-12f) continue;

            glm::vec3 proj = e - glm::dot(e, n) * n;
            float pl2 = glm::dot(proj, proj);
            if (pl2 < 1e-12f) continue;
            glm::vec3 T = proj / std::sqrt(pl2);

            float kappa = 2.0f * glm::dot(e, n) / len2;
            float w = std::sqrt(len2);
            totalW += w;
            M += w * kappa * glm::outerProduct(T, T);
        }
        if (totalW > 0.0f) M /= totalW;

        // Build orthonormal basis (u, w_axis) for the tangent plane
        glm::vec3 u;
        if (std::fabs(n.x) < 0.707f) u = glm::vec3(1.0f, 0.0f, 0.0f);
        else                         u = glm::vec3(0.0f, 1.0f, 0.0f);
        u = glm::normalize(u - glm::dot(u, n) * n);
        glm::vec3 w_axis = glm::cross(n, u);

        // Project M into that 2x2 tangent basis
        float m11 = glm::dot(u, M * u);
        float m22 = glm::dot(w_axis, M * w_axis);
        float m12 = glm::dot(u, M * w_axis);

        // Eigenvalues of [[m11, m12], [m12, m22]]
        float tr = m11 + m22;
        float det = m11 * m22 - m12 * m12;
        float disc = std::max(0.0f, 0.25f * tr * tr - det);
        float sq = std::sqrt(disc);
        float l1 = 0.5f * tr - sq;
        float l2 = 0.5f * tr + sq;

        // use consistent fallback so the stroke orientation doesn't flicker between frames on flat surface
        const float degenEps = 1e-5f;
        if (std::fabs(l1 - l2) < degenEps) {
            glm::vec3 fb = glm::vec3(1.0f, 0.0f, 0.0f);
            fb = fb - glm::dot(fb, n) * n;
            if (glm::dot(fb, fb) < 1e-8f) {
                fb = glm::vec3(0.0f, 1.0f, 0.0f);
                fb = fb - glm::dot(fb, n) * n;
            }
            out[v] = glm::normalize(fb);
            continue;
        }

        // Pick the eigenvalue with smaller magnitude == direction the surface bends least in
        float lmin = (std::fabs(l1) < std::fabs(l2)) ? l1 : l2;

        // Eigenvector in the 2x2: either (m12, lmin - m11) or
        // (lmin - m22, m12).  Use whichever has the larger norm for
        // numerical stability.
        glm::vec2 cand1(m12, lmin - m11);
        glm::vec2 cand2(lmin - m22, m12);
        glm::vec2 e2 = (glm::dot(cand1, cand1) >= glm::dot(cand2, cand2))
            ? cand1 : cand2;
        float e2len = glm::length(e2);
        if (e2len < 1e-8f) { out[v] = u; continue; }
        e2 /= e2len;

        glm::vec3 dmin = e2.x * u + e2.y * w_axis;
        float dl = glm::length(dmin);
        out[v] = (dl > 1e-8f) ? dmin / dl : u;
    }

    return out;
}



void AttachCurvatureAttribute(Shape* shape)
{
    if (!shape || shape->vaoID == 0) return;
    if (shape->Pnt.empty() || shape->Tri.empty()) return;

    // Shape stores positions as vec4 and normals as vec3
    // ComputeMinCurvatureDirs wants vec3
    std::vector<glm::vec3> positions;
    positions.reserve(shape->Pnt.size());
    for (const glm::vec4& p : shape->Pnt) positions.emplace_back(p);

    std::vector<glm::vec3> normals;
    normals.reserve(shape->Nrm.size());
    for (const glm::vec3& nrm : shape->Nrm) normals.push_back(nrm);
    if (normals.size() != positions.size()) return;

    // flatten
    std::vector<unsigned int> indices;
    indices.reserve(shape->Tri.size() * 3);
    for (const glm::ivec3& t : shape->Tri) {
        indices.push_back((unsigned int)t.x);
        indices.push_back((unsigned int)t.y);
        indices.push_back((unsigned int)t.z);
    }

    std::vector<glm::vec3> curvDirs =
        ComputeMinCurvatureDirs(positions, normals, indices);

    glBindVertexArray(shape->vaoID);

    GLuint curvVBO = 0;
    glGenBuffers(1, &curvVBO);
    glBindBuffer(GL_ARRAY_BUFFER, curvVBO);
    glBufferData(GL_ARRAY_BUFFER,
        (GLsizeiptr)(curvDirs.size() * sizeof(glm::vec3)),
        curvDirs.data(),
        GL_STATIC_DRAW);

    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}