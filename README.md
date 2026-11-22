# Food Is Just Food

SKSE plugin for Skyrim SE / AE. Food feeds you; it doesn't make you a better warrior or mage.

When the game loads, every food (from any mod) loses the effects that raise combat or character stats: health,
magicka and stamina and their regeneration, resistances, carry weight, speed, skills. Everything else stays: hunger
and warmth (SunHelm, Survival, CACO), drunkenness, diseases and poisonings, every harmful effect and every scripted
one. No plugin (.esp): nothing is saved in your game. Prices stay as they were.

- Exceptions: `SKSE/Plugins/FoodIsJustFood.json`.
- What was changed: `Documents/My Games/Skyrim Special Edition/SKSE/FoodIsJustFood.log`.

Built by GitHub Actions (`.github/workflows/build.yml`): the package goes to the `dist` branch, the logs to `ci-logs`.
