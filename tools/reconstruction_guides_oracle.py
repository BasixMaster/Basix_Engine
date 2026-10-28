"""Independent local binary reconstruction-guide catalog/arithmetic checks.

Original MIT implementation. Uses no SDK/runtime; immutableWorld's nearest-hit
and actual-miss geometry oracle remains the separately frozen C++ gate.
"""
# SPDX-License-Identifier: MIT
from pathlib import Path
import hashlib
import math
import struct

PIXEL = struct.Struct('<16f4I4f')
SIZES = ((64,36),)*4 + ((48,27),(64,36),(96,54),(64,36))
RESETS = (True,False,False,False,False,True,True,True)
IDS = (10000123,90000007)


def require(ok, why):
    if not ok:
        raise ValueError(why)


def f32(x):
    return struct.unpack('<f',struct.pack('<f',x))[0]


def add(a,b): return tuple(x+y for x,y in zip(a,b))
def sub(a,b): return tuple(x-y for x,y in zip(a,b))
def scale(a,s): return tuple(x*s for x in a)
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def length(a): return math.hypot(*a)
def cross(a,b): return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def unit(a): return scale(a,1/length(a))


def camera(frame):
    origin=(0.,0.,7.);target=(0.,-.1,0.)
    if frame>=2: origin=(f32(.08),0.,7.);target=(f32(.08),-.1,0.)
    if frame>=5: origin=(.5,f32(.3),6.5);target=(0.,f32(-.1),0.)
    forward=unit(sub(target,origin));right=unit(cross(forward,(0.,1.,0.)));up=cross(right,forward)
    return dict(origin=origin,forward=forward,right=right,up=up,tangent=math.tan(math.pi/8),aspect=16/9)


def settings(frame):
    return ((.25 if frame%2 else -.25),(.125 if frame%3 else -.375),6. if frame==7 else .1,7. if frame==7 else 128.)


def matrices(frame):
    a=[1.,0.,0.,f32(f32(-1.3)-f32(.7)),0.,1.,0.,0.,0.,0.,1.,0.]
    b=[-1.,.25,0.,f32(f32(1.3)+f32(.7)),0.,1.,0.,0.,.25,0.,1.,0.]
    if frame>=3:
        a[0]=a[10]=f32(math.cos(.05));a[2]=f32(math.sin(.05));a[8]=-a[2];a[3]=f32(a[3]+f32(.12));b[7]=f32(b[7]+f32(.08))
    return a,b


def transform(m,p):
    return tuple(((m[r*4]*p[0]+m[r*4+1]*p[1])+m[r*4+2]*p[2])+m[r*4+3] for r in range(3))


def inverse_point(m,p):
    # Independent pivoted linear solve, rather than the host cofactor formula.
    a=[[m[r*4+c] for c in range(3)]+[p[r]-m[r*4+3]] for r in range(3)]
    for c in range(3):
        pivot=max(range(c,3),key=lambda r:abs(a[r][c]));a[c],a[pivot]=a[pivot],a[c]
        require(a[c][c]!=0,'Frozen object inverse singular')
        div=a[c][c];a[c]=[v/div for v in a[c]]
        for r in range(3):
            if r!=c:
                factor=a[r][c];a[r]=[x-factor*y for x,y in zip(a[r],a[c])]
    return tuple(row[3] for row in a)


