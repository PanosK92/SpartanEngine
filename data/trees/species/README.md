# Tree species presets

Each JSON file in a region folder is one species for the Tree Creator (`Editor > Tree Creator`, or the `tree_*` MCP commands).
The window exposes only seven controls (age, branchiness, leafiness, wildness, season, seed, quality); everything that makes a
species look like itself lives in these files.

## Layout

- `id`, `name`, `scientific`, `region`, `description`: identity, the region string groups the species list.
- `class`: `broadleaf`, `conifer`, `palm` or `shrub`, picks defaults and the impostor style.
- `budget`: lod 0 triangle target at the standard quality, foliage gets about 70% of it.
- `height`, `height_var`: metres at age 0.5.
- `trunk`: radius (fraction of height), taper, flare, lean, gnarl, twist, multi stem split.
- `crown`: envelope shape and base height, leaves outside it are pruned.
- `levels`: Weber-Penn style branch levels (count, start/end along the parent, length, down angle, curve, gravity...).
- `leaves`: shape, leaf size in metres, card size, density, colours per season, flowers, translucency.
  Cards grow with the tree (up to 1.6x past 12 m) so tall crowns stay covered within the budget.
- `palm`: fronds, frond length and droop, replaces leaf cards for palms and tree ferns.
- `bark`: painter style (`furrowed`, `plated`, `smooth`, `birch`, `twisted`, `fibrous`...), colours in linear space.
  Real bark albedo sits around 0.05 to 0.25 linear, anything brighter reads as painted.

Edits are picked up by `tree_species_list` with `reload: true`, no restart needed.
Changing a texture painter in `source/geometry/TreeTextures.cpp` needs the version bump in `texture_hash` (TreeSpecies.cpp).

## Triangle counts

Default controls (age, branchiness, leafiness and wildness at 0.5, summer, seed 1, standard quality).
The last lod of every non palm species is the foliage impostor, so the far tree costs a handful of triangles plus bark.

| species | region | lod 0 | bark | foliage | lods | height m |
|---|---|---|---|---|---|---|
| olive | greece | 5656 | 2600 | 3056 | 5656 / 2017 / 914 / 430 / 246 | 6.1 |
| aleppo_pine | greece | 5561 | 2921 | 2640 | 5561 / 1878 / 862 / 355 / 195 | 16.2 |
| carob | greece | 6904 | 2608 | 4296 | 6904 / 2561 / 1168 / 579 / 323 | 10.3 |
| greek_fir | greece | 13664 | 3584 | 10080 | 13664 / 5586 / 2461 / 1253 / 649 | 23.9 |
| holm_oak | greece | 11303 | 2903 | 8400 | 11303 / 4663 / 2106 / 1071 / 567 | 13.7 |
| mediterranean_cypress | greece | 6204 | 2044 | 4160 | 6204 / 2539 / 1048 / 535 / 287 | 17.8 |
| oriental_plane | greece | 8590 | 3170 | 5420 | 8590 / 3362 / 1554 / 707 / 383 | 22.3 |
| stone_pine | italy | 5442 | 854 | 4588 | 5442 / 2515 / 1169 / 603 / 327 | 20.6 |
| lombardy_poplar | italy | 8595 | 2939 | 5656 | 8595 / 3479 / 1408 / 715 / 375 | 27.6 |
| english_oak | central_europe | 10920 | 3776 | 7144 | 10920 / 4255 / 1923 / 938 / 510 | 22.8 |
| european_beech | central_europe | 11853 | 3521 | 8332 | 11853 / 4768 / 2216 / 1056 / 556 | 25.4 |
| horse_chestnut | central_europe | 5926 | 2426 | 3500 | 5926 / 2210 / 1007 / 480 / 268 | 17.9 |
| weeping_willow | central_europe | 11064 | 1964 | 9100 | 11064 / 4949 / 2257 / 1165 / 617 | 15.5 |
| norway_spruce | scandinavia | 12616 | 2536 | 10080 | 12616 / 5470 / 2461 / 1253 / 649 | 30.9 |
| scots_pine | scandinavia | 5737 | 2545 | 3192 | 5737 / 2043 / 966 / 419 / 227 | 23.1 |
| silver_birch | scandinavia | 9260 | 2856 | 6404 | 9260 / 3655 / 1746 / 807 / 423 | 20.7 |
| japanese_maple | japan | 6421 | 1873 | 4548 | 6421 / 2733 / 1196 / 652 / 380 | 5.4 |
| cherry_blossom | japan | 7685 | 3173 | 4512 | 7685 / 2797 / 1292 / 606 / 334 | 9.9 |
| japanese_black_pine | japan | 6719 | 2347 | 4372 | 6719 / 2769 / 1203 / 587 / 323 | 13.1 |
| coast_redwood | north_america | 12653 | 2573 | 10080 | 12653 / 5561 / 2476 / 1253 / 649 | 65.7 |
| sugar_maple | north_america | 9945 | 3205 | 6740 | 9945 / 3913 / 1818 / 867 / 463 | 24.8 |
| quaking_aspen | north_america | 5635 | 1035 | 4600 | 5635 / 2547 / 1156 / 591 / 315 | 17.5 |
| southern_live_oak | north_america | 11420 | 3772 | 7648 | 11420 / 4463 / 2010 / 1000 / 540 | 13.2 |
| date_palm | middle_east | ~450 | 143 | ~300 | 4 lods, no impostor | 14.5 |
| cedar_of_lebanon | middle_east | 12669 | 2589 | 10080 | 12669 / 5499 / 2495 / 1273 / 669 | 26.8 |
| baobab | africa | 3950 | 3050 | 900 | 3950 / 1313 / 450 / 287 / 88 | 18.0 |
| umbrella_thorn_acacia | africa | 5425 | 1017 | 4408 | 5425 / 2558 / 1129 / 601 / 337 | 9.2 |
| river_red_gum | australia | 6231 | 3507 | 2724 | 6231 / 2297 / 934 / 390 / 219 | 25.9 |
| tree_fern | australia | 488 | 117 | 371 | 4 lods, no impostor | 4.4 |
| jacaranda | south_america | 8114 | 3250 | 4864 | 8114 / 3092 / 1435 / 662 / 370 | 14.1 |
| monkey_puzzle | south_america | 8302 | 2430 | 5872 | 8302 / 3345 / 1468 / 749 / 397 | 27.7 |
| bamboo | asia | 2905 | 1349 | 1556 | 2905 / 1351 / 804 / 616 / 524 | 16.5 |
| ginkgo | asia | 6336 | 2812 | 3524 | 6336 / 2454 / 1097 / 485 / 247 | 22.4 |
