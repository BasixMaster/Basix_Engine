// SPDX-License-Identifier: MIT
#include "basix/instances.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
using namespace basix;
using render::Vec3;
using instances::InputInstance;
int checks=0,failures=0;
void check(bool value,const char* label){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
template<class F>void rejects(F function,const char* label){bool rejected=false;try{function();}catch(const std::invalid_argument&){rejected=true;}check(rejected,label);}
bool near(double a,double b,double tolerance=1e-12){return std::abs(a-b)<=tolerance;}
bool near(Vec3 a,Vec3 b,double tolerance=1e-12){return near(a.x,b.x,tolerance)&&near(a.y,b.y,tolerance)&&near(a.z,b.z,tolerance);}
Vec3 normal(const reference::Triangle& t){return render::normalize(render::cross(t.b-t.a,t.c-t.a))*(t.reverse_orientation?-1.:1.);}
reference::Scene mesh(){
 reference::Scene result;
 result.materials={{10,{.2,.4,.8},.5,0,{},render::MaterialModel::lambert},
                   {30,{1,1,1},.5,0,{3,2,1},render::MaterialModel::lambert}};
 result.triangles={{7,10,{0,0,0},{2,0,0},{0,1,1}},
                   {19,30,{0,0,0},{0,1,1},{-2,0,0}}};
 result.environment={.01,.02,.03};return result;
}
std::array<InputInstance,2> inputs(){
 return {InputInstance{101,1000,10,{1,0,0,-3,0,1,0,0,0,0,1,0}},
         InputInstance{9001,2000,30,{-2,0,0,3,0,.5f,0,.25f,0,0,4,-.5f}}};
}
void identities_and_materials(){
 const auto source=mesh();const auto descriptors=inputs();
 const auto batch=instances::bake_instances(source,descriptors,17);
 check(sizeof(instances::Metadata)==16&&offsetof(instances::Metadata,stable_id)==8&&offsetof(instances::Metadata,epoch)==12,"metadata scalar ABI is16bytes");
 check(batch.metadata.size()==2&&batch.matrices.size()==2&&batch.condition_numbers.size()==2,"one metadata/matrix record per dense TLAS slot");
 check(batch.shader_scene.triangles.size()==4&&batch.reference_scene.triangles.size()==4,"shared mesh expands to two shader/reference instance records");
 check(batch.metadata[0].first_triangle==0&&batch.metadata[1].first_triangle==2&&batch.metadata[0].triangle_count==2&&batch.metadata[1].triangle_count==2,"dense offsets cover disjoint primitive ranges");
 check(batch.metadata[0].stable_id==101&&batch.metadata[1].stable_id==9001&&batch.metadata[0].epoch==17&&batch.metadata[1].epoch==17,"stable IDs and epoch remain separate from slots");
 check(batch.shader_scene.triangles[0].id==1007&&batch.shader_scene.triangles[1].id==1019&&batch.shader_scene.triangles[2].id==2007&&batch.shader_scene.triangles[3].id==2019,"global primitive IDs use stable per-instance bases");
 check(batch.shader_scene.triangles[0].material_id==10&&batch.shader_scene.triangles[1].material_id==10&&batch.shader_scene.triangles[2].material_id==30&&batch.shader_scene.triangles[3].material_id==30,"per-instance material overrides retain stable material IDs");
 check(near(batch.shader_scene.environment,source.environment)&&batch.shader_scene.materials.size()==2,"environment/material table preserved");
 check(batch.orientation_per_triangle==std::vector<float>({1,1,-1,-1}),"reflection orientation is per triangle and leaves shared object data unchanged");
 check(near(batch.condition_numbers[0],1)&&near(batch.condition_numbers[1],8),"independent diagonal infinity condition anchors");
 check(batch.matrices[0]==descriptors[0].matrix&&batch.matrices[1]==descriptors[1].matrix,"TLAS matrices retained exactly as supplied FP32 values");
 const auto& mirrored=batch.shader_scene.triangles[2];
 check(near(mirrored.a,{3,.25,-.5})&&near(mirrored.b,{-1,.25,-.5})&&near(mirrored.c,{3,.75,3.5}),"mirrored shader vertices retain object barycentric ordering");
 check(near(batch.reference_scene.triangles[2].a,mirrored.a)&&near(batch.reference_scene.triangles[2].b,mirrored.b)&&near(batch.reference_scene.triangles[2].c,mirrored.c)&&batch.reference_scene.triangles[2].reverse_orientation&&!mirrored.reverse_orientation,"reference orientation flag preserves identical shader sampling/barycentric vertex order");
 // Inverse-transpose of diag(-2,.5,4) maps authored(0,-1,1) to(0,-2,.25).
 const auto expected_normal=render::normalize(Vec3{0,-2,.25});
 check(near(normal(batch.reference_scene.triangles[2]),expected_normal),"independent inverse-transpose normal preserves authored outside");
 check(near(normal(mirrored)*batch.orientation_per_triangle[2],expected_normal),"shader orientation sign reconstructs the same authored normal");
 reference::World world(batch.reference_scene);
 check(near(world.light_area(),2*std::sqrt(65.)),"independent transformed emitter-area anchor");
 const auto center=(mirrored.a+mirrored.b+mirrored.c)/3.;
 const auto light=world.sample_light(center+expected_normal*2,0,4./9,.5);
 check(light.valid&&near(light.normal,expected_normal)&&light.primitive_id==2007&&near(light.emission,{3,2,1}),"one-sided mirrored emitter remains visible from authored outside");
 check(near(source.triangles[0].b,{2,0,0})&&near(source.triangles[0].c,{0,1,1})&&descriptors[1].matrix[0]==-2,"source mesh and descriptors remain immutable");
 auto reversed=descriptors;std::reverse(reversed.begin(),reversed.end());const auto reordered=instances::bake_instances(source,reversed,18);
 check(reordered.metadata[0].stable_id==9001&&reordered.metadata[1].stable_id==101&&reordered.metadata[0].first_triangle==0,"reordered dense slots retain independently stable instance identities");
 check(reordered.shader_scene.triangles[0].id==2007&&reordered.shader_scene.triangles[2].id==1007&&near(reordered.shader_scene.triangles[0].a,mirrored.a),"primitive IDs/material/geometry stable after slot reorder");
 const auto next=instances::bake_instances(source,descriptors,18);
 check(next.metadata[0].epoch==18&&next.metadata[1].epoch==18&&next.shader_scene.triangles[2].id==mirrored.id&&near(next.shader_scene.triangles[2].b,mirrored.b),"new epoch changes metadata generation without silently changing geometry");
 InputInstance no_override{99,0,0};const auto mixed=instances::bake_instances(source,std::span(&no_override,1),1);
 check(mixed.shader_scene.triangles[0].material_id==10&&mixed.shader_scene.triangles[1].material_id==30,"zero material override preserves per-primitive materials");
 auto authored_reversed=source;authored_reversed.triangles[0].reverse_orientation=true;
 const auto oriented=instances::bake_instances(authored_reversed,descriptors,17);
 check(oriented.orientation_per_triangle==std::vector<float>({-1,1,1,-1})&&
       oriented.reference_scene.triangles[0].reverse_orientation&&
       !oriented.reference_scene.triangles[1].reverse_orientation&&
       !oriented.reference_scene.triangles[2].reverse_orientation&&
       oriented.reference_scene.triangles[3].reverse_orientation,
       "authored orientation XOR determinant sign is tracked per primitive");
 bool raw_shader_flags=true;for(const auto& t:oriented.shader_scene.triangles)raw_shader_flags=raw_shader_flags&&!t.reverse_orientation;
 check(raw_shader_flags&&near(normal(oriented.reference_scene.triangles[2]),-expected_normal)&&
       near(oriented.reference_scene.triangles[2].b,mirrored.b)&&near(oriented.reference_scene.triangles[2].c,mirrored.c),
       "shader flags stay false while authored reverse normal preserves unchanged sample vertices");
}
void numerical_inputs(){
 const auto source=mesh();InputInstance descriptor{1,0,0};
 descriptor.matrix={1,.25f,0,0,0,1,0,0,.25f,0,1,0};
 auto shear=instances::bake_instances(source,std::span(&descriptor,1),1);
 check(near(shear.shader_scene.triangles[0].a,{0,0,0})&&near(shear.shader_scene.triangles[0].b,{2,0,.5})&&near(shear.shader_scene.triangles[0].c,{.25,1,1}),"independent affine shear coordinates");
 check(shear.orientation_per_triangle[0]==1&&shear.condition_numbers[0]>1&&shear.condition_numbers[0]<2,"well-conditioned shear accepted without a reflection flag");
 descriptor.matrix={0,0,1,0,0,1,0,0,-1,0,0,0};
 auto rotation=instances::bake_instances(source,std::span(&descriptor,1),1);
 check(near(rotation.shader_scene.triangles[0].b,{0,0,-2})&&near(rotation.shader_scene.triangles[0].c,{1,1,0})&&rotation.orientation_per_triangle[0]==1,"pivot-swapping rotation retains positive determinant");
 descriptor.matrix={0x1p-19f,0,0,0,0,1,0,0,0,0,1,0};
 auto bounded=instances::bake_instances(source,std::span(&descriptor,1),1);
 check(near(bounded.condition_numbers[0],0x1p19),"condition policy accepts exact power-of-two anchor below limit");
 descriptor.matrix[0]=0x1p-20f;
 rejects([&]{instances::bake_instances(source,std::span(&descriptor,1),1);},"condition policy rejects exact power-of-two anchor above limit");
 auto non_float=source;non_float.triangles[0].b.x=1.+0x1p-30;
 InputInstance identity{1,0,0};auto rounded=instances::bake_instances(non_float,std::span(&identity,1),1);
 check(rounded.shader_scene.triangles[0].b.x==1.,"object vertices quantized before transformation");
 bool analytic=true;
 for(int exponent=-8;exponent<=8;++exponent)for(int sign:{-1,1}){
  const float sx=std::ldexp(float(sign),exponent),sy=std::ldexp(1.f,-exponent);
  InputInstance varied{1,0,0,{sx,0,0,.25f,0,sy,0,-.5f,0,0,2,.125f}};
  const auto output=instances::bake_instances(source,std::span(&varied,1),1);
  const auto& t=output.shader_scene.triangles[0];
  const Vec3 expected_b{float(2*sx+.25f),-.5,.125},expected_c{.25,float(sy-.5f),2.125};
  const auto desired=render::normalize(Vec3{0,-1/double(sy),.5});
  analytic=analytic&&near(t.b,expected_b)&&near(t.c,expected_c)&&near(normal(output.reference_scene.triangles[0]),desired)&&output.orientation_per_triangle[0]==float(sign);
 }
 check(analytic,"34 independent diagonal/reflection coordinate and normal anchors");
}
void invalid_inputs(){
 const auto source=mesh();const auto good=inputs();
 rejects([&]{instances::bake_instances(source,good,0);},"zero epoch rejected");
 rejects([&]{instances::bake_instances(source,{},1);},"empty instance list rejected");
 auto invalid=source;invalid.triangles.clear();rejects([&]{instances::bake_instances(invalid,good,1);},"empty mesh rejected");
 invalid=source;invalid.spheres.push_back({100,10,{0,0,0},1});rejects([&]{instances::bake_instances(invalid,good,1);},"sphere geometry rejected");
 invalid=source;invalid.materials[0].base_color.x=std::numeric_limits<double>::quiet_NaN();rejects([&]{instances::bake_instances(invalid,good,1);},"invalid source material rejected");
 invalid=source;invalid.triangles[1].id=7;rejects([&]{instances::bake_instances(invalid,good,1);},"duplicate source primitive ID rejected");
 invalid=source;invalid.triangles[0].material_id=999;rejects([&]{instances::bake_instances(invalid,good,1);},"unknown source material rejected");
 auto descriptors=good;descriptors[0].stable_id=0;rejects([&]{instances::bake_instances(source,descriptors,1);},"zero stable instance ID rejected");
 descriptors=good;descriptors[1].stable_id=101;rejects([&]{instances::bake_instances(source,descriptors,1);},"duplicate stable instance ID rejected");
 descriptors=good;descriptors[1].primitive_id_base=1000;rejects([&]{instances::bake_instances(source,descriptors,1);},"colliding global primitive IDs rejected");
 descriptors=good;descriptors[0].primitive_id_base=std::numeric_limits<std::uint32_t>::max()-6;rejects([&]{instances::bake_instances(source,descriptors,1);},"primitive ID addition overflow rejected");
 InputInstance edge{77,std::numeric_limits<std::uint32_t>::max()-19,0};
 auto highest=instances::bake_instances(source,std::span(&edge,1),1);check(highest.shader_scene.triangles[1].id==std::numeric_limits<std::uint32_t>::max(),"highest representable primitive ID accepted without wrap");
 descriptors=good;descriptors[0].material_override=999;rejects([&]{instances::bake_instances(source,descriptors,1);},"unknown override rejected");
 for(unsigned slot=0;slot<12;++slot){descriptors=good;descriptors[0].matrix[slot]=std::numeric_limits<float>::quiet_NaN();rejects([&]{instances::bake_instances(source,descriptors,1);},"nonfinite matrix component rejected");}
 descriptors=good;descriptors[0].matrix[3]=std::numeric_limits<float>::infinity();rejects([&]{instances::bake_instances(source,descriptors,1);},"infinite translation rejected");
 descriptors=good;descriptors[0].matrix[0]=0;rejects([&]{instances::bake_instances(source,descriptors,1);},"zero-row singular matrix rejected");
 descriptors=good;descriptors[0].matrix={1,1,0,0,1,1,0,0,0,0,1,0};rejects([&]{instances::bake_instances(source,descriptors,1);},"nonzero-row singular matrix rejected");
 descriptors=good;descriptors[0].matrix[0]=1e-8f;rejects([&]{instances::bake_instances(source,descriptors,1);},"ill-conditioned invertible transform rejected");
 descriptors=good;descriptors[0].matrix[3]=1e20f;rejects([&]{instances::bake_instances(source,descriptors,1);},"world geometry collapsed by FP32 translation rejected");
 descriptors=good;descriptors[0].matrix={1e30f,0,0,0,0,1e30f,0,0,0,0,1e30f,0};rejects([&]{instances::bake_instances(source,descriptors,1);},"overflowing FP32 world area rejected");
 descriptors=good;descriptors[0].matrix={1e-30f,0,0,0,0,1e-30f,0,0,0,0,1e-30f,0};rejects([&]{instances::bake_instances(source,descriptors,1);},"underflowing FP32 world area rejected");
 invalid=source;invalid.triangles[0].b.x=1e100;rejects([&]{instances::bake_instances(invalid,good,1);},"object vertex outside FP32 range rejected");
 invalid=source;invalid.triangles.resize(1);const double maximum=std::numeric_limits<float>::max();invalid.triangles[0].a={-maximum,0,0};invalid.triangles[0].b={maximum,0,0};invalid.triangles[0].c={-maximum,1e-38,0};rejects([&]{instances::bake_instances(invalid,good,1);},"overflowing float edge rejected before narrowing conversion");
 invalid=source;for(auto& t:invalid.triangles){t.a=t.a*1e-100;t.b=t.b*1e-100;t.c=t.c*1e-100;}rejects([&]{instances::bake_instances(invalid,good,1);},"object geometry collapsed during upload rejected");
 std::vector<InputInstance> too_many(50001);rejects([&]{instances::bake_instances(source,too_many,1);},"bounded total instance triangle count enforced before allocation");
 // Failure never mutates source object geometry or partially returns a batch.
 check(near(source.triangles[0].b,{2,0,0})&&source.triangles.size()==2,"failed validations preserve input mesh");
}
}
int main(){identities_and_materials();numerical_inputs();invalid_inputs();std::cout<<"Instance baking: "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}
