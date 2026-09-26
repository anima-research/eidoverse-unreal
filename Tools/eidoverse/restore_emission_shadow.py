import unreal
qa=getattr(unreal,'_mythos_emission_qa',None)
if qa:
    for a in qa['props']:a.destroy_actor()
    for light,visible in qa['lights']:light.set_visibility(visible)
    qa['lamp'].set_intensity(qa['lamp_intensity'])
    qa['lamp'].set_cast_shadows(qa['lamp_shadow'])
    qa['pawn'].set_actor_location(qa['pos'],False,True)
    qa['pc'].set_control_rotation(qa['rotation'])
    qa['bridge'].set_actor_tick_enabled(qa['tick'])
    unreal._mythos_emission_qa=None
    unreal.log('MYTHOS_SHADOW_TEST_RESTORED')
