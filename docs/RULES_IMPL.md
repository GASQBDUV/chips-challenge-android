# Engine vs re/RULES.md

Implemented as specified (section numbers = RULES.md): two layers up/lo (1,4,5), tile tables TT[] driving legality (generated), 110 ms game tick with global never-reset tick counter (0),
chip_move with B flag + one-slot queue C and per-tick `game_input` (3,4), forced sliding moves every tick with bounce and corner redirect (3,7), slip list for blocks/creatures (6),
creatures on even ticks in field-10 order, teeth/blob on tick%4==0, unlisted creatures inert, creatures on clone machines not added at load, tank halts when blocked (5),
move_block (water->dirt, bomb, fire, ice slide, teleport) (6), teleport reverse row-major scan (7), traps start per file flag, trap_recalc/brown press (7),
red button clone, green toggle on both layers, blue button tank reverse (7), thief/doors/socket/pass-once/blue walls/hidden walls (4), timer at tick%10==0 (3),
death codes 1-6 and messages (8), score = 10*time_left + level*500*0.8^tries (stop when <500), replay adds only improvement (8), RNG = MSVC rand (0).

Verified by `test/test_rules.c` (79 checks): B/C key queue timing, upper layer follows Chip, drowning/fire + flippers/fire boots, doors (green not consumed), socket, block push/wall/water/bomb/fire/two blocks,
thin walls, fake/real blue wall, hidden 0x2C vs 0x05, dirt, pass-once wall, thief (boots only), bomb, exit (no chip check), ice (1 tile/tick, bounce, skates, corner 1B), force floor (carry, steer, suction boots),
teleport, clone machine, trap close/open, toggle walls, timer phase (tick%10) and carry-over, untimed levels, bug/paramecium/glider/fireball/ball/tank/teeth/blob timing and preference orders,
fire/water/bomb vs creatures, inert unlisted creatures, block sliding on ice. `test/solve.c` plays the real CHIPS.DAT: greedy BFS solves 12 of the first 30 levels (incl. blocks-into-water, clone/trap/bomb lesson, force floors,
80-blob level 23); unsolved ones need non-greedy planning (not evidence of engine bugs; level 7 was solved by an earlier, different engine and is worth a look).

Not implemented / simplifications: mouse click-to-move; the "skip level after 10 failures" offer; story strings every 10 levels and the end sequence; tick sound/bell/click1/chimes/hit3 WAVs are not in the user's file set
(substitutes: socket = DOOR.WAV, time warning = CLICK3.WAV, thief/bomb = STRIKE.WAV); `idle` face-S only after 2 idle even ticks as in the pseudocode; 110 ms nominal tick (real Win3.1 timer granularity may give ~110 ms);
level passwords gate the menu level select (RULES: required unless record saved - same effect); creature move order inside the slip list uses swap-removal (can shift one tick in rare removals).
