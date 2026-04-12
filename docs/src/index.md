# Mora

**Declarative patching for Skyrim Special Edition.**

Mora replaces dozens of runtime patchers — SPID, KID, SkyPatcher, and more — with a single compiled DLL. Write rules in a clean, Python-inspired language. Mora evaluates them at compile time and bakes the results into native code.

> **6,782 NPC name patches applied in 1.63 milliseconds.**

## Quick Example

```mora
namespace my_mod.balance

requires mod("Skyrim.esm")

# All iron weapons get 99 damage
iron_weapons(Weapon):
    weapon(Weapon)
    has_keyword(Weapon, :WeapMaterialIron)
    => set_damage(Weapon, 99)
```

```bash
$ mora compile balance.mora --data-dir ~/.steam/.../Skyrim Special Edition/Data
✓ 200 patches baked into native code
✓ MoraRuntime.dll (16.5 KB)
✓ Compiled 1 rules in 389ms
```

Drop the DLL in `Data/SKSE/Plugins/` and launch the game. That's it.

## Why Mora?

- **Fast**: patches apply in under 2ms, not minutes
- **Declarative**: say what you want, not how to do it
- **Compiled**: rules are evaluated once, results baked into a native DLL
- **Unified**: one language replaces SPID, KID, SkyPatcher, and custom SKSE plugins

## Get Started

- [Getting Started](getting-started.md): install Mora and write your first rule
- [How Mora Works](how-mora-works.md): understand the design and why it's fast
- [Language Guide](language-guide.md): learn the syntax
- [Examples](examples.md): real-world `.mora` files
