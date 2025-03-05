#include <zeno/zeno.h>
#include <zeno/utils/logger.h>
#include <zeno/types/PrimitiveObject.h>
#include <zeno/utils/fileio.h>
#include <glm/detail/type_half.hpp>
#include <zeno/types/NumericObject.h>
#include <zeno/types/UserData.h>
#include <zeno/extra/GlobalState.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <map>

namespace zeno {
namespace {
struct ReadDanceMesh : INode {
    virtual void apply() override {
        auto prim = std::make_shared<PrimitiveObject>();
        auto path = get_input2<std::string>("path");
        auto reader = BinaryReader(file_get_binary(path));

        reader.seek_from_begin(0x18);
        auto bone_count = reader.read_LE<uint32_t>();
        auto bone_ptr = reader.read_LE<uint32_t>();
        auto bones = std::make_shared<PrimitiveObject>();
        bones->verts.resize(bone_count);
        auto &parent = bones->verts.add_attr<int>("parent");
        auto &childCount = bones->verts.add_attr<int>("childCount");
        auto &c1 = bones->verts.add_attr<vec4f>("c1");
        auto &c2 = bones->verts.add_attr<vec4f>("c2");
        auto &c3 = bones->verts.add_attr<vec4f>("c3");
        auto &c4 = bones->verts.add_attr<vec4f>("c4");

        for (auto i = 0; i < bone_count; i++) {
            auto cur_bone_ptr = bone_ptr + 16 * 11 * i;
            reader.seek_from_begin(16 * 1 + cur_bone_ptr);
            c1[i] = reader.read_LE<vec4f>();
            c2[i] = reader.read_LE<vec4f>();
            c3[i] = reader.read_LE<vec4f>();
            c4[i] = reader.read_LE<vec4f>();
            for (auto j = 0; j < 4; j++) {
                if (glm::abs(c1[i][j]) < 0.00001) {
                    c1[i][j] = 0;
                }
            }
            for (auto j = 0; j < 4; j++) {
                if (glm::abs(c2[i][j]) < 0.00001) {
                    c2[i][j] = 0;
                }
            }
            for (auto j = 0; j < 4; j++) {
                if (glm::abs(c3[i][j]) < 0.00001) {
                    c3[i][j] = 0;
                }
            }
            for (auto j = 0; j < 4; j++) {
                if (glm::abs(c4[i][j]) < 0.00001) {
                    c4[i][j] = 0;
                }
            }

            reader.seek_from_begin(16 * 4 + cur_bone_ptr);
            bones->verts[i] = reader.read_LE<vec3f>();
            reader.seek_from_begin(16 * 8 + cur_bone_ptr);
            reader.seek_from_begin(16 * 9 + cur_bone_ptr);
            reader.seek_from_begin(16 * 10 + 12 + cur_bone_ptr);
            parent[i] = reader.read_LE<int32_t>();
            if (parent[i] != -1) {
                bones->lines.push_back({parent[i], i});
                childCount[parent[i]] += 1;
            }
        }
        set_output("bones", std::move(bones));

        reader.seek_from_begin(0x28);
        auto section_count = reader.read_LE<uint32_t>();
        reader.seek_from_begin(0x34);
        auto section_ptr = reader.read_LE<uint32_t>();

        int vert_total_count = 0;
        int face_total_count = 0;


        for (auto i = 0; i < section_count; i++) {
            reader.seek_from_begin(i * 64 + section_ptr);
            auto vert_offset = reader.read_LE<uint32_t>();
            auto vert_count = reader.read_LE<uint32_t>();
            reader.read_LE<uint8_t>();
            auto fvf_size = reader.read_LE<uint8_t>();
            assert(fvf_size == 68);
            reader.skip(22);

            auto face_offset = reader.read_LE<uint32_t>();
            auto face_count = reader.read_LE<uint32_t>() / 3;

            vert_total_count += vert_count;
            face_total_count += face_count;
        }
        prim->resize(vert_total_count);
        prim->tris.resize(face_total_count);
        auto &uv = prim->verts.add_attr<vec3f>("uv");
        auto &nrm = prim->verts.add_attr<vec3f>("nrm");
        auto &bi = prim->verts.add_attr<vec4i>("bi");
        auto &bw = prim->verts.add_attr<vec4f>("bw");
        auto &id = prim->verts.add_attr<int>("id");

        int vi = 0;
        int fi = 0;
        std::vector<std::set<int>> bone_mapping_section;
        std::vector<int> bone_map_section_to_batch;
        for (auto i = 0; i < section_count; i++) {
            std::set<int> bone_index;
            reader.seek_from_begin(i * 64 + section_ptr);
            auto vert_offset = reader.read_LE<uint32_t>();
            auto vert_count = reader.read_LE<uint32_t>();
            reader.read_LE<uint8_t>();
            auto fvf_size = reader.read_LE<uint8_t>();
            assert(fvf_size == 68);
            reader.skip(22);

            auto face_offset = reader.read_LE<uint32_t>();
            auto face_count = reader.read_LE<uint32_t>() / 3;

            for (auto j = 0; j < face_count; j++) {
                auto face_start = section_ptr + 32 + i * 64 + face_offset + j * 6;
                reader.seek_from_begin(face_start);
                auto f0 = reader.read_LE<uint16_t>() + vi;
                auto f1 = reader.read_LE<uint16_t>() + vi;
                auto f2 = reader.read_LE<uint16_t>() + vi;
                prim->tris[fi] = vec3i(f0, f1, f2);
                fi += 1;
            }

            for (auto j = 0; j < vert_count; j++) {
                auto vert_start = section_ptr + i * 64 + vert_offset + j * fvf_size;
                reader.seek_from_begin(vert_start);
                prim->verts[vi] = reader.read_LE<vec3f>();
                id[vi] = i;

                auto b0 = (int)reader.read_LE<uint8_t>();
                auto b1 = (int)reader.read_LE<uint8_t>();
                auto b2 = (int)reader.read_LE<uint8_t>();
                auto b3 = (int)reader.read_LE<uint8_t>();

                bone_index.insert(b0);
                if (b1 != 0)
                    bone_index.insert(b1);
                if (b2 != 0)
                    bone_index.insert(b2);
                if (b3 != 0)
                    bone_index.insert(b3);

                bi[vi] = vec4i(b0, b1, b2, b3);
                auto w = reader.read_LE<vec3f>();
                bw[vi] = vec4f(1 - w[0] - w[1] - w[2], w[0], w[1], w[2]);

                nrm[vi] = reader.read_LE<vec3f>();
                auto tang = reader.read_LE<vec3f>();
                auto bitang = reader.read_LE<vec3f>();

                auto u = reader.read_LE<int16_t>();
                auto v = reader.read_LE<int16_t>();
                uv[vi] = vec3f(glm::detail::toFloat32(u), 1 - glm::detail::toFloat32(v), 0);

                vi += 1;
            }
            int max_value = *std::max_element(bone_index.begin(), bone_index.end());
            if (max_value == bone_index.size() - 1) {
                bone_mapping_section.emplace_back();
            }
            for (auto bi : bone_index) {
                bone_mapping_section.back().insert(bi);
            }
            bone_map_section_to_batch.push_back(bone_mapping_section.size() - 1);
        }
        reader.seek_from_begin(0x24);
        int bone_mapping_table_ptr = reader.read_LE<int>();
        reader.seek_from_begin(bone_mapping_table_ptr);
        std::vector<std::vector<int>> bone_mapping_table;
        for (auto & s : bone_mapping_section) {
            std::vector<int> temp;
            for (auto & _: s) {
                temp.push_back(reader.read_LE<int16_t>());
            }
            bone_mapping_table.push_back(temp);
        }

        vi = 0;
        for (auto i = 0; i < section_count; i++) {
            reader.seek_from_begin(i * 64 + section_ptr);
            auto vert_offset = reader.read_LE<uint32_t>();
            auto vert_count = reader.read_LE<uint32_t>();
            int batch = bone_map_section_to_batch[i];
            for (auto j = 0; j < vert_count; j++) {
                for (auto &x: bi[vi]) {
                    x = bone_mapping_table[batch][x];
                }
                vi += 1;
            }
        }

        set_output("prim", std::move(prim));
    }
};

ZENDEFNODE(ReadDanceMesh, {
    {
        {"readpath", "path"},
    },
    {
        "prim",
        "bones",
    },
    {},
    {"alembic"},
});
vec4f getQuat(uint8_t data[6]) {
    uint64_t num = 0;
    for (auto i = 5; i >= 0; i--) {
        num <<= 8;
        num += data[i];
    }
    int _type = num & 3;
    num >>= 2;
    float _8 = (float(num & 0x7FFF) - 16383.5f) / 23169.767578125f;
    num >>= 15;
    float _6 = (float(num & 0x7FFF) - 16383.5f) / 23169.767578125f;
    num >>= 15;
    float _7 = (float(num & 0x7FFF) - 16383.5f) / 23169.767578125f;
    float _5 = std::sqrt(1.0f - (_8 * _8 + _6 * _6 + _7 * _7));
    vec4f quat;
    if (_type == 0) {
        quat = {_5, _7, _6, _8};
    }
    else if (_type == 1) {
        quat = {_7, _5, _6, _8};
    }
    else if (_type == 2) {
        quat = {_7, _6, _5, _8};
    }
    else {
        quat = {_7, _6, _8, _5};
    }
    for (auto i = 0; i < 3; i++) {
        if (abs(quat[i]) < 0.0001f) {
            quat[i] = 0;
        }
    }
    return quat;
}
uint32_t align_to(uint32_t v, uint32_t a) {
    return (v + a - 1) / a * a;
}
vec3f read_vec3h(BinaryReader &reader) {
    auto x = glm::detail::toFloat32(reader.read_LE<int16_t>());
    auto y = glm::detail::toFloat32(reader.read_LE<int16_t>());
    auto z = glm::detail::toFloat32(reader.read_LE<int16_t>());
    return {x, y, z};
}
struct ReadDanceAnm : INode {
    std::vector<std::vector<vec3f>> arc_pos;
    std::vector<std::vector<vec4f>> arc_rot;
    std::vector<int> arc_interpolation;
    std::vector<int> arc_type;
    std::vector<int> arc_unknown;
    std::vector<int> arc_addr;
    int max_frame = 0;
    virtual void apply() override {
        if (arc_pos.empty()) {
            auto path = get_input2<std::string>("path");
            auto reader = BinaryReader(file_get_binary(path));

            reader.seek_from_begin(0x04);
            max_frame = reader.read_LE<uint32_t>();

            reader.seek_from_begin(0x20);
            auto bone_count = reader.read_LE<uint32_t>();

            arc_pos.resize(bone_count);
            arc_rot.resize(bone_count);
            arc_interpolation.resize(bone_count);
            arc_type.resize(bone_count);
            arc_unknown.resize(bone_count);
            arc_addr.resize(bone_count);

            reader.skip(8 + 2 * bone_count);
            uint32_t section2 = reader.current();
            reader.seek_from_begin(section2 + 8);
            std::vector<uint32_t> addrs;
            for (auto i = 0; i < (bone_count - 3) * 2; i++) {
                addrs.push_back(reader.read_LE<uint32_t>());
            }
//            zeno::log_info("addr: {}", reader.current());
            for (auto i = 0; i < addrs.size(); i++) {
                auto offset = section2 + addrs[i];
                reader.seek_from_begin(offset);
                auto _type = reader.read_LE<uint16_t>();
                auto _interpolation = reader.read_LE<uint16_t>();
                auto _count = reader.read_LE<uint16_t>();
                auto _bone = reader.read_LE<uint16_t>();
                auto _zero = reader.read_LE<uint32_t>();
                auto _unknown = reader.read_LE<uint32_t>();
                reader.seek_from_begin(align_to(reader.current(), 16));
                if (_type == 28) {
                    if (_interpolation == 0) {
//                    if (false) {
                        std::vector<int> index;
                        for (auto j = 0; j < _count; j++) {
                            index.push_back(reader.read_LE<uint16_t>());
                        }
                        std::vector<vec4f> rots;
                        reader.seek_from_begin(align_to(reader.current(), 16));
                        for (auto j = 0; j < _count; j++) {
                            uint8_t data[6];
                            for (auto k = 0; k < 6; k++) {
                                data[k] = reader.read_LE<uint8_t>();
                            }
                            auto rot = getQuat(data);
                            rots.push_back(rot);
                        }
                        std::map<int, vec4f> mapping;
                        for (auto j = 0; j < index.size(); j++) {
                            mapping[index[j]] = rots[j];
                        }
                        arc_rot[_bone].resize(max_frame + 1, {0, 0, 0, 1});
                        for (auto f = 0; f < arc_rot[_bone].size(); f++) {
                            if (mapping.count(f)) {
                                arc_rot[_bone][f] = mapping[f];
                            }
                            else if (f > 0) {
                                arc_rot[_bone][f] = arc_rot[_bone][f-1];
                            }
                        }
                    }
                    else {
                        for (auto j = 0; j < _count; j++) {
                            uint8_t data[6];
                            for (auto k = 0; k < 6; k++) {
                                data[k] = reader.read_LE<uint8_t>();
                            }
                            arc_rot[_bone].push_back(getQuat(data));
                        }
                    }
                }
                else {
                    arc_interpolation[_bone] = _interpolation;
                    arc_type[_bone] = _type;
                    arc_unknown[_bone] = _unknown;
                    arc_addr[_bone] = offset;
                    if (_interpolation == 0) {
                        std::vector<int> index;
                        for (auto j = 0; j < _count; j++) {
                            index.push_back(reader.read_LE<uint16_t>());
                        }
                        std::vector<vec3f> poss;
                        reader.seek_from_begin(align_to(reader.current(), 16));
                        vec3f base_offset = {};
                        if (_type == 31) {
                            base_offset = reader.read_LE<vec3f>();
                        }

                        for (auto j = 0; j < _count; j++) {
                            vec3f translate;
                            if (_type == 30) {
                                translate = read_vec3h(reader);
                            }
                            else if (_type == 31) {
                                translate = read_vec3h(reader) + base_offset;
                            }
                            else {
                                translate = reader.read_LE<vec3f>();
                            }
                            for (auto k = 0; k < 3; k++) {
                                if (zeno::abs(translate[k]) < 0.001) {
                                    translate[k] = 0;
                                }
                            }
                            poss.push_back(translate);
                        }
                        std::map<int, vec3f> mapping;
                        for (auto j = 0; j < index.size(); j++) {
                            mapping[index[j]] = poss[j];
                        }
                        arc_pos[_bone].resize(max_frame + 1);
                        for (auto f = 0; f < arc_pos[_bone].size(); f++) {
                            if (mapping.count(f)) {
                                arc_pos[_bone][f] = mapping[f];
                            }
                            else if (f > 0) {
                                arc_pos[_bone][f] = arc_pos[_bone][f-1];
                            }
                        }
                    }
                    else {
                        for (auto j = 0; j < _count; j++) {
                            vec3f translate;
                            if (_type == 30) {
                                translate = read_vec3h(reader);
                            }
                            else {
                                translate = reader.read_LE<vec3f>();
                            }
                            for (auto k = 0; k < 3; k++) {
                                if (zeno::abs(translate[k]) < 0.001) {
                                    translate[k] = 0;
                                }
                            }
                            arc_pos[_bone].push_back(translate);
                        }
                    }
                }
            }
        }
        int frame;
        if (has_input("frame")) {
            frame = get_input2<int>("frame");
        } else {
            frame = getGlobalState()->frameid;
        }
        auto anm = std::make_shared<PrimitiveObject>();
        anm->userData().set2("max_frame", max_frame);
        auto bone_count = arc_pos.size();
        anm->verts.resize(bone_count);
        auto &rot = anm->verts.add_attr<vec4f>("rot");
        auto &count = anm->verts.add_attr<int>("count");
        auto &interpolation = anm->verts.add_attr<int>("interpolation");
        auto &type = anm->verts.add_attr<int>("type");
        auto &unknown = anm->verts.add_attr<int>("unknown");
        auto &addr = anm->verts.add_attr<int>("addr");
        for (auto b = 0; b < bone_count; b++) {
            count[b] = arc_pos[b].size();
            if (arc_pos[b].size()) {
                int _frame = zeno::clamp(frame, 0, arc_pos[b].size() - 1);
                anm->verts[b] = arc_pos[b][_frame];
            }
            if (arc_rot[b].size()) {
                int _frame = zeno::clamp(frame, 0, arc_rot[b].size() - 1);
                rot[b] = arc_rot[b][_frame];
            }
            else {
                rot[b] = {0, 0, 0, 1};
            }
            interpolation[b] = arc_interpolation[b];
            type[b] = arc_type[b];
            unknown[b] = arc_unknown[b];
            addr[b] = arc_addr[b];
        }
        set_output("anm", std::move(anm));
    }
};

ZENDEFNODE(ReadDanceAnm, {
    {
        {"readpath", "path"},
        {"frame"},
    },
    {
        "anm",
    },
    {},
    {"alembic"},
});

struct ReadDanceCamera : INode {
    std::vector<vec3f> arc_pos;
    std::vector<vec4f> arc_rot;
    virtual void apply() override {
        if (arc_pos.empty()) {
            auto path = get_input2<std::string>("path");
            auto reader = BinaryReader(file_get_binary(path));

            {
                reader.seek_from_begin(0x38);
                auto _type = reader.read_LE<uint16_t>();
                auto _interpolation = reader.read_LE<uint16_t>();
                auto _count = reader.read_LE<uint16_t>();
                auto _bone = reader.read_LE<uint16_t>();
                auto _zero = reader.read_LE<uint32_t>();
                auto _unknown = reader.read_LE<uint32_t>();
                reader.seek_from_begin(align_to(reader.current(), 16));
                if (_interpolation == 0) {
                    std::vector<int> index;
                    for (auto j = 0; j < _count; j++) {
                        index.push_back(reader.read_LE<uint16_t>());
                    }
                    reader.seek_from_begin(align_to(reader.current(), 16));
                    std::vector<vec4f> rots;
                    for (auto j = 0; j < _count; j++) {
                        rots.push_back(reader.read_LE<vec4f>());
                    }
                    std::map<int, vec4f> mapping;
                    for (auto j = 0; j < index.size(); j++) {
                        mapping[index[j]] = rots[j];
                    }
                    for (auto f = 0; f <= index.back(); f++) {
                        if (mapping.count(f)) {
                            arc_rot.push_back(mapping[f]);
                        }
                        else if (f > 0) {
                            arc_rot.push_back(arc_rot.back());
                        }
                        else {
                            arc_rot.emplace_back(0, 0, 0, 1);
                        }
                    }
                }
                else {
                    for (auto j = 0; j < _count; j++) {
                        arc_rot.push_back(reader.read_LE<vec4f>());
                    }
                }
            }
            {
                reader.seek_from_begin(align_to(reader.current(), 16));
                auto _type = reader.read_LE<uint16_t>();
                auto _interpolation = reader.read_LE<uint16_t>();
                auto _count = reader.read_LE<uint16_t>();
                auto _bone = reader.read_LE<uint16_t>();
                auto _zero = reader.read_LE<uint32_t>();
                auto _unknown = reader.read_LE<uint32_t>();
                reader.seek_from_begin(align_to(reader.current(), 16));
                if (_interpolation == 0) {
                    std::vector<int> index;
                    for (auto j = 0; j < _count; j++) {
                        index.push_back(reader.read_LE<uint16_t>());
                    }
                    std::vector<vec3f> poss;
                    reader.seek_from_begin(align_to(reader.current(), 16));

                    for (auto j = 0; j < _count; j++) {
                        vec3f translate = reader.read_LE<vec3f>();
                        reader.read_LE<float>();
                        for (auto k = 0; k < 3; k++) {
                            if (zeno::abs(translate[k]) < 0.001) {
                                translate[k] = 0;
                            }
                        }
                        poss.push_back(translate);
                    }
                    std::map<int, vec3f> mapping;
                    for (auto j = 0; j < index.size(); j++) {
                        mapping[index[j]] = poss[j];
                    }
                    for (auto f = 0; f <= index.back(); f++) {
                        if (mapping.count(f)) {
                            arc_pos.push_back(mapping[f]);
                        }
                        else if (f > 0) {
                            arc_pos.push_back(arc_pos.back());
                        }
                        else {
                            arc_pos.emplace_back(0, 0, 0);
                        }
                    }
                }
                else {
                    for (auto j = 0; j < _count; j++) {
                        vec3f translate = reader.read_LE<vec3f>();
                        reader.read_LE<float>();
                        for (auto k = 0; k < 3; k++) {
                            if (zeno::abs(translate[k]) < 0.001) {
                                translate[k] = 0;
                            }
                        }
                        arc_pos.push_back(translate);
                    }
                }
            }
        }
        int frame;
        if (has_input("frame")) {
            frame = get_input2<int>("frame");
        } else {
            frame = getGlobalState()->frameid;
        }
        auto trans = std::make_shared<NumericObject>(arc_pos[zeno::clamp(frame, 0, arc_pos.size() - 1)]);
        set_output("trans", std::move(trans));
        auto rot = std::make_shared<NumericObject>(arc_rot[zeno::clamp(frame, 0, arc_rot.size() - 1)]);
        set_output("rot", std::move(rot));
    }
};

ZENDEFNODE(ReadDanceCamera, {
    {
        {"readpath", "path"},
        {"frame"},
    },
    {
        "trans",
        "rot",
    },
    {},
    {"alembic"},
});

struct EvalDance : INode {
    virtual void apply() override {
        auto bones = get_input<PrimitiveObject>("bones");
        auto &parent = bones->verts.attr<int>("parent");
        auto anm = get_input<PrimitiveObject>("anm");
        auto &c1 = bones->verts.attr<vec4f>("c1");
        auto &c2 = bones->verts.attr<vec4f>("c2");
        auto &c3 = bones->verts.attr<vec4f>("c3");
        auto &c4 = bones->verts.attr<vec4f>("c4");


        std::vector<glm::mat4> ms;
        ms.resize(bones->size());
        for (auto i = 0; i < ms.size(); i++) {
            glm::mat4 m;
            m[0] = zeno::vec_to_other<glm::vec4>(c1[i]);
            m[1] = zeno::vec_to_other<glm::vec4>(c2[i]);
            m[2] = zeno::vec_to_other<glm::vec4>(c3[i]);
            m[3] = zeno::vec_to_other<glm::vec4>(c4[i]);

            auto rotation = anm->verts.attr<vec4f>("rot")[i];
            auto trans = anm->verts[i];
            glm::quat myQuat(rotation[3], rotation[0], rotation[1], rotation[2]);
            glm::mat4 matQuat = glm::toMat4(myQuat);
            matQuat = glm::translate(zeno::vec_to_other<glm::vec3>(trans)) * matQuat;

            if (parent[i] >= 0) {
                glm::mat4 _m;
                _m[0] = zeno::vec_to_other<glm::vec4>(c1[parent[i]]);
                _m[1] = zeno::vec_to_other<glm::vec4>(c2[parent[i]]);
                _m[2] = zeno::vec_to_other<glm::vec4>(c3[parent[i]]);
                _m[3] = zeno::vec_to_other<glm::vec4>(c4[parent[i]]);
                glm::mat4 tmp = glm::inverse(glm::inverse(_m) * m);
                matQuat = tmp * matQuat;
            }

            ms[i] = m * matQuat * glm::inverse(m);
            if (parent[i] >= 0) {
                ms[i] = ms[parent[i]] * ms[i];
            }
        }
        for (auto i = 0; i < bones->size(); i++) {
            auto p = bones->verts[i];
            auto np = ms[i] * glm::vec4(p[0], p[1], p[2], 1);
            bones->verts[i] = {np[0], np[1], np[2]};
        }

        set_output("bones", std::move(bones));
        auto prim = get_input<PrimitiveObject>("prim");
        auto &nrm = prim->verts.add_attr<vec3f>("nrm");
        auto &bi = prim->verts.attr<vec4i>("bi");
        auto &bw = prim->verts.attr<vec4f>("bw");
        for (auto i = 0; i < prim->verts.size(); i++) {
            glm::mat4 m(0);
            for (auto j = 0; j < 4; j++) {
                m += ms[bi[i][j]] * bw[i][j];
            }
            auto p = prim->verts[i];
            auto glp = glm::vec4(p[0], p[1], p[2], 1);
            glp = m * glp;
            prim->verts[i] = {glp.x, glp.y, glp.z};
            auto n = nrm[i];
            auto gln = glm::vec4(n[0], n[1], n[2], 0);
            gln = m * gln;
            nrm[i] = {gln.x, gln.y, gln.z};
        }

        set_output("prim", std::move(prim));
    }
};

ZENDEFNODE(EvalDance, {
    {
        {"prim"},
        {"bones"},
        {"anm"},
    },
    {
        "prim",
        "bones",
    },
    {},
    {"alembic"},
});
struct VecRotation : INode {
    virtual void apply() override {
        auto dir = get_input2<vec3f>("dir");
        auto quat = get_input2<vec4f>("quat");
        glm::quat myQuat(quat[3], quat[0], quat[1], quat[2]);
        glm::mat4 matQuat = glm::toMat4(myQuat);
        auto np = matQuat * glm::vec4(dir[0], dir[1], dir[2], 0);
        dir = {np[0], np[1], np[2]};

        set_output("dir", std::make_shared<NumericObject>(dir));
    }
};

ZENDEFNODE(VecRotation, {
    {
        {"vec3f", "dir", "0, 1, 0"},
        {"vec4f", "quat", "0, 0, 0, 1"},
    },
    {
        {"dir"},
    },
    {},
    {"alembic"},
});

} // namespace
} // namespace zeno
