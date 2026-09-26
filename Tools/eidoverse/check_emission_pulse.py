"""Sample one complete live pulse; emits PASS or an assertion in the editor log."""
import unreal,time
def start_emission_pulse_check():
    w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    m=next(c for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Actor)
           for c in a.get_components_by_class(unreal.EidoverseAvatarMesh) if c.get_num_bones()==445)
    slot=next(i for i,n in enumerate(m.get_material_slot_names()) if 'lampglass' in str(n).lower())
    material=m.get_material(slot);values=[];started=time.monotonic()
    def tick(dt):
        values.append(material.get_scalar_parameter_value('emissiveStrength'))
        if time.monotonic()-started<3.6:return
        unreal.unregister_slate_post_tick_callback(handle)
        assert len(values)>20 and max(values)-min(values)>.1,'Emissive material is not pulsing'
        assert 0<=min(values)<max(values)<=1.351
        unreal.log('MYTHOS_PULSE_PASS: strength %.4f to %.4f over %d frames'%(min(values),max(values),len(values)))
    handle=unreal.register_slate_post_tick_callback(tick)
start_emission_pulse_check()
