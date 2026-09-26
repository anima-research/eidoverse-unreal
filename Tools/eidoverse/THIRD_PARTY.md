# Runtime avatar dependency

`Plugins/glTFRuntime` vendors https://github.com/rdeioris/glTFRuntime at commit
`cef7be26f803b1728508218957ea9d1baf01400d` (version 20260826).
The upstream MIT license is retained in `Plugins/glTFRuntime/LICENSE`.
Source and material assets are included; no production avatars are bundled.

The underwater adapter uses glTF geometry/material/skin decoding plus its own
VRM 0.x/1.0 humanoid mapping. This is not a complete VRM/MToon implementation.

Rebuild UnderwaterOptics with UAT BuildPlugin and
`-Dependencies=<project>/Plugins/glTFRuntime/glTFRuntime.uplugin`.
Build glTFRuntime separately too for its editor module. Install the resulting
Mac binaries in each plugin's `Binaries` directory with Unreal closed.
The glTFRuntime material content is included in the project's always-cook list.
The project's lit avatar templates in `Content/Underwater/Avatars/Materials`
derive from those MIT-licensed material assets and are also always cooked.
`Tools/eidoverse/build_lit_materials.py` reproduces the template adaptation;
the original upstream license remains in the vendored plugin.
