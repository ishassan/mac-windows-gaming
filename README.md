# Windows Games on Mac

A collection of guides and patches for running classic Windows games on Mac (Apple Silicon).

## Games

### Revenant (1999)

| Approach | Description | Status |
|----------|-------------|--------|
| [CrossOver Wine](revenant-crossover-wine/) | Uses CrossOver Wine from Homebrew with a display mode hook. Proven and fully working, but game renders with black borders (640x480 in a 960x600 display). | Working |
| [Porting Kit Fullscreen](revenant-porting-kit-fullscreen/) | Uses Porting Kit with a custom DirectDraw replacement that scales 640x480 to fill the entire screen. Cinematics and menus work, but starting gameplay fails on texture loading. | In Progress |

### Commandos: Behind Enemy Lines (1998)

| Approach | Description | Status |
|----------|-------------|--------|
| [Native port](commandos-native/) | Static recompilation of the game's x86 code to arm64 (M-HT/SR) with a native Windows API layer on SDL2. No Wine, no Rosetta. Needs the GOG game data. | Single-player working |
