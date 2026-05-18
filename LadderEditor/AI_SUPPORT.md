# PiLab AI Support Documents

This folder includes the final AI-facing support files for generating PiLab Ladder Editor projects.

Use these files in this order:

1. `schema/pilab-ladder-project.schema.json` — machine-readable shape validation.
2. `docs/LADDER_PROJECT_SCHEMA.md` — human-readable project format and semantics.
3. `docs/PILAB_AI_GENERATION_GUIDE.md` — compact rules for AI assistants.
4. `examples/mixed_ladder_script_example.json` — valid mixed ladder/script example.

The most important AI rule is this: `ONS`, `TON`, `TOF`, `CTU`, and `CTD` tags are inline block instance names. Do not generate a normal `NO` or `NC` contact using the same tag as one of those instances. Place the block inline when its output should drive downstream ladder logic. `TON`, `TOF`, `CTU`, and `CTD` require positive presets; `ONS` does not.
