"""Multi-scale world-space sediment and per-instance biological motion."""
import unreal
from editor_toolset.toolsets.material import MaterialTools as M
MEL=unreal.MaterialEditingLibrary
ROOT='/Game/Underwater/Ecology/Materials/'
def prop(o,n,v):o.set_editor_property(n,v)
def expr(mat,cls,**kw):
 o=MEL.create_material_expression(mat,cls)
 for n,v in kw.items():prop(o,n,v)
 return o
def custom(mat,code,inputs,output=unreal.CustomMaterialOutputType.CMOT_FLOAT3):
 o=expr(mat,unreal.MaterialExpressionCustom,code=code,output_type=output)
 pins=[]
 for name in inputs:
  p=unreal.CustomInput();prop(p,'input_name',name);pins.append(p)
 prop(o,'inputs',pins)
 for name,node in inputs.items():assert MEL.connect_material_expressions(node,'',o,name)
 return o
def connect(o,p):assert MEL.connect_material_property(o,'',p)
noise='''struct N {
 float h(float2 p){float3 q=frac(float3(p.xyx)*.1031);q+=dot(q,q.yzx+33.33);return frac((q.x+q.y)*q.z);}
 float n(float2 p){float2 i=floor(p),f=frac(p);f=f*f*(3-2*f);return lerp(lerp(h(i),h(i+float2(1,0)),f.x),lerp(h(i+float2(0,1)),h(i+1),f.x),f.y);}
 float f(float2 p){return .57*n(p)+.28*n(p*2.13+17.3)+.15*n(p*4.71-8.1);}
 float cells(float2 p){float2 i=floor(p),f=frac(p);float d=2;for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){float2 q=float2(x,y);float2 r=q+float2(h(i+q),h(i+q+71.2))-f;d=min(d,dot(r,r));}return sqrt(d);}
};N n;
'''
for name in ('Sand','Rock','Deck','Galvanized','Sponge','Shell','Cucumber'):
 mat=unreal.load_asset(ROOT+'M_'+name);assert mat
 for e in M.get_expressions(mat):MEL.delete_material_expression(mat,e)
 prop(mat,'tangent_space_normal',False)
 p=expr(mat,unreal.MaterialExpressionWorldPosition);normal=expr(mat,unreal.MaterialExpressionVertexNormalWS)
 if name=='Sand':
  code=noise+'''
 float2 warp=float2(n.f(P.xy*.0008),n.f(P.xy*.0008+63))*950;
 float2 Q=P.xy+warp;
 float macro=n.f(Q*.00038),meso=n.f(Q*.008),fine=n.f(P.xy*.21);
 float patch=smoothstep(.42,.66,n.f(Q*.0015));
 float slope=1-saturate(Normal.z);
 float rock=saturate(smoothstep(.07,.31,slope)+patch*.35);
 float silt=smoothstep(3800,10000,-P.z)*(1-rock);
 float meadow=(1-smoothstep(1500,2600,length((P.xy-float2(0,6000))*float2(1,.85))))*(1-smoothstep(1200,1900,-P.z));
 float gravel=smoothstep(.49,.66,n.f(Q*.0027))* (1-silt*.85);
 float footprint=max(length(ddx(P)),length(ddy(P)));
 float micro=1-smoothstep(.7,4.5,footprint);
 float2 grainWarp=float2(n.f(P.xy*.035),n.f(P.xy*.035+18))*2;
 float pebble=n.cells(P.xy*float2(.062,.087)+grainWarp);
 float pebbles=(1-smoothstep(.1,.75,pebble))*(.35+.65*n.n(P.xy*.023));
 float angle=(n.f(Q*.00065)-.5)*2.5;
 float2 dir=float2(cos(angle),sin(angle));
 float ripple=sin(dot(P.xy,dir)*.074+n.f(Q*.006)*5);
 float rippleMask=(1-gravel)*(1-silt)*(1-rock)*(1-meadow*.8)*smoothstep(.35,.62,macro);
 float3 sand=lerp(float3(.17,.155,.12),float3(.34,.31,.235),macro);
 float3 color=sand*(.84+meso*.3);
 color=lerp(color,float3(.15,.16,.145)*(.65+meso*.7),rock);
 color=lerp(color,float3(.13,.12,.10)*(.8+macro*.35),silt*.8);
 color=lerp(color,float3(.12,.15,.075)*(.8+meso*.4),meadow*patch*.7);
 color=lerp(color,lerp(float3(.16,.155,.13),float3(.29,.265,.215),pebbles),gravel*.32*micro);
 float shell=smoothstep(.81,.91,n.n(P.xy*.14))*smoothstep(.5,.65,n.n(Q*.027))*micro;
 color=lerp(color,float3(.48,.43,.33),shell*(.2+gravel*.6));
 color*=1+((fine-.5)*.24+ripple*rippleMask*.035)*micro;
 float relief=(pebbles*gravel*.55+(fine-.5)*.16+ripple*rippleMask*.35+(meso-.5)*.45)*micro;
 return float4(color,relief);
'''
 elif name=='Rock':
  code=noise+'''
 float2 Q=P.xy+P.z*float2(.71,.37);float macro=n.f(Q*.002);
 float fine=n.f(Q*.16);float cracks=1-smoothstep(.015,.065,abs(n.f(Q*.019)-.52));
 float crust=smoothstep(.51,.69,n.f(Q*.009))*(.4+.6*saturate(Normal.z));
 float3 color=lerp(float3(.075,.087,.078),float3(.24,.245,.205),macro);
 color*=1-cracks*.18;color=lerp(color,float3(.25,.14,.155),crust*.38);
 float micro=1-smoothstep(1,7,max(length(ddx(P)),length(ddy(P))));
 return float4(color*(.87+fine*.26),((fine-.5)*.65-cracks*.25)*micro);
'''
 else:
  rgb={'Deck':(.22,.25,.24),'Galvanized':(.40,.43,.41),'Sponge':(.30,.20,.09),'Shell':(.44,.39,.30),'Cucumber':(.14,.10,.07)}[name]
  code=noise+f'float f=n.f(P.xy*.09+P.z*.013);float3 c=float3{rgb}*(.72+f*.5);return float4(c,(f-.5)*.15);'
 packed=custom(mat,code,{'P':p,'Normal':normal},unreal.CustomMaterialOutputType.CMOT_FLOAT4)
 base=custom(mat,'return V.rgb;',{'V':packed});h=custom(mat,'return V.a;',{'V':packed},unreal.CustomMaterialOutputType.CMOT_FLOAT1)
 connect(base,unreal.MaterialProperty.MP_BASE_COLOR)
 bump=custom(mat,'''float3 dpdx=ddx(P),dpdy=ddy(P);float3 r1=cross(dpdy,N),r2=cross(N,dpdx);float det=dot(dpdx,r1);float3 grad=sign(det)*(ddx(H)*r1+ddy(H)*r2);return normalize(abs(det)*N-grad);''',{'P':p,'N':normal,'H':h})
 connect(bump,unreal.MaterialProperty.MP_NORMAL)
 rough=custom(mat,'return lerp(.79,.96,saturate((-P.z-800)/9000));' if name=='Sand' else 'return .84;',{'P':p},unreal.CustomMaterialOutputType.CMOT_FLOAT1)
 if name in ('Deck','Galvanized'):rough=expr(mat,unreal.MaterialExpressionConstant,r=.6 if name=='Deck' else .42)
 connect(rough,unreal.MaterialProperty.MP_ROUGHNESS)
 connect(expr(mat,unreal.MaterialExpressionConstant,r=.65 if name in ('Deck','Galvanized') else 0),unreal.MaterialProperty.MP_METALLIC)
 MEL.layout_material_expressions(mat);M.recompile(mat);unreal.EditorAssetLibrary.save_loaded_asset(mat)
 unreal.log('ECOLOGY_LAYERED_MATERIAL_OK '+name)