def geometry():
    triangles=[]
    def quad(a,b,c,d,material,start):
        triangles.extend(((start,material,(a,b,c)),(start+1,material,(a,c,d))))
    quad((-1,-1,-1),(1,-1,-1),(1,-1,1),(-1,-1,1),0,10)
    quad((-1,1,1),(1,1,1),(1,1,-1),(-1,1,-1),0,12)
    quad((-1,-1,-1),(-1,1,-1),(1,1,-1),(1,-1,-1),0,14)
    quad((-1,-1,1),(-1,1,1),(-1,1,-1),(-1,-1,-1),1,16)
    quad((1,-1,-1),(1,1,-1),(1,1,1),(1,-1,1),2,18)
    quad((-.35,.98,-.25),(.35,.98,-.25),(.35,.98,.25),(-.35,.98,.25),5,20)
    def box(a,b,material,start):
        x,y,z=a;X,Y,Z=b
        quads=(((x,y,z),(X,y,z),(X,Y,z),(x,Y,z)),((X,y,Z),(x,y,Z),(x,Y,Z),(X,Y,Z)),
               ((x,y,Z),(x,y,z),(x,Y,z),(x,Y,Z)),((X,y,z),(X,y,Z),(X,Y,Z),(X,Y,z)),
               ((x,Y,z),(X,Y,z),(X,Y,Z),(x,Y,Z)),((x,y,Z),(X,y,Z),(X,y,z),(x,y,z)))
        for k,(v0,v1,v2,v3) in enumerate(quads):
            # Host reverses each closed-box triangle for outward winding.
            triangles.extend(((start+2*k,material,(v0,v2,v1)),(start+2*k+1,material,(v0,v3,v2))))
    box((-.72,-.99,-.05),(-.12,-.30,.50),4,100)
    box((.16,-.99,-.65),(.69,.02,-.12),3,112)
    ident,mat,(a,b,c)=triangles[10];point=add(a,scale(sub(b,a),.2));triangles[10]=(ident,mat,(a,point,c));triangles.append((124,5,(point,b,c)))
    return [(i,m,tuple(tuple(f32(x) for x in p) for p in tri)) for i,m,tri in triangles]


MATERIALS = ((.75,.75,.75,.5,0),(.75,.12,.08,.5,0),(.08,.55,.15,.5,0),
             (.95,.65,.22,.28,1),(.25,.35,.75,.5,0),(0,0,0,.5,0))


def world_catalog(frame):
    result={}
    for slot,m in enumerate(matrices(frame)):
        for primitive,material,tri in geometry():
            world=tuple(tuple(f32(x) for x in transform(m,p)) for p in tri)
            normal=unit(cross(sub(world[1],world[0]),sub(world[2],world[0])))
            if slot==1: normal=scale(normal,-1)
            result[(1000 if slot==0 else 2000)+primitive]=(slot,material,world,normal)
    return result


def uv(p,c):
    delta=sub(p,c['origin']);z=dot(delta,c['forward'])
    if not z>0: return None
    result=(.5+.5*dot(delta,c['right'])/(z*c['aspect']*c['tangent']),.5-.5*dot(delta,c['up'])/(z*c['tangent']))
    return result if all(math.isfinite(v) for v in result) else None


def expected_ray(frame,x,y,size=None,clip=None):
    c=camera(frame);w,h=size or SIZES[frame];jx,jy,_,_=clip or settings(frame)
    px=(2*(x+.5-jx)/w-1)*c['aspect']*c['tangent'];py=(1-2*(y+.5-jy)/h)*c['tangent']
    return unit(add(c['forward'],add(scale(c['right'],px),scale(c['up'],py))))


