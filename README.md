What is this?
=============

This is a map editor for game servers that derivied from [OpenTibia](https://github.com/opentibia/server) server project.

It is a fork of a [Map Editor](https://github.com/hampusborgos/rme) created by [Remere](https://github.com/hampusborgos).

You can find an engine compatible with OTBM format at [OTAcademy](https://github.com/OTAcademy), [OTLand](https://github.com/OTLand), [OpenTibiaBR](https://github.com/OpenTibiaBR) or other OT communities.

Visit [OTAcademy discord](http://discord.gg/OTAcademy) if you are looking for support or updates.

Procedural world generation
===========================

The development world planner builds seeded continents, biomes, urban blocks,
furnished multi-floor buildings and connected caves. PNGs can guide elevation
and biome regions; generation is previewed before one undoable application.

See the [world generation guide](docs/WORLD_GENERATION.md) for editor usage,
recipes, client bindings and the `rme --generate-world` batch command. The
[bundled 10.98 city](data/procedural/recipes/example_furnished_city_1098.json)
provides a reproducible furniture example. The
[large continent profile](data/procedural/recipes/example_large_continent_1098.json)
exports more than three million tiles with an explicit batch budget and optional
OTBM readback verification. Tracking: [issue #5](https://github.com/mateusbellei/rme/issues/5).

I want to contribute
====================

Contributions are very welcome, if you would like to make any changes, fork this project or request commit access.

Please, if you would like to contribute anything, documentation, extensions or code speak up!


Bugs
======

Have you found a bug? Please create an issue in our [bug tracker](https://github.com/OTAcademy/rme/issues)

Other Applications
==========

* To host your MMORPG game server, you can use [The Forgotten Server Plus](https://github.com/Zbizu/forgottenserver).
* To play your MMORPG game, you can use [OTClient 1.0](https://github.com/Mehah/otclient)
* To map your MMORPG game, you can use this map editor.

Download
========

You can find official releases at this repository [releases page](https://github.com/OTAcademy/RME/releases).

Compiling
=========
Required libraries:
* wxWidgets >= 3.0
* Boost >= 1.55.0

### VCPKG libraries:
* 32-bit : `vcpkg install wxwidgets freeglut asio nlohmann-json fmt libarchive boost-spirit`
* 64-bit : `vcpkg install --triplet x64-windows wxwidgets freeglut asio nlohmann-json fmt libarchive boost-spirit`

[Compile on Windows](https://github.com/hjnilsson/rme/wiki/Compiling-on-Windows)

[Compile on Ubuntu](https://github.com/hjnilsson/rme/wiki/Compiling-on-Ubuntu)

[Compile on Arch Linux](https://github.com/hjnilsson/rme/wiki/Compiling-on-Arch-Linux)

[Compile on macOS](https://github.com/hjnilsson/rme/wiki/Compiling-on-macOS)