for name in ('Silver','Back','Fin','Mackerel','Perch','Ray'):
 mat=unreal.load_asset(ROOT+'M_'+name);assert mat
 # Replace only motion; preserve existing shared fish surface parameters.
 p=expr(mat,unreal.MaterialExpressionLocalPosition,included_offsets=unreal.PositionIncludedOffsets.EXCLUDE_OFFSETS)
 t=expr(mat,unreal.MaterialExpressionTime)
 phase=expr(mat,unreal.MaterialExpressionPerInstanceCustomData,data_index=0,const_default_value=0)
 speed=expr(mat,unreal.MaterialExpressionPerInstanceCustomData,data_index=1,const_default_value=1)
 if name=='Ray':code='return float3(0,0,sin(T*2.1+Phase-abs(P.y)*.035)*pow(saturate(abs(P.y)/75),1.3)*7);'
 else:code='float tail=pow(saturate((-P.x+4)/22),1.7);return float3(0,sin(T*(7+Speed*2)+Phase+P.x*.11)*tail*3.1,0);'
 bend=custom(mat,code,{'P':p,'T':t,'Phase':phase,'Speed':speed})
 tr=expr(mat,unreal.MaterialExpressionTransform,transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
 assert MEL.connect_material_expressions(bend,'',tr,'');connect(tr,unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
 if name in ('Mackerel','Perch'):
  code='float bar=pow(saturate(sin(P.x*.7+sin(P.z*.45)*1.3)),5)*smoothstep(-2,5,P.z);return lerp(float3(.27,.38,.34),float3(.025,.065,.07),bar*.8);' if name=='Mackerel' else 'float bar=pow(saturate(sin(P.x*.48+P.z*.06)),3);return lerp(float3(.36,.29,.16),float3(.12,.14,.11),bar*.6);'
  connect(custom(mat,code,{'P':p}),unreal.MaterialProperty.MP_BASE_COLOR)
 MEL.layout_material_expressions(mat);M.recompile(mat);unreal.EditorAssetLibrary.save_loaded_asset(mat)
 unreal.log('ECOLOGY_INDIVIDUAL_MOTION_OK '+name)
unreal.log('ECOLOGY_ENRICH_MATERIALS_SUCCESS')
