"""Remove only superseded writes, preserving each event's remaining execution chain."""
import unreal
from editor_toolset.toolsets.blueprint import BlueprintTools as BT

for name in ('BP_DiverPawn','BP_Habitat','BP_Cyclops'):
    bp=unreal.load_asset('/Game/Underwater/Blueprints/'+name)
    count=0
    for graph in BT.list_graphs(bp):
        for node in BT.find_nodes(graph,title=''):
            info=BT.get_node_infos([node])[0]
            operation=info.type_id.split('|')[-1]
            remove=operation in ('SetFogNow','SetFogActor','SetFogDensity','SetInsideHabitat')
            if operation=='SetVectorParameterValue':
                parameter=next((p.value for p in info.input_pins if p.name=='ParameterName'),'')
                remove=parameter in ('TorchPos','TorchDir') or parameter.startswith(('Hab0','Hab1'))
            if not remove: continue
            incoming=[c for p in info.input_pins if p.type_id=='Exec' for c in p.connected_pins]
            outgoing=[c for p in info.output_pins if p.type_id=='Exec' for c in p.connected_pins]
            assert len(incoming)<=1 and len(outgoing)<=1,info.type_id
            assert not any(p.connected_pins for p in info.output_pins if p.type_id!='Exec'),info.type_id
            BT.delete_node(node)
            if incoming and outgoing: BT.connect_pins(incoming[0],outgoing[0])
            count+=1
    BT.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_loaded_asset(bp)
    unreal.log(f'OPTICS_CLEANUP {name}: removed {count} obsolete writes; compiled successfully')
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