def check_pixel(values,frame,x,y,catalog,*,size=None,reset=None,clip=None):
    floats=values[:16]+values[20:];errors=[]
    def check(ok,message):
        if not ok: errors.append(message)
    if not all(math.isfinite(v) for v in floats): return ['nonfinite/poisoned readback'],None
    world=values[:3];linear=values[3];normal=values[4:7];rough=values[7];albedo=values[8:11];metal=values[11];mx,my,depth,mvalid=values[12:16];pid,iid,mat,found=values[16:20];ray=values[20:23]
    check(values[23]==1,'ray.w differs');check(length(sub(ray,expected_ray(frame,x,y,size,clip)))<2e-6,'frozen jittered camera ray differs');check(abs(dot(ray,ray)-1)<2e-6,'ray not unit')
    if not dot(ray,ray)>0:return errors+['zero primary ray'],None
    c=camera(frame);_,_,near,far=clip or settings(frame)
    if found==0:
        check(all(v==0 for v in values[:12]) and (mx,my,depth,mvalid)==(0,0,1,0) and (pid,iid,mat)==(0,0,0),'miss clear or masks differ')
        return errors,dict(hit=False,motion=False,depth_error=0.,motion_error=0.)
    check(found==1,'invalid hit flag');check(pid in catalog,'stable primitive outside frozen catalog')
    if found!=1 or pid not in catalog:return errors,None
    slot,expected_material,tri,authored=catalog[pid];check(iid==IDS[slot] and mat==expected_material,'stable instance/material identity differs')
    material=tuple(f32(v) for v in MATERIALS[expected_material]);check(length(sub(albedo,material[:3]))<1e-6 and abs(rough-material[3])<1e-6 and abs(metal-material[4])<1e-6,'material channels differ')
    check(abs(length(normal)-1)<2e-6 and length(sub(normal,authored))<5e-5,'authored unit normal differs')
    delta=sub(world,c['origin']);z=dot(delta,c['forward']);check(near-2e-4<=z<=far+2e-4 and abs(linear-z)<2e-4,'linear/clip depth differs')
    if not z>0:return errors+['nonpositive hit view depth'],None
    expected_depth=far*(z-near)/(z*(far-near)) if z>0 else math.inf;de=abs(depth-expected_depth);check(de<2e-5,'noninverted perspective depth differs')
    t=dot(delta,ray)/dot(ray,ray);check(t>0 and length(sub(delta,scale(ray,t)))<2e-4,'world hit not on submitted primary ray')
    # Returned triangle containment and plane check, independent of World.
    a,b,d=tri;e=sub(b,a);f=sub(d,a);v=sub(world,a);ee=dot(e,e);ff=dot(f,f);ef=dot(e,f);denom=ee*ff-ef*ef
    beta=(dot(v,e)*ff-dot(v,f)*ef)/denom;gamma=(dot(v,f)*ee-dot(v,e)*ef)/denom
    area=length(cross(e,f));alt=area/max(length(e),length(f),length(sub(d,b)));tol=2e-4/alt
    check(abs(dot(v,unit(cross(e,f))))<2e-4 and beta>=-tol and gamma>=-tol and beta+gamma<=1+tol,'world hit outside returned frozen triangle')
    me=0.;want_valid=False
    if not (RESETS[frame] if reset is None else reset):
        current_uv=uv(world,c);old_point=transform(matrices(frame-1)[slot],inverse_point(matrices(frame)[slot],world));old_uv=uv(old_point,camera(frame-1))
        want_valid=current_uv is not None and all(0<=v<1 for v in current_uv) and near<=z<=far and old_uv is not None
        if want_valid:
            w,h=size or SIZES[frame];want=((old_uv[0]-current_uv[0])*w,(old_uv[1]-current_uv[1])*h)
            me=math.hypot(mx-want[0],my-want[1]);check(me<.002,'previous material-point motion differs')
    check(mvalid==(1 if want_valid else 0),'history motion-valid flag differs')
    if not want_valid:check(mx==0 and my==0,'reset/invalid motion must be zero')
    return errors,dict(hit=True,motion=want_valid,depth_error=de,motion_error=me)


