# Underwater prototype — Test1

Open `Test1.uproject` in Unreal Engine 5.8 and load
`/Game/Underwater/Maps/L_Epipelagic`.

Source, Unreal assets and original procedural authoring assets are versioned here.
Large binary assets use Git LFS. No remote is configured or pushed automatically.
Editor caches, Saved state and generated plugin binaries are intentionally excluded;
build the UnderwaterOptics plugin for your local Unreal installation after cloning.

Authoring notes:

- `Tools/vessel_redesign/README.md`: vessels, optics, ocean, controls and particulate fixes.
- `Tools/marine_ecology/README.md`: terrain, plants, fauna and verification scripts.

## Local setup

Run `git lfs install --local`. This workspace also uses a clean filter to keep the
Android File Server development token local while retaining all other engine settings:

```sh
git config filter.local-dev-config.clean 'python3 Tools/git_clean_config.py'
```

The token is blank in repository history; the existing local runtime value is untouched.
Authoring scripts currently contain workspace-specific absolute paths documented in
their individual notes; update them before regenerating assets on another machine.
