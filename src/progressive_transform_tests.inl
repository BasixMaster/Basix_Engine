// SPDX-License-Identifier: MIT
// Explicit bounded-ray-domain experiment; hardware terms remain conditional policies.
void transform_budget_json(std::ostream& out,const basix::transform_uncertainty::Budget& b){
 out<<"{\"representation\":";boundary_vec(out,b.representation);out<<",\"forward_policy\":";boundary_vec(out,b.forward_policy);out<<",\"object_policy\":";boundary_vec(out,b.object_policy);out<<",\"inverse_policy\":";boundary_vec(out,b.inverse_policy);out<<",\"total\":";boundary_vec(out,b.total);out<<'}';
}
int run_transform_cases(const std::filesystem::path& out,bool validate,const std::string& fault){
 require(fault.empty()||fault=="zero-budget"||fault=="negative-budget"||fault=="origin"||fault=="direction"||fault=="omit","Unknown transform fixture fault");
 std::filesystem::create_directories(out);unsigned cases=0,failed=0,vertices=0,edges=0;bool validation=false;std::ostringstream rows;rows<<std::setprecision(17);bool comma=false;
 {
 Owner owner;owner.initialize(validate);validation=owner.validation_enabled;
 for(unsigned id=0;id<6;++id)for(int side:{-1,1})for(unsigned target_id=0;target_id<3;++target_id)for(unsigned grazing=0;grazing<2;++grazing)for(unsigned layer=0;layer<2;++layer){
  if(grazing&&target_id!=2)continue;
  const double object_offset=id==0?0:(id==2||id==4?512:64);
  ref::Scene mesh;mesh.materials={{1,{.5,.5,.5},.5,0,{},br::MaterialModel::lambert}};
  auto fan=[&](double z){const br::Vec3 center{0,0,z};const std::array<br::Vec3,4> rim={br::Vec3{1,0,z},{0,1,z},{-1,0,z},{0,-1,z}};for(unsigned i=0;i<4;++i)mesh.triangles.push_back({uint32_t(mesh.triangles.size()+1),1,center,rim[i],rim[(i+1)%4]});};
  fan(object_offset);if(layer)fan(float(object_offset+side*.01));
  auto matrix=boundary_matrix(id,1);for(unsigned r=0;r<3;++r)matrix[r*4+3]=float(double(matrix[r*4+3])-double(matrix[r*4+2])*object_offset);
  bi::InputInstance input;input.stable_id=10000123;input.primitive_id_base=1000;input.matrix=matrix;
  const auto data=bi::bake_instances(mesh,std::span<const bi::InputInstance>(&input,1),1);const auto budgets=basix::transform_uncertainty::analyze(mesh,data);
  br::Vec3 target_object{0,0,object_offset};if(target_id==1)target_object.x=.5;if(target_id==2){target_object.x=.25;target_object.y=.125;}
  const auto target=quantize(boundary_transform(matrix,target_object));const auto primary_direction=quantize(br::normalize(boundary_transform(matrix,grazing?br::Vec3{1,0,side*0x1p-10}:br::Vec3{0,0,double(side)},false)));const auto secondary_direction=quantize(br::normalize(boundary_transform(matrix,{.125,.0625,double(side)},false)));
  auto origin=quantize(target-primary_direction*(grazing?10.:2.));auto direction=primary_direction;if(fault=="origin")origin.x=40;if(fault=="direction")direction=direction*2;
  ref::Camera camera;camera.origin=origin;camera.target=target;ref::RenderConfig config;config.width=4;config.height=1;
  SceneAcceleration acceleration(owner,mesh);acceleration.shared_instances(shared_specs(data));NativePipeline pipeline(owner);pipeline.initialize(*acceleration.tlas);
  MappedScene mapped(owner,data.shader_scene,config,camera,data.metadata,data.orientation_per_triangle,1,budgets);mapped.root->flags|=16;
  mapped.root->camera_origin={float(origin.x),float(origin.y),float(origin.z),0};mapped.root->camera_forward={float(direction.x),float(direction.y),float(direction.z),0};mapped.root->camera_right={float(secondary_direction.x),float(secondary_direction.y),float(secondary_direction.z),0};
  if(fault=="zero-budget"||fault=="negative-budget"){auto* gpu_tri=reinterpret_cast<abi::Triangle*>(mapped.heap.range.cpu+(mapped.root->triangles-mapped.root_address));for(size_t i=0;i<budgets.size();++i)gpu_tri[i].error_x=gpu_tri[i].error_y=gpu_tri[i].error_z=fault=="zero-budget"?0.f:-1.f;}
  mapped.dispatch(pipeline,0,1,4,fault=="omit");
  const auto first=mapped.hits[0],point=mapped.hits[1],normal=mapped.hits[2],spawn=mapped.accumulation[0],second=mapped.accumulation[1],error=mapped.accumulation[2],gpu_budget=mapped.accumulation[3];auto vec=[](abi::Float4 v){return br::Vec3{v.x,v.y,v.z};};
  bool pass=true;for(auto v:{first,point,normal,spawn,second,error,gpu_budget})for(float x:{v.x,v.y,v.z,v.w})pass=pass&&std::isfinite(x);
  pass=pass&&point.w==0&&normal.w==1&&first.x>0&&first.x<=128&&first.y>=0&&first.y<4&&first.y==std::floor(first.y)&&second.w==0&&error.w==1;
  const auto& plane=data.shader_scene.triangles[0];const auto n=br::normalize(br::cross(plane.b-plane.a,plane.c-plane.a));const double sign=br::dot(n,secondary_direction)>=0?1:-1;const double distance=br::dot(vec(spawn)-plane.a,n)*sign;const double gap=layer?std::abs(br::dot(data.shader_scene.triangles[4].a-plane.a,n)):0;
  pass=pass&&distance>0;
  if(pass){const auto index=uint32_t(first.y);const auto& t=data.shader_scene.triangles[index];const auto bary=t.a+(t.b-t.a)*first.z+(t.c-t.a)*first.w;const auto b=budgets[index].total;pass=pass&&gpu_budget.x==float(b.x)&&gpu_budget.y==float(b.y)&&gpu_budget.z==float(b.z)&&error.x>=b.x&&error.y>=b.y&&error.z>=b.z;
   const auto ray_point=origin+direction*first.x;const auto delta=vec(point)-ray_point;pass=pass&&std::abs(delta.x)<=error.x+2e-6&&std::abs(delta.y)<=error.y+2e-6&&std::abs(delta.z)<=error.z+2e-6&&br::max_component({std::abs(vec(point).x-bary.x),std::abs(vec(point).y-bary.y),std::abs(vec(point).z-bary.z)})<=2e-6;
  }
  if(layer)pass=pass&&gap>0&&distance<gap&&second.z==1&&second.y>=4&&second.y<8&&second.y==std::floor(second.y)&&second.x>0;else pass=pass&&second.z==0;
  if(pass&&layer){const auto q=vec(spawn)+secondary_direction*second.x;const double tolerance=std::max({double(error.x),double(error.y),double(error.z)})+2e-6;pass=std::abs(br::dot(q-data.shader_scene.triangles[4].a,n))<=tolerance;}
  const bool vertex=normal.w==1&&((first.z==0&&first.w==0)||(first.z==1&&first.w==0)||(first.z==0&&first.w==1));const bool edge=normal.w==1&&!vertex&&(first.z==0||first.w==0||float(first.z+first.w)==1);vertices+=vertex;edges+=edge;
  ++cases;if(!pass){++failed;if(failed<=6)std::cerr<<"Transform fixture fail id="<<id<<" side="<<side<<" target="<<target_id<<" grazing="<<grazing<<" layer="<<layer<<" hit="<<first.x<<','<<first.y<<" error="<<point.w<<" spawnDistance="<<distance<<" gap="<<gap<<'\n';}
  if(comma)rows<<',';comma=true;rows<<"{\"transform_id\":"<<id<<",\"side\":"<<side<<",\"target_id\":"<<target_id<<",\"grazing\":"<<(grazing?"true":"false")<<",\"layer\":"<<layer<<",\"matrix\":[";for(size_t i=0;i<12;++i){if(i)rows<<',';rows<<matrix[i];}rows<<"],\"object_triangles\":";boundary_geometry(rows,mesh);rows<<",\"world_triangles\":";boundary_geometry(rows,data.shader_scene);rows<<",\"budgets\":[";for(size_t i=0;i<budgets.size();++i){if(i)rows<<',';transform_budget_json(rows,budgets[i]);}rows<<"],\"primary_origin\":";boundary_vec(rows,origin);rows<<",\"primary_direction\":";boundary_vec(rows,direction);rows<<",\"secondary_direction\":";boundary_vec(rows,secondary_direction);rows<<",\"target_world\":";boundary_vec(rows,target);rows<<",\"first\":";boundary_four(rows,first);rows<<",\"point\":";boundary_vec(rows,vec(point));rows<<",\"normal\":";boundary_vec(rows,vec(normal));rows<<",\"first_found\":"<<(normal.w==1?"true":"false")<<",\"hit_error\":";boundary_number(rows,point.w);rows<<",\"spawn\":";boundary_vec(rows,vec(spawn));rows<<",\"second\":";boundary_four(rows,second);rows<<",\"point_error\":";boundary_vec(rows,vec(error));rows<<",\"gpu_budget\":";boundary_vec(rows,vec(gpu_budget));rows<<",\"passed\":"<<(pass?"true":"false")<<'}';
 }
 }
 const bool pass=failed==0&&cases==96&&validation_errors==0&&validation_warnings==0;
 std::ofstream file(out/"transform-ray-result.json");file<<"{\"status\":\""<<(pass?"passed":"failed")<<"\",\"cases\":"<<cases<<",\"failed_cases\":"<<failed<<",\"fault\":\""<<fault<<"\",\"origin_limit\":32,\"max_distance\":128,\"root_bytes\":208,\"triangle_bytes\":80,\"exact_vertices\":"<<vertices<<",\"exact_edges\":"<<edges<<",\"validation_enabled\":"<<(validation?"true":"false")<<",\"validation_errors\":"<<validation_errors<<",\"validation_warnings\":"<<validation_warnings<<",\"fixtures\":["<<rows.str()<<"],\"scope\":\"Fixed96-case origin32/distance128 domain, six affine transforms and cancellation offsets, normal/grazing rays, exact boundaries and separated blockers. Representation error enclosed arithmetically; forward/object/inverse arithmetic terms are conditional proxy policies, not a Vulkan hardware traversal proof or nearby-geometry guarantee.\"}\n";file.close();require(bool(file),"Transform receipt write failed");require(pass,"Transform ray fixture gate failed");std::cout<<"PASS transform ray fixtures "<<cases<<'\n';return 0;
}
