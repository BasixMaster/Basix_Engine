// SPDX-License-Identifier: MIT
// Included in the native experiment's internal scope; no additional device owner.
void rebase_fixture(ref::Scene& scene,ref::Camera& camera,br::Vec3 shift,bool rebase){
 // Construct a double-precision world fixture, then remove the selected local
 // origin before FP32 conversion. Information lost before this step is not recovered.
 for(auto& t:scene.triangles){t.a=t.a+shift;t.b=t.b+shift;t.c=t.c+shift;}
 camera.origin=camera.origin+shift;camera.target=camera.target+shift;
 if(rebase){for(auto& t:scene.triangles){t.a=t.a-shift;t.b=t.b-shift;t.c=t.c-shift;}camera.origin=camera.origin-shift;camera.target=camera.target-shift;}
 for(auto& t:scene.triangles){t.a=quantize(t.a);t.b=quantize(t.b);t.c=quantize(t.c);}
}
int run_ray_spawn_cases(const std::filesystem::path& out,bool validate,bool bounded,bool rebase,bool omit,bool bad_spawn){
 std::filesystem::create_directories(out);unsigned cases=0,failed=0,secondary=0,shadow=0;bool validation=false;
 std::ostringstream rows;rows<<std::setprecision(12);bool comma=false;
 {
 Owner owner;owner.initialize(validate);validation=owner.validation_enabled;
 for(double scale:{.001,1.,1000.})for(double translation:{0.,1048576.,-16777216.})for(int sign:{-1,1})for(int oblique:{0,1})for(int kind=0;kind<5;++kind){
  const double gap=1e-5*scale;ref::Scene scene;scene.materials={{1,{.5,.5,.5},.5,0,{},br::MaterialModel::lambert},{2,{1,1,1},.5,0,{1,1,1},br::MaterialModel::lambert}};
  auto plane=[&](double z,uint32_t material){scene.triangles.push_back({uint32_t(scene.triangles.size()+1),material,{-scale,-scale,z},{scale,-scale,z},{0,scale,z}});};
  plane(0,1);if(kind==0)plane(sign*gap,1);if(kind==2)plane(sign*(scale-gap),1);if(kind>=2)plane(sign*scale,2);if(kind>=2&&oblique){auto& light=scene.triangles.back();light.a.x+=.2*scale;light.b.x+=.2*scale;light.c.x+=.2*scale;}
  ref::Camera camera;camera.origin={-.1*scale,0,-sign*scale};camera.target={0,0,0};const br::Vec3 shift{translation,-translation,translation};rebase_fixture(scene,camera,shift,rebase);
  const auto primary_direction=br::normalize(br::Vec3{.1,0,double(sign)});const auto direction=br::normalize(br::Vec3{oblique?.2:0,oblique?.1:0,sign*(oblique?.001:1.)});
  ref::RenderConfig config;config.width=config.height=1;SceneAcceleration acceleration(owner,scene);NativePipeline pipeline(owner);pipeline.initialize(*acceleration.tlas);MappedScene mapped(owner,scene,config,camera);if(bounded)mapped.root->flags|=16;
  mapped.root->camera_forward={float(primary_direction.x),float(primary_direction.y),float(primary_direction.z),0};mapped.root->camera_right={float(direction.x),float(direction.y),float(direction.z),kind==4?2.f:(kind>=2?1.f:0.f)};
  mapped.dispatch(pipeline,0,1,bad_spawn?3:2,omit);const auto hit=mapped.hits[0],spawn=mapped.accumulation[0];const int expected=kind==4?0:((kind==0||kind==2)?1:-1);
  const double actual_gap=kind==0?std::abs(scene.triangles[1].a.z-scene.triangles[0].a.z):(kind==2?std::abs(scene.triangles[2].a.z-scene.triangles[1].a.z):0);
  bool pass=std::isfinite(hit.x)&&std::isfinite(hit.y)&&std::isfinite(hit.z)&&hit.w==1&&hit.x==0&&hit.y==float(expected)&&std::isfinite(spawn.x)&&std::isfinite(spawn.y)&&std::isfinite(spawn.z)&&std::isfinite(spawn.w)&&spawn.w>=0;
  // Plane membership/side and a real gap are independent of the spawn algorithm.
  // Bound the offset below the physical separation for the close-layer fixture.
  const double local_spawn_z=double(spawn.z)-(rebase?0:translation);
  if(kind==0)pass=pass&&sign*local_spawn_z>0&&sign*local_spawn_z<actual_gap;
  if(expected==1)pass=pass&&hit.z>0;
  ++cases;if(kind<2)++secondary;else ++shadow;if(!pass){++failed;if(failed<=8)std::cerr<<"Ray fixture failure scale="<<scale<<" translation="<<translation<<" sign="<<sign<<" kind="<<kind<<" oblique="<<oblique<<" hit="<<hit.x<<','<<hit.y<<" spawnZ="<<local_spawn_z<<'\n';}
  if(comma)rows<<',';comma=true;rows<<"{\"scale\":"<<scale<<",\"world_translation\":"<<translation<<",\"sign\":"<<sign<<",\"oblique\":"<<oblique<<",\"kind\":"<<kind<<",\"gap\":"<<gap<<",\"actual_uploaded_gap\":"<<actual_gap<<",\"hit\":[";for(float value:{hit.x,hit.y,hit.z,hit.w}){if(std::isfinite(value))rows<<value;else rows<<"null";rows<<',';}rows.seekp(-1,std::ios_base::cur);rows<<"],\"spawn\":[";for(float value:{spawn.x,spawn.y,spawn.z,spawn.w}){if(std::isfinite(value))rows<<value;else rows<<"null";rows<<',';}rows.seekp(-1,std::ios_base::cur);rows<<"],\"local_spawn_z\":";if(std::isfinite(local_spawn_z))rows<<local_spawn_z;else rows<<"null";rows<<",\"expected_index\":"<<expected<<",\"passed\":"<<(pass?"true":"false")<<'}';
 }
 }
 const bool pass=failed==0&&validation_errors==0&&validation_warnings==0;
 std::ofstream receipt(out/"ray-spawn-result.json");receipt<<"{\"status\":\""<<(pass?"passed":"failed")<<"\",\"candidate\":\"triangle_error_box_v1\",\"bounded_rays\":"<<(bounded?"true":"false")<<",\"bad_spawn\":"<<(bad_spawn?"true":"false")<<",\"omit_dispatch\":"<<(omit?"true":"false")<<",\"camera_relative_rebase\":"<<(rebase?"true":"false")<<",\"cases\":"<<cases<<",\"secondary_cases\":"<<secondary<<",\"shadow_cases\":"<<shadow<<",\"failed_cases\":"<<failed<<",\"validation_enabled\":"<<(validation?"true":"false")<<",\"validation_errors\":"<<validation_errors<<",\"validation_warnings\":"<<validation_warnings<<",\"fixtures\":["<<rows.str()<<"],\"scope\":\"Original layered-triangle analytic identity oracle, scales0.001/1/1000, gap1e-5*scale, three translations up to2^24, front/back, two directions and a synthetic short-connection regression for conservative uncertainty-budget rejection. Nominal gaps may change during double translation; actual uploaded gaps are serialized. Traversal extent allowance is empirical for the tested identity-instance GPU; no universal Vulkan error proof, arbitrary transforms, subnormal arithmetic or unrepresentable geometry claim.\"}\n";receipt.close();require(bool(receipt),"Ray fixture receipt write failed");require(pass,"Ray spawn fixture gate failed");std::cout<<"PASS ray spawn fixtures "<<cases<<'\n';return 0;
}
