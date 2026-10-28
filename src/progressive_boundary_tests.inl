// SPDX-License-Identifier: MIT
// Bounded single-instance hardware barycentric fixtures. Geometry metadata is
// an independently baked world-space counterpart of the object-space BLAS.
br::Vec3 boundary_transform(const std::array<float,12>& m,br::Vec3 p,bool point=true){
 return {double(m[0])*p.x+double(m[1])*p.y+double(m[2])*p.z+(point?m[3]:0),double(m[4])*p.x+double(m[5])*p.y+double(m[6])*p.z+(point?m[7]:0),double(m[8])*p.x+double(m[9])*p.y+double(m[10])*p.z+(point?m[11]:0)};
}
std::array<float,12> boundary_matrix(unsigned id,double scale){
 std::array<float,12> m{1,0,0,0,0,1,0,0,0,0,1,0};
 if(id==1)m={0,0,1,0,0,1,0,0,-1,0,0,0};
 if(id==2)m={2,0,0,0,0,.5f,0,0,0,0,4,0};
 if(id==3)m={1,.25f,0,0,0,1,0,0,.25f,0,1,0};
 if(id==4)m[0]=-1;
 if(id==5){m[3]=float(.125*scale);m[7]=float(-.25*scale);m[11]=float(.5*scale);}
 return m;
}
void boundary_number(std::ostream& out,double value){if(std::isfinite(value))out<<value;else out<<"null";}
void boundary_vec(std::ostream& out,br::Vec3 v){out<<'[';boundary_number(out,v.x);out<<',';boundary_number(out,v.y);out<<',';boundary_number(out,v.z);out<<']';}
void boundary_four(std::ostream& out,abi::Float4 v){out<<'[';boundary_number(out,v.x);out<<',';boundary_number(out,v.y);out<<',';boundary_number(out,v.z);out<<',';boundary_number(out,v.w);out<<']';}
void boundary_geometry(std::ostream& out,const ref::Scene& scene){out<<'[';bool comma=false;for(const auto& t:scene.triangles){if(comma)out<<',';comma=true;out<<'[';bool component=false;for(auto p:{t.a,t.b,t.c})for(double x:{p.x,p.y,p.z}){if(component)out<<',';component=true;boundary_number(out,x);}out<<']';}out<<']';}
int run_ray_boundary_cases(const std::filesystem::path& out,bool validate,bool omit,bool bad_spawn,bool bad_transform){
 std::filesystem::create_directories(out);unsigned cases=0,failed=0,actual_vertices=0,actual_edges=0;bool validation=false;std::ostringstream rows;rows<<std::setprecision(17);bool comma=false;
 {
 Owner owner;owner.initialize(validate);
 validation=owner.validation_enabled;
 for(unsigned transform=0;transform<6;++transform)for(double scale:{.001,1.,1000.})for(int side:{-1,1})for(unsigned target_id=0;target_id<5;++target_id)for(unsigned layer=0;layer<2;++layer){
  ref::Scene object;object.materials={{1,{.5,.5,.5},.5,0,{},br::MaterialModel::lambert}};
  auto fan=[&](double z){const br::Vec3 center{0,0,z};const std::array<br::Vec3,4> rim={br::Vec3{scale,0,z},{0,scale,z},{-scale,0,z},{0,-scale,z}};for(unsigned i=0;i<4;++i)object.triangles.push_back({uint32_t(object.triangles.size()+1),1,quantize(center),quantize(rim[i]),quantize(rim[(i+1)%4])});};
  fan(0);if(layer)fan(side*1e-5*scale);
  const auto matrix=boundary_matrix(transform,scale);auto world=object;for(auto& t:world.triangles){t.a=quantize(boundary_transform(matrix,t.a));t.b=quantize(boundary_transform(matrix,t.b));t.c=quantize(boundary_transform(matrix,t.c));}
  br::Vec3 target_object{};if(target_id==1)target_object={.5*scale,0,0};if(target_id==2)target_object={.25*scale,.125*scale,0};if(target_id>=3)target_object={.5*scale,(target_id==3?1:-1)*0x1p-18*scale,0};target_object=quantize(target_object);
  const auto target=quantize(boundary_transform(matrix,target_object));ref::Camera camera;camera.origin=quantize(boundary_transform(matrix,target_object+br::Vec3{0,0,-side*scale}));camera.target=target;
  const auto primary_direction=quantize(br::normalize(boundary_transform(matrix,{0,0,double(side)},false)));const auto secondary_direction=quantize(br::normalize(boundary_transform(matrix,{.125,.0625,double(side)},false)));
  ref::RenderConfig config;config.width=4;config.height=1;SceneAcceleration acceleration(owner,object,bad_transform?nullptr:&matrix);NativePipeline pipeline(owner);pipeline.initialize(*acceleration.tlas);MappedScene mapped(owner,world,config,camera);mapped.root->flags|=16;
  mapped.root->camera_forward={float(primary_direction.x),float(primary_direction.y),float(primary_direction.z),0};mapped.root->camera_right={float(secondary_direction.x),float(secondary_direction.y),float(secondary_direction.z),0};mapped.dispatch(pipeline,0,1,bad_spawn?5:4,omit);
  const auto first=mapped.hits[0],point=mapped.hits[1],normal=mapped.hits[2],spawn=mapped.accumulation[0],second=mapped.accumulation[1];
  auto vec=[](abi::Float4 p){return br::Vec3{p.x,p.y,p.z};};
  bool finite=true;for(auto p:{first,point,normal,spawn,second})for(float x:{p.x,p.y,p.z,p.w})finite=finite&&std::isfinite(x);
  bool pass=finite&&point.w==0&&normal.w==1&&first.x>0&&first.y>=0&&first.y<4&&first.y==std::floor(first.y)&&first.z>=0&&first.w>=0&&first.z<=1&&first.w<=1&&float(first.z+first.w)<=1&&spawn.w>=0&&second.w==0;
  const auto& plane=world.triangles[0];const auto plane_normal=br::normalize(br::cross(plane.b-plane.a,plane.c-plane.a));const double outward=br::dot(plane_normal,secondary_direction)>=0?1:-1;
  const double actual_gap=layer?std::abs(br::dot(world.triangles[4].a-plane.a,plane_normal)):0;
  const double distance=br::dot(vec(spawn)-plane.a,plane_normal)*outward;
  pass=pass&&distance>0;
  if(layer)pass=pass&&actual_gap>0&&distance<actual_gap&&second.z==1&&second.y>=4&&second.y<8&&second.y==std::floor(second.y)&&second.x>0;
  else pass=pass&&second.z==0;
  double magnitude=scale;for(auto p:{camera.origin,target})for(double x:{p.x,p.y,p.z})magnitude=std::max(magnitude,std::abs(x));for(const auto& t:world.triangles)for(auto p:{t.a,t.b,t.c})for(double x:{p.x,p.y,p.z})magnitude=std::max(magnitude,std::abs(x));const double tolerance=16*0x1p-24*magnitude;
  if(finite&&first.y>=0&&first.y<4&&first.y==std::floor(first.y)){
   const auto& t=world.triangles[uint32_t(first.y)];const auto e1=t.b-t.a,e2=t.c-t.a,q=target-t.a;
   const double a=br::dot(e1,e1),b=br::dot(e1,e2),c=br::dot(e2,e2),d=br::dot(q,e1),e=br::dot(q,e2),det=a*c-b*b;
   const double u=(d*c-e*b)/det,v=(e*a-d*b)/det;
   pass=pass&&det>0&&u>=-2*0x1p-24&&v>=-2*0x1p-24&&u+v<=1+2*0x1p-24;
   const auto bary_point=t.a+e1*first.z+e2*first.w;
   for(auto delta:{vec(point)-target,vec(point)-bary_point,vec(point)-(camera.origin+primary_direction*first.x)})pass=pass&&br::max_component({std::abs(delta.x),std::abs(delta.y),std::abs(delta.z)})<=tolerance;
  }
  if(finite&&layer&&second.z==1&&second.y>=4&&second.y<8&&second.y==std::floor(second.y)){
   const auto& blocker=world.triangles[uint32_t(second.y)];const auto e1=blocker.b-blocker.a,e2=blocker.c-blocker.a,secondary_point=vec(spawn)+secondary_direction*second.x,q=secondary_point-blocker.a;
   const auto n=br::normalize(br::cross(e1,e2));const double a=br::dot(e1,e1),b=br::dot(e1,e2),c=br::dot(e2,e2),d=br::dot(q,e1),e=br::dot(q,e2),det=a*c-b*b;
   const double u=(d*c-e*b)/det,v=(e*a-d*b)/det;
   pass=pass&&std::abs(br::dot(q,n))<=tolerance&&det>0&&u>=-2*0x1p-24&&v>=-2*0x1p-24&&u+v<=1+2*0x1p-24;
  }
  const bool vertex=finite&&normal.w==1&&((first.z==0&&first.w==0)||(first.z==1&&first.w==0)||(first.z==0&&first.w==1));const bool edge=finite&&normal.w==1&&!vertex&&(first.z==0||first.w==0||float(first.z+first.w)==1);
  if(vertex)++actual_vertices;if(edge)++actual_edges;++cases;if(!pass){++failed;if(failed<=10)std::cerr<<"Boundary fixture fail transform="<<transform<<" scale="<<scale<<" side="<<side<<" target="<<target_id<<" layer="<<layer<<" first="<<first.x<<','<<first.y<<','<<first.z<<','<<first.w<<" found="<<normal.w<<" errors="<<point.w<<','<<second.w<<" distance="<<distance<<" gap="<<actual_gap<<'\n';}
  if(comma)rows<<',';comma=true;rows<<"{\"scale\":"<<scale<<",\"transform_id\":"<<transform<<",\"side\":"<<side<<",\"target_id\":"<<target_id<<",\"expected_layer\":"<<layer<<",\"matrix\":[";for(size_t i=0;i<matrix.size();++i){if(i)rows<<',';rows<<matrix[i];}rows<<"],\"object_triangles\":";boundary_geometry(rows,object);rows<<",\"world_triangles\":";boundary_geometry(rows,world);rows<<",\"primary_origin\":";boundary_vec(rows,camera.origin);rows<<",\"primary_direction\":";boundary_vec(rows,primary_direction);rows<<",\"secondary_direction\":";boundary_vec(rows,secondary_direction);rows<<",\"target_world\":";boundary_vec(rows,target);rows<<",\"first\":";boundary_four(rows,first);rows<<",\"point\":";boundary_vec(rows,vec(point));rows<<",\"normal\":";boundary_vec(rows,vec(normal));rows<<",\"spawn\":";boundary_vec(rows,vec(spawn));rows<<",\"second\":";boundary_four(rows,second);rows<<",\"first_found\":"<<(normal.w==1?"true":"false")<<",\"hit_error\":";boundary_number(rows,point.w);rows<<",\"actual_gap\":"<<actual_gap<<",\"position_tolerance\":"<<tolerance<<",\"exact_vertex_hit\":"<<(vertex?"true":"false")<<",\"exact_edge_hit\":"<<(edge?"true":"false")<<",\"passed\":"<<(pass?"true":"false")<<'}';
 }
 }
 const bool passed=failed==0&&actual_vertices>=12&&actual_edges>=12&&validation_errors==0&&validation_warnings==0;
 std::ofstream f(out/"ray-boundary-result.json");f<<"{\"status\":\""<<(passed?"passed":"failed")<<"\",\"candidate\":\"world_triangle_metadata_v1\",\"cases\":"<<cases<<",\"failed_cases\":"<<failed<<",\"exact_vertex_hits\":"<<actual_vertices<<",\"exact_edge_hits\":"<<actual_edges<<",\"minimum_exact_vertex_hits\":12,\"minimum_exact_edge_hits\":12,\"bad_spawn\":"<<(bad_spawn?"true":"false")<<",\"bad_transform\":"<<(bad_transform?"true":"false")<<",\"omit_dispatch\":"<<(omit?"true":"false")<<",\"validation_enabled\":"<<(validation?"true":"false")<<",\"validation_errors\":"<<validation_errors<<",\"validation_warnings\":"<<validation_warnings<<",\"fixtures\":["<<rows.str()<<"],\"scope\":\"Single TLAS instance, six fixed identity/rotation/nonuniform/shear/mirror/translation transforms, shared fan vertex/edge and adjacent targets, three scales and two sides/layer states. World-space triangle metadata is separately baked from the object BLAS. Empirical allowance only; no general instance remapping, arbitrary transform error proof, motion, performance or cross-device acceptance.\"}\n";f.close();require(bool(f),"Boundary receipt write failed");require(passed,"Ray boundary fixture gate failed");std::cout<<"PASS ray boundary fixtures "<<cases<<" exactVertices="<<actual_vertices<<" exactEdges="<<actual_edges<<'\n';return 0;
}
