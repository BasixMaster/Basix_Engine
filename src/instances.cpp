// SPDX-License-Identifier: MIT
#include "basix/instances.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

namespace basix::instances {
namespace {
using render::Vec3;
void require(bool condition, const char* reason) {
    if (!condition) throw std::invalid_argument(reason);
}
Vec3 quantize(Vec3 value) {
    require(render::finite(value), "Nonfinite instance position");
    const double maximum=std::numeric_limits<float>::max();
    require(std::abs(value.x)<=maximum&&std::abs(value.y)<=maximum&&
            std::abs(value.z)<=maximum,"Instance position exceeds FP32 range");
    const Vec3 result{float(value.x), float(value.y), float(value.z)};
    require(render::finite(result), "Instance position exceeds FP32 range");
    return result;
}
Vec3 transform(const Matrix& m, Vec3 p) {
    return quantize({double(m[0])*p.x+double(m[1])*p.y+double(m[2])*p.z+m[3],
                     double(m[4])*p.x+double(m[5])*p.y+double(m[6])*p.z+m[7],
                     double(m[8])*p.x+double(m[9])*p.y+double(m[10])*p.z+m[11]});
}
struct MatrixInfo { double condition{}, determinant{}; };
MatrixInfo analyze(const Matrix& matrix) {
    for (float value : matrix)
        require(std::isfinite(value), "Nonfinite instance transform");
    // Independent double Gauss-Jordan inverse, with row-scaled pivot choice.
    // Matrix entries originate in FP32; the condition bound is a supported
    // input policy and does not establish any Vulkan traversal uncertainty.
    double rows[3][6]{}; double scales[3]{}; double norm = 0;
    for (unsigned r=0; r<3; ++r) {
        double sum=0;
        for (unsigned c=0; c<3; ++c) {
            rows[r][c]=matrix[r*4+c]; sum+=std::abs(rows[r][c]);
            scales[r]=std::max(scales[r], std::abs(rows[r][c]));
        }
        require(scales[r]>0, "Singular instance transform");
        rows[r][r+3]=1; norm=std::max(norm,sum);
    }
    double determinant=1;
    for (unsigned column=0; column<3; ++column) {
        unsigned pivot=column;
        for (unsigned r=column+1; r<3; ++r)
            if (std::abs(rows[r][column])/scales[r]>
                std::abs(rows[pivot][column])/scales[pivot]) pivot=r;
        require(rows[pivot][column]!=0 && std::isfinite(rows[pivot][column]),
                "Singular instance transform");
        if (pivot!=column) {
            for (unsigned c=0; c<6; ++c) std::swap(rows[pivot][c],rows[column][c]);
            std::swap(scales[pivot],scales[column]); determinant=-determinant;
        }
        const double divisor=rows[column][column]; determinant*=divisor;
        for (unsigned c=0; c<6; ++c) rows[column][c]/=divisor;
        for (unsigned r=0; r<3; ++r) if (r!=column) {
            const double factor=rows[r][column];
            for (unsigned c=0; c<6; ++c) rows[r][c]-=factor*rows[column][c];
        }
    }
    double inverse_norm=0;
    for (unsigned r=0; r<3; ++r) {
        double sum=0;
        for (unsigned c=3; c<6; ++c) {
            require(std::isfinite(rows[r][c]), "Unrepresentable inverse transform");
            sum+=std::abs(rows[r][c]);
        }
        inverse_norm=std::max(inverse_norm,sum);
    }
    const double condition=norm*inverse_norm;
    require(std::isfinite(determinant)&&determinant!=0,
            "Singular instance transform");
    require(std::isfinite(condition)&&condition<=maximum_condition_number,
            "Ill-conditioned instance transform");
    return {condition,determinant};
}
void gpu_triangle_domain(const reference::Triangle& t) {
    const auto e1=t.b-t.a, e2=t.c-t.a;
    const auto raw=render::cross(e1,e2);
    const double area=.5*std::hypot(raw.x,raw.y,raw.z);
    require(std::isfinite(area)&&area>0&&area<=std::numeric_limits<float>::max()&&float(area)>0,
            "Instance triangle area is outside FP32 domain");
    const double maximum=std::numeric_limits<float>::max();
    require(std::max({std::abs(e1.x),std::abs(e1.y),std::abs(e1.z),
                       std::abs(e2.x),std::abs(e2.y),std::abs(e2.z)})<=maximum,
            "Instance triangle edges exceed FP32 domain");
    const float x1=float(e1.x),y1=float(e1.y),z1=float(e1.z);
    const float x2=float(e2.x),y2=float(e2.y),z2=float(e2.z);
    const float scale=std::max({std::abs(x1),std::abs(y1),std::abs(z1),
                                std::abs(x2),std::abs(y2),std::abs(z2)});
    require(std::isfinite(scale)&&scale>0,"Instance triangle edges exceed FP32 domain");
    const float ax=x1/scale,ay=y1/scale,az=z1/scale;
    const float bx=x2/scale,by=y2/scale,bz=z2/scale;
    const float nx=ay*bz-az*by,ny=az*bx-ax*bz,nz=ax*by-ay*bx;
    require(std::isfinite(nx)&&std::isfinite(ny)&&std::isfinite(nz)&&
            (nx!=0||ny!=0||nz!=0),"Instance triangle normal degenerates in FP32");
}
}

Batch bake_instances(const reference::Scene& mesh,
                     std::span<const InputInstance> inputs,
                     std::uint32_t epoch) {
    require(epoch!=0,"Instance epoch must be nonzero");
    require(!inputs.empty()&&inputs.size()<=0x1000000u,"Invalid instance count");
    require(mesh.spheres.empty()&&!mesh.triangles.empty(),
            "Instances require nonempty triangle-only mesh");
    require(mesh.triangles.size()<=100000 && inputs.size()<=100000/mesh.triangles.size(),
            "Instanced scene exceeds bounded triangle count");
    std::string reason;
    require(reference::validate(mesh,&reason),"Invalid instance source mesh/materials");
    auto object=mesh;
    for (auto& triangle : object.triangles) {
        triangle.a=quantize(triangle.a); triangle.b=quantize(triangle.b);
        triangle.c=quantize(triangle.c); gpu_triangle_domain(triangle);
    }
    require(reference::validate(object,&reason),"Source mesh degenerates after FP32 upload");
    Batch result;
    result.shader_scene.materials=mesh.materials;
    result.shader_scene.environment=mesh.environment;
    result.reference_scene.materials=mesh.materials;
    result.reference_scene.environment=mesh.environment;
    const auto count=inputs.size()*mesh.triangles.size();
    result.shader_scene.triangles.reserve(count);
    result.reference_scene.triangles.reserve(count);
    result.orientation_per_triangle.reserve(count);
    result.metadata.reserve(inputs.size());result.matrices.reserve(inputs.size());
    result.condition_numbers.reserve(inputs.size());
    std::set<std::uint32_t> instance_ids, primitive_ids, materials;
    for (const auto& material : mesh.materials) materials.insert(material.id);
    for (const auto& input : inputs) {
        require(input.stable_id!=0&&instance_ids.insert(input.stable_id).second,
                "Invalid or duplicate stable instance ID");
        require(input.material_override==0||materials.contains(input.material_override),
                "Unknown instance material override");
        const auto info=analyze(input.matrix);
        const bool mirrored=info.determinant<0;
        result.metadata.push_back({std::uint32_t(result.shader_scene.triangles.size()),
            std::uint32_t(object.triangles.size()),input.stable_id,epoch});
        result.matrices.push_back(input.matrix);result.condition_numbers.push_back(info.condition);
        for (const auto& original : object.triangles) {
            require(original.id<=std::numeric_limits<std::uint32_t>::max()-input.primitive_id_base,
                    "Instance primitive ID overflow");
            const auto id=input.primitive_id_base+original.id;
            require(id!=0&&primitive_ids.insert(id).second,"Duplicate instanced primitive ID");
            reference::Triangle triangle{id,input.material_override?input.material_override:original.material_id,
                transform(input.matrix,original.a),transform(input.matrix,original.b),
                transform(input.matrix,original.c)};
            triangle.reverse_orientation=false;
            gpu_triangle_domain(triangle);
            result.shader_scene.triangles.push_back(triangle);
            const bool reverse=original.reverse_orientation!=mirrored;
            triangle.reverse_orientation=reverse;
            result.reference_scene.triangles.push_back(triangle);
            result.orientation_per_triangle.push_back(reverse?-1.f:1.f);
        }
    }
    require(reference::validate(result.shader_scene,&reason)&&
            reference::validate(result.reference_scene,&reason),"Invalid baked instance scene");
    return result;
}
} // namespace basix::instances
