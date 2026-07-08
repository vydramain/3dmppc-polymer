#include "assets/obj_loader.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace rv_3dmppc {

namespace {

// One "a/b/c" corner reference. 0 means "absent".
struct Corner {
    int v = 0, vt = 0, vn = 0;
    bool operator==(const Corner& o) const { return v == o.v && vt == o.vt && vn == o.vn; }
};

struct CornerHash {
    std::size_t operator()(const Corner& c) const {
        return (std::size_t(c.v) * 73856093) ^ (std::size_t(c.vt) * 19349663) ^
               (std::size_t(c.vn) * 83492791);
    }
};

// Parse "v", "v/vt", "v//vn" or "v/vt/vn". Indices are 1-based in OBJ; negative
// indices count back from the end.
Corner parseCorner(const std::string& tok, int nv, int nvt, int nvn) {
    Corner c;
    int field = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= tok.size(); ++i) {
        if (i == tok.size() || tok[i] == '/') {
            if (i > start) {
                int val = std::stoi(tok.substr(start, i - start));
                if (field == 0) c.v = val < 0 ? nv + 1 + val : val;
                else if (field == 1) c.vt = val < 0 ? nvt + 1 + val : val;
                else if (field == 2) c.vn = val < 0 ? nvn + 1 + val : val;
            }
            ++field;
            start = i + 1;
        }
    }
    return c;
}

}  // namespace

std::optional<Mesh> loadObj(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr, "loadObj: cannot open '%s'\n", path.c_str());
        return std::nullopt;
    }

    std::vector<Vec3> positions;
    std::vector<Vec2> uvs;
    std::vector<Vec3> normals;

    Mesh mesh;
    std::unordered_map<Corner, std::uint32_t, CornerHash> cache;

    auto emit = [&](const Corner& c) -> std::uint32_t {
        if (auto it = cache.find(c); it != cache.end()) return it->second;
        Vertex v;
        if (c.v > 0 && c.v <= static_cast<int>(positions.size())) v.pos = positions[c.v - 1];
        if (c.vt > 0 && c.vt <= static_cast<int>(uvs.size())) v.uv = uvs[c.vt - 1];
        if (c.vn > 0 && c.vn <= static_cast<int>(normals.size())) v.normal = normals[c.vn - 1];
        const auto idx = static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(v);
        cache.emplace(c, idx);
        return idx;
    };

    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string kind;
        ss >> kind;
        if (kind == "v") {
            Vec3 p;
            ss >> p.x >> p.y >> p.z;
            positions.push_back(p);
        } else if (kind == "vt") {
            Vec2 t;
            ss >> t.x >> t.y;
            uvs.push_back(t);
        } else if (kind == "vn") {
            Vec3 n;
            ss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        } else if (kind == "f") {
            std::vector<Corner> face;
            std::string tok;
            while (ss >> tok) {
                face.push_back(parseCorner(tok, static_cast<int>(positions.size()),
                                           static_cast<int>(uvs.size()),
                                           static_cast<int>(normals.size())));
            }
            // Fan triangulation.
            for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                mesh.indices.push_back(emit(face[0]));
                mesh.indices.push_back(emit(face[i]));
                mesh.indices.push_back(emit(face[i + 1]));
            }
        }
    }

    return mesh;
}

}  // namespace rv_3dmppc