def verify_receipt(path,require_passed=True):
    import json
    path=Path(path);result=json.loads(path.read_text(encoding='utf-8'))
    require(result.get('frames')==8 and result.get('pixels')==20304 and result.get('guide_root_bytes')==128 and result.get('guide_pixel_bytes')==96,'Frame count or guide ABI differs')
    rows=result['sequence'];require(len(rows)==8,'Sequence length differs')
    summaries=[];total_failed=0;total_hits=0;total_misses=0;total_motion=0
    for frame,row in enumerate(rows):
        w,h=SIZES[frame];require(row.get('frame')==frame and row.get('width')==w and row.get('height')==h and row.get('reset') is RESETS[frame] and row.get('history') is (not RESETS[frame]) and row.get('pixels')==w*h,'Frozen frame settings/history differ')
        c=camera(frame)
        for key in ('origin','forward','right','up'):
            actual=row['camera_'+key];expected=c[key]+({'origin':0.,'forward':c['tangent'],'right':c['aspect'],'up':0.}[key],)
            require(len(actual)==4 and all(isinstance(x,(int,float)) and math.isfinite(x) and abs(x-y)<2e-7 for x,y in zip(actual,expected)),'Runtime camera '+key+' differs from frozen pose')
        jnf=row['jitter_near_far'];require(len(jnf)==4 and all(math.isfinite(x) and abs(x-y)<2e-7 for x,y in zip(jnf,settings(frame))),'Runtime jitter/clip range differs')
        raw=path.parent/('frame-'+str(frame))/'guides.bin';data=raw.read_bytes();require(len(data)==w*h*PIXEL.size,'Binary guide dimensions/ABI differ')
        failures=0;hits=0;misses=0;motions=0;de=0.;me=0.;examples=[];catalog=world_catalog(frame)
        for index,values in enumerate(PIXEL.iter_unpack(data)):
            errors,metrics=check_pixel(values,frame,index%w,index//w,catalog)
            if errors:
                failures+=1
                if len(examples)<8:examples.append(dict(pixel=index,errors=errors))
            if metrics:
                hits+=metrics['hit'];misses+=not metrics['hit'];motions+=metrics['motion'];de=max(de,metrics['depth_error']);me=max(me,metrics['motion_error'])
        if require_passed:
            require(failures==0,'Independent guide pixel checks failed frame'+str(frame)+': '+str(examples[:2]))
            require(row['failed_pixels']==0 and row['hits']==hits and row['misses']==misses and row['valid_motions']==motions,'CPU/independent frame count accounting differs')
        total_failed+=failures;total_hits+=hits;total_misses+=misses;total_motion+=motions
        summaries.append(dict(frame=frame,pixels=w*h,failed_pixels=failures,hits=hits,misses=misses,valid_motions=motions,depth_max_error=de,motion_max_error_pixels=me,guide_sha256=hashlib.sha256(data).hexdigest(),failure_examples=examples))
    if require_passed:require(result.get('status')=='passed' and result.get('failed_pixels')==0 and result.get('hits')==total_hits and result.get('misses')==total_misses and result.get('valid_motions')==total_motion,'CPU/independent overall accounting differs')
    return dict(frames=8,pixels=20304,failed_pixels=total_failed,hits=total_hits,misses=total_misses,valid_motions=total_motion,numeric_readback_rechecked=True,sequence=summaries,
                scope='Independent frozen rays, returned-hit containment/normal/material/depth, rigid material-point motion and masks; C++ immutableWorld separately checks nearest-hit/miss geometry')


def matrix_product(a,b):
    return tuple(sum(a[r*4+k]*b[k*4+c] for k in range(4)) for r in range(4) for c in range(4))


def projection_matrices():
    tangent=math.tan(math.pi/8);sx=1/(tangent*16/9);sy=1/tangent;a=128/(128-.1);b=-.1*a
    return ((sx,0,0,0,0,sy,0,0,0,0,a,1,0,0,b,0),
            (1/sx,0,0,0,0,1/sy,0,0,0,0,0,1/b,0,0,1,-a/b))


def camera_to_world(c):
    return c['right']+(0,)+c['up']+(0,)+c['forward']+(0,)+c['origin']+(1,)


def world_to_camera(c):
    r,u,f,p=c['right'],c['up'],c['forward'],c['origin']
    return (r[0],u[0],f[0],0,r[1],u[1],f[1],0,r[2],u[2],f[2],0,-dot(p,r),-dot(p,u),-dot(p,f),1)


def check_dynamic_constants(row,frame,reset):
    constants=row['sdk_constants'];active=row['sdk_evaluated'];w,h=row['render_width'],row['render_height']
    if not active:
        require(constants is None,'Fallback must not claim submitted SDK constants')
        require(row['tag_extents'] is None,'Fallback must not claim submitted SDK resource tags')
        return
    p,inv=projection_matrices();now=camera(frame);old=now if reset else camera(frame-1)
    temporal=matrix_product(matrix_product(matrix_product(inv,camera_to_world(now)),world_to_camera(old)),p)
    reverse=matrix_product(matrix_product(matrix_product(inv,camera_to_world(old)),world_to_camera(now)),p)
    for key,expected in (('camera_view_to_clip',p),('clip_to_camera_view',inv),('clip_to_previous_clip',temporal),('previous_clip_to_clip',reverse)):
        actual=constants[key];require(len(actual)==16 and all(math.isfinite(v) and abs(v-e)<=2e-6*max(1,abs(e)) for v,e in zip(actual,expected)),'Submitted SDK '+key+' differs from independent row-vector projection')
    require(constants['reset'] is reset and constants['camera_motion_included'] is True and constants['motion_vectors_jittered'] is False and constants['depth_inverted'] is False,'Submitted SDK temporal/depth flags differ')
    for key,expected in (('near',.1),('far',128),('fov',math.pi/4),('aspect',16/9)):
        require(abs(constants[key]-f32(expected))<1e-7,'Submitted SDK '+key+' differs')
    for key,expected in (('camera_pos',now['origin']),('camera_forward',now['forward']),('camera_right',now['right']),('camera_up',now['up'])):
        require(len(constants[key])==3 and all(math.isfinite(v) and abs(v-e)<2e-7 for v,e in zip(constants[key],expected)),'Submitted SDK camera vector differs')
    require(tuple(constants['jitter'])==settings(frame)[:2] and tuple(constants['mvec_scale'])==(f32(1/w),f32(1/h)),'SDK jitter sign or motion pixel scale differs')
    require(row['tag_extents']==dict(input=[w,h],output=[row['output_width'],row['output_height']],exposure=[1,1]),'Submitted SDK resource tag extents differ')


def decode_file(path,code,count):
    data=path.read_bytes();item=struct.calcsize('<'+code);require(len(data)==item*count,'Raw artifact dimensions differ: '+path.name)
    return data,struct.unpack('<'+str(count)+code,data)


def check_dynamic_buffers(directory,row,guide_data):
    w,h,ow,oh=(row[k] for k in ('render_width','render_height','output_width','output_height'));n=w*h;outn=ow*oh
    color_data,color=decode_file(directory/'input-color.rgba16','e',n*4);depth_data,depth=decode_file(directory/'input-depth.f32','f',n);motion_data,motion=decode_file(directory/'input-motion.rg32','f',n*2)
    output_data,output=decode_file(directory/'output.rgba16','e',outn*4);engine_data,engine=decode_file(directory/'engine-output.rgba32','f',outn*4);sum_data,sums=decode_file(directory/'radiance-sum.rgba32','f',n*4)
    require(all(math.isfinite(v) for v in color+depth+motion+output+engine+sums),'Nonfinite raw input/output artifact')
    max_upload=0.;input_values=[];out_values=[]
    for i in range(n):
        require(color[i*4+3]==1 and sums[i*4+3]==16,'Input color alpha or path sample count differs')
        if struct.unpack_from('<I',guide_data,i*96+76)[0]==0:
            require(tuple(sums[i*4:i*4+3])==(0,0,0) and tuple(color[i*4:i*4+3])==(0,0,0),'Center-sampled guide miss must have exact black primary radiance/input')
        for c in range(3):
            expected=sums[i*4+c]/16;actual=color[i*4+c];error=abs(actual-expected)
            require(expected>=0 and actual>=0 and error<=max(.00001,abs(expected)*.0011),'Path sum/16 to half input upload differs')
            max_upload=max(max_upload,error);input_values.append(actual)
        require(depth_data[i*4:i*4+4]==guide_data[i*96+56:i*96+60] and motion_data[i*8:i*8+8]==guide_data[i*96+48:i*96+56],'Native depth/motion bytes differ from guide export')
    for i in range(outn):
        for c in range(4):require(engine[i*4+c]==output[i*4+c],'Restored engine/native-half copy differs')
        if row['fallback']:require(engine[i*4+3]==1,'Bilinear fallback constant input alpha differs')
        for c in range(3):
            value=engine[i*4+c];require(-.1<=value<1e5,'SDK output sentinel/range failure');out_values.append(value)
    mean=math.fsum(input_values)/len(input_values);outmean=math.fsum(out_values)/len(out_values);variation=math.fsum(v*v for v in out_values)/len(out_values)-outmean*outmean
    require(mean>1e-5 and .25*mean<outmean<4*mean and variation>1e-8,'Output liveness/mean/variation gate failed')
    require(math.isclose(mean,row['input_mean'],rel_tol=1e-10,abs_tol=1e-12) and math.isclose(outmean,row['output_mean'],rel_tol=1e-10,abs_tol=1e-12),'Recorded input/output means differ from raw artifacts')
    fallback_max=0.;fallback_components=0
    if row['fallback']:
        xs=[]
        for x in range(ow):
            sx=f32(f32(f32(x+.5)*w)/ow);sx=f32(sx-.5);px=math.floor(sx);xs.append((max(0,min(w-1,px)),max(0,min(w-1,px+1)),f32(sx-px)))
        for y in range(oh):
            sy=f32(f32(f32(y+.5)*h)/oh);sy=f32(sy-.5);py=math.floor(sy);fy=f32(sy-py);y0=max(0,min(h-1,py));y1=max(0,min(h-1,py+1))
            for x,(x0,x1,fx) in enumerate(xs):
                for c in range(3):
                    a,b,c0,d=(color[(yy*w+xx)*4+c] for yy,xx in ((y0,x0),(y0,x1),(y1,x0),(y1,x1)))
                    expected=(a+(b-a)*fx)*(1-fy)+(c0+(d-c0)*fx)*fy;actual=engine[(y*ow+x)*4+c];error=abs(actual-expected)
                    require(error<=max(.00002,abs(expected)*.002),'Independent bilinear fallback differs')
                    fallback_max=max(fallback_max,error);fallback_components+=1
    pfm=directory/'linear.pfm';data=pfm.read_bytes();header=f'PF\n{ow} {oh}\n-1.0\n'.encode('ascii')
    require(data.startswith(header) and len(data)==len(header)+outn*12,'Output HDR shape differs')
    rgb=struct.unpack('<'+str(outn*3)+'f',data[len(header):])
    bottom_up=tuple(engine[((oh-1-y)*ow+x)*4+c] for y in range(oh) for x in range(ow) for c in range(3))
    require(bottom_up==rgb,'Bottom-up output HDR bytes disagree with restored engine RGB')
    paths=('guides.bin','input-color.rgba16','input-depth.f32','input-motion.rg32','output.rgba16','engine-output.rgba32','radiance-sum.rgba32','linear.pfm')
    return dict(input_component_checks=n*6,output_component_checks=outn*3,native_engine_rgba_comparisons=outn*4,fallback_component_checks=fallback_components,max_color_upload_error=max_upload,max_fallback_error=fallback_max,input_mean=mean,output_mean=outmean,output_variation=variation,
                artifact_sha256={name:hashlib.sha256((directory/name).read_bytes()).hexdigest() for name in paths})


def verify_dynamic_receipt(path):
    import json
    path=Path(path);result=json.loads(path.read_text(encoding='utf-8'));forced=result['forced_fallback'];injected=result['injected_nonprojectable_history'];rows=result['sequence']
    require(type(forced) is bool and type(injected) is bool and not(forced and injected) and len(rows)==11 and result['frames']==11,'Dynamic frame profile differs');summaries=[];total_pixels=0;total_motion=0;inputs=0;outputs=0;fallback_components=0;settings_by_output={}
    for frame,row in enumerate(rows):
        w,h=row['render_width'],row['render_height'];ow,oh=(384,216) if frame>=7 else (320,180);reset=frame in (0,5,7,9) or injected and frame==2;active=not forced and frame!=8
        require(row['frame']==frame and row['output_width']==ow and row['output_height']==oh and row['reset'] is reset and row['history'] is (not reset) and row['sdk_evaluated'] is active and row['fallback'] is (not active),'Dynamic lifecycle sequence differs')
        require(type(w) is int and type(h) is int and 0<w<=512 and 0<h<=512 and row['guide_pixels']==w*h and row['guide_failed_pixels']==0 and row['spp']==16 and row['display_aspect']==ow/oh,'Dynamic render metadata differs')
        optimum=(row['optimal_render_width'],row['optimal_render_height']);minimum=(row['render_width_min'],row['render_height_min']);maximum=(row['render_width_max'],row['render_height_max'])
        if forced:
            require((w,h)==(ow*2//3,oh*2//3) and optimum+minimum+maximum==(0,)*6,'Forced fallback must use independent nominal input dimensions without SDK settings')
        else:
            require(all(type(v) is int and v>0 for v in optimum+minimum+maximum) and all(lo<=v<=hi for lo,v,hi in zip(minimum,(w,h),maximum)),'Input extent outside reported SDK range')
            reported=optimum+minimum+maximum
            require(settings_by_output.setdefault((ow,oh),reported)==reported,'Identical SDK options returned inconsistent frozen extent metadata')
            wanted=tuple(max(lo,min(hi,v-v//4)) for lo,v,hi in zip(minimum,optimum,maximum)) if frame==4 else optimum
            require((w,h)==wanted and (frame!=4 or (w,h)!=optimum),'SDK optimal/dynamic input selection differs')
        c=camera(frame);clip=settings(frame)[:2]+(.1,128.)
        for key in ('origin','forward','right','up'):
            expected=c[key]+({'origin':0.,'forward':c['tangent'],'right':c['aspect'],'up':0.}[key],);actual=row['camera_'+key]
            require(len(actual)==4 and all(math.isfinite(v) and abs(v-e)<2e-7 for v,e in zip(actual,expected)),'Dynamic frozen camera basis differs')
        require(len(row['jitter_near_far'])==4 and all(abs(v-e)<2e-7 for v,e in zip(row['jitter_near_far'],clip)) and tuple(row['jitter'])==clip[:2],'Dynamic jitter/clip convention differs')
        previous=camera(max(0,frame-1));previous_forward=previous['forward']
        if injected and frame==2:previous_forward=scale(previous_forward,-1)
        expected_forward=previous_forward+(previous['tangent'],)
        require(len(row['previous_forward_before_reset'])==4 and all(math.isfinite(v) and abs(v-e)<2e-7 for v,e in zip(row['previous_forward_before_reset'],expected_forward)),'Runtime pre-reset previous forward differs from frozen/injected camera')
        check_dynamic_constants(row,frame,reset);directory=path.parent/('frame-'+str(frame));data=(directory/'guides.bin').read_bytes();require(len(data)==w*h*96,'Dynamic raw guide shape differs')
        catalog=world_catalog(frame);hits=0;misses=0;motions=0;de=0.;me=0.
        for index,values in enumerate(PIXEL.iter_unpack(data)):
            errors,metrics=check_pixel(values,frame,index%w,index//w,catalog,size=(w,h),reset=reset,clip=clip)
            require(not errors,'Dynamic guide frame'+str(frame)+' pixel'+str(index)+': '+str(errors))
            hits+=metrics['hit'];misses+=not metrics['hit'];motions+=metrics['motion'];de=max(de,metrics['depth_error']);me=max(me,metrics['motion_error'])
        require(row['hits']==hits and row['misses']==misses and row['valid_motions']==motions,'Dynamic raw guide/host count accounting differs')
        proof={}
        if injected and frame==2:
            before=(directory/'pre-reset-guides.bin').read_bytes();require(len(before)==w*h*96,'Pre-reset proof guide shape differs');invalid=0
            for index,values in enumerate(PIXEL.iter_unpack(before)):
                errors,metrics=check_pixel(values,frame,index%w,index//w,catalog,size=(w,h),reset=True,clip=clip)
                require(not errors,'Malformed injected pre-reset readback: '+str(errors))
                if metrics['hit']:
                    require(values[15]==0 and values[12]==values[13]==0,'Pre-reset foreground must have invalid zero motion')
                    slot=catalog[values[16]][0];old_point=transform(matrices(frame-1)[slot],inverse_point(matrices(frame)[slot],values[:3]));old_z=dot(sub(old_point,previous['origin']),previous_forward)
                    require(old_z<=0,'Injected previous camera must independently make foreground nonprojectable');invalid+=1
            require(invalid>0 and invalid==row['pre_reset_invalid_foreground']==hits and motions==0,'Pre-reset invalid foreground recount/reset output differs')
            proof=dict(pre_reset_invalid_foreground=invalid,pre_reset_guide_sha256=hashlib.sha256(before).hexdigest(),injected_previous_projection_rechecked=True)
        else:
            require(row['pre_reset_invalid_foreground']==0 and not(directory/'pre-reset-guides.bin').exists(),'Unexpected pre-reset proof or invalid-foreground count')
        buffers=check_dynamic_buffers(directory,row,data);inputs+=buffers['input_component_checks'];outputs+=buffers['output_component_checks'];fallback_components+=buffers['fallback_component_checks'];total_pixels+=w*h;total_motion+=motions
        summaries.append(dict(frame=frame,guide_pixels=w*h,hits=hits,misses=misses,valid_motions=motions,depth_max_error=de,motion_max_error_pixels=me,**proof,**buffers))
    require(result['guide_pixel_checks']==total_pixels and result['input_component_checks']==inputs and result['output_component_checks']==outputs and result['fallback_component_checks']==fallback_components,'Dynamic aggregate component accounting differs')
    return dict(frames=11,injected_nonprojectable_history=injected,guide_pixel_checks=total_pixels,input_component_checks=inputs,output_component_checks=outputs,fallback_component_checks=fallback_components,valid_motions=total_motion,numeric_readback_rechecked=True,sequence=summaries,
                scope='Independent guide catalog, SDK constants/tags, all raw input/native/engine/HDR conversions and every fallback bilinear RGB; SDK output mean/variation is liveness, not temporal reconstruction quality')
